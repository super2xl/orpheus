#include "application.h"
#include "core/dma_interface.h"
#include "core/runtime_manager.h"
#include "core/task_manager.h"
#include "utils/logger.h"
#include "utils/bookmarks.h"
#include "utils/telemetry.h"
#include "analysis/disassembler.h"
#include "analysis/pattern_scanner.h"
#include "analysis/string_scanner.h"
#include "analysis/memory_watcher.h"
#include "analysis/pe_dumper.h"
#include "analysis/rtti_parser.h"
#include "analysis/cfg_builder.h"
#include "analysis/function_recovery.h"
#include "analysis/signature.h"
#include "mcp/mcp_server.h"
#include "emulation/emulator.h"
#include "dumper/cs2_schema.h"
#include "decompiler/decompiler.hh"
#include "embedded_resources.h"
#include "version.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <GLFW/glfw3.h>

// OpenGL for texture functions (GL_CLAMP_TO_EDGE is 1.2+ but Windows gl.h is 1.1)
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// stb_image for icon loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <regex>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace orpheus::ui {

static void glfw_error_callback(int error, const char* description) {
    LOG_ERROR("GLFW Error {}: {}", error, description);
}

Application::Application() = default;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(const std::string& title, int width, int height) {
    if (!InitializeGLFW()) {
        return false;
    }

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    // Set window icon (if available)
    SetWindowIcon();

    // Query DPI scale for high-DPI displays
    float xscale, yscale;
    glfwGetWindowContentScale(window_, &xscale, &yscale);
    dpi_scale_ = std::max(xscale, yscale);
    if (dpi_scale_ < 1.0f) dpi_scale_ = 1.0f;
    LOG_INFO("DPI scale factor: {:.2f}", dpi_scale_);

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if (!InitializeImGui()) {
        glfwDestroyWindow(window_);
        glfwTerminate();
        return false;
    }

    SetupImGuiStyle();
    RegisterKeybinds();

    // Create DMA interface
    dma_ = std::make_unique<DMAInterface>();

    // Create disassembler
    disassembler_ = std::make_unique<analysis::Disassembler>(true);

    // Create bookmark manager and load saved bookmarks
    bookmarks_ = std::make_unique<BookmarkManager>();
    bookmarks_->Load();

    LOG_INFO("Orpheus initialized successfully");
    return true;
}

bool Application::InitializeGLFW() {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    return true;
}

bool Application::InitializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Note: ViewportsEnable disabled - causes orphan windows on some setups
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Store ini path in member variable (ImGui keeps the pointer)
    ini_path_ = (RuntimeManager::Instance().GetConfigDirectory() / "orpheus_layout.ini").string();
    io.IniFilename = ini_path_.c_str();

    // Load fonts using member font_size_
    RebuildFonts();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void Application::SetupImGuiStyle() {
    // Apply common style settings with DPI scaling
    ImGuiStyle& style = ImGui::GetStyle();
    float s = dpi_scale_;  // Shorthand for scaling

    // Rounding - softer, more refined feel
    style.WindowRounding = 0.0f;           // Keep sharp for docking compatibility
    style.FrameRounding = 4.0f * s;        // Softer buttons/inputs
    style.ScrollbarRounding = 6.0f * s;    // Rounded scrollbars
    style.GrabRounding = 4.0f * s;         // Softer slider grabs
    style.TabRounding = 4.0f * s;          // Slightly rounded tabs
    style.ChildRounding = 4.0f * s;        // Rounded child windows
    style.PopupRounding = 4.0f * s;        // Rounded popups/tooltips

    // Border sizes
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;            // Clean tab look

    // Padding and spacing - more breathing room
    style.WindowPadding = ImVec2(10.0f * s, 10.0f * s);
    style.FramePadding = ImVec2(8.0f * s, 4.0f * s);
    style.ItemSpacing = ImVec2(8.0f * s, 6.0f * s);
    style.ItemInnerSpacing = ImVec2(6.0f * s, 6.0f * s);
    style.IndentSpacing = 20.0f * s;
    style.ScrollbarSize = 14.0f * s;       // Easier to grab
    style.GrabMinSize = 10.0f * s;
    style.DockingSeparatorSize = 2.0f * s;

    // Table padding
    style.CellPadding = ImVec2(6.0f * s, 4.0f * s);

    // Alignment
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Apply current theme colors
    ApplyTheme(current_theme_);
}

void Application::RebuildFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // Only clear if fonts already exist (runtime rebuild vs initial load)
    bool is_runtime_rebuild = (io.Fonts->Fonts.Size > 0);
    if (is_runtime_rebuild) {
        io.Fonts->Clear();
    }

    // Apply DPI scaling to font sizes
    float scaled_font_size = font_size_ * dpi_scale_;
    float scaled_mono_size = (font_size_ - 1.0f) * dpi_scale_;

    // Font config with oversampling for better anti-aliasing
    ImFontConfig font_config;
    font_config.OversampleH = 2;  // Horizontal oversampling for crisp text
    font_config.OversampleV = 1;  // Vertical oversampling
    font_config.PixelSnapH = true;  // Snap to pixel grid for sharp rendering

    // Helper to load embedded font with anti-aliasing config
    auto LoadEmbeddedFont = [&io, &font_config](const unsigned char* data, size_t size, float fontSize) -> ImFont* {
        void* font_data = IM_ALLOC(size);
        memcpy(font_data, data, size);
        return io.Fonts->AddFontFromMemoryTTF(font_data, (int)size, fontSize, &font_config);
    };

    font_regular_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_regular_ttf,
        orpheus::embedded::jetbrainsmono_regular_ttf_size,
        scaled_font_size
    );

    font_bold_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_bold_ttf,
        orpheus::embedded::jetbrainsmono_bold_ttf_size,
        scaled_font_size
    );

    font_mono_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_medium_ttf,
        orpheus::embedded::jetbrainsmono_medium_ttf_size,
        scaled_mono_size
    );

    if (font_regular_ && font_bold_ && font_mono_) {
        LOG_INFO("Loaded fonts at size {:.0f}px (DPI scale: {:.2f})", scaled_font_size, dpi_scale_);
    } else {
        LOG_WARN("Failed to load embedded fonts, using default");
        io.Fonts->AddFontDefault();
    }

    // Only rebuild atlas and invalidate texture at runtime (after OpenGL backend init)
    if (is_runtime_rebuild) {
        io.Fonts->Build();
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();  // Immediately recreate
    }
}

void Application::ToggleFullscreen() {
    if (is_fullscreen_) {
        // Restore windowed mode - use nullptr for monitor to exit fullscreen
        glfwSetWindowMonitor(window_, nullptr,
                             windowed_x_, windowed_y_,
                             windowed_width_, windowed_height_,
                             GLFW_DONT_CARE);
        is_fullscreen_ = false;

        // Reset ImGui/docking layout to handle new size
        first_frame_ = true;

        LOG_INFO("Exited fullscreen mode");
    } else {
        // Save current window position/size before going fullscreen
        glfwGetWindowPos(window_, &windowed_x_, &windowed_y_);
        glfwGetWindowSize(window_, &windowed_width_, &windowed_height_);

        // Get primary monitor and its current video mode
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        // Use borderless fullscreen - matching current video mode avoids mode switch
        // This is smoother than exclusive fullscreen as it doesn't change display modes
        glfwSetWindowMonitor(window_, monitor, 0, 0,
                             mode->width, mode->height,
                             mode->refreshRate);
        is_fullscreen_ = true;

        // Reset ImGui/docking layout to handle new size
        first_frame_ = true;

        LOG_INFO("Entered fullscreen mode ({}x{} @ {}Hz)",
                 mode->width, mode->height, mode->refreshRate);
    }
}

void Application::SetWindowIcon() {
    if constexpr (orpheus::embedded::has_icon) {
        int width, height, channels;
        unsigned char* pixels = stbi_load_from_memory(
            orpheus::embedded::orpheus_png,
            static_cast<int>(orpheus::embedded::orpheus_png_size),
            &width, &height, &channels, 4  // Force RGBA
        );

        if (pixels) {
            GLFWimage icon;
            icon.width = width;
            icon.height = height;
            icon.pixels = pixels;
            glfwSetWindowIcon(window_, 1, &icon);
            stbi_image_free(pixels);
            LOG_INFO("Window icon set ({}x{})", width, height);
        } else {
            LOG_WARN("Failed to decode window icon");
        }
    } else {
        LOG_DEBUG("No icon embedded, using default");
    }
}

void Application::ApplyTheme(Theme theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    switch (theme) {
    case Theme::Dark:
        // Professional dark theme (VS Code / JetBrains style)
        colors[ImGuiCol_Text]                   = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.15f, 0.15f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.10f, 0.10f, 0.10f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.45f, 0.72f, 0.96f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.45f, 0.72f, 0.96f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.55f, 0.82f, 1.00f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.45f, 0.72f, 0.96f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.45f, 0.72f, 0.96f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.45f, 0.72f, 0.96f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.45f, 0.72f, 0.96f, 0.35f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.45f, 0.72f, 0.96f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.65f);
        break;

    case Theme::Light:
        // Clean light theme
        colors[ImGuiCol_Text]                   = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.80f, 0.80f, 0.80f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.95f, 0.95f, 0.95f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.20f, 0.55f, 0.85f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.20f, 0.55f, 0.85f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.15f, 0.45f, 0.75f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.20f, 0.55f, 0.85f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.20f, 0.55f, 0.85f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.20f, 0.55f, 0.85f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.20f, 0.55f, 0.85f, 0.35f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.20f, 0.55f, 0.85f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
        break;

    case Theme::Nord:
        // Nord theme - popular dark theme with blue/teal accent
        colors[ImGuiCol_Text]                   = ImVec4(0.85f, 0.87f, 0.91f, 1.00f);  // Snow Storm
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.54f, 0.60f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);  // Polar Night
        colors[ImGuiCol_ChildBg]                = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.20f, 0.22f, 0.27f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.26f, 0.30f, 0.37f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.22f, 0.25f, 0.31f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.15f, 0.17f, 0.21f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.35f, 0.40f, 0.49f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);  // Frost
        colors[ImGuiCol_CheckMark]              = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.35f, 0.40f, 0.49f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.35f, 0.40f, 0.49f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.40f, 0.49f, 1.00f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.35f, 0.40f, 0.49f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.22f, 0.25f, 0.31f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.53f, 0.75f, 0.82f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.22f, 0.25f, 0.31f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.53f, 0.75f, 0.82f, 0.35f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.10f, 0.12f, 0.15f, 0.65f);
        break;

    case Theme::Dracula:
        // Dracula theme - purple/pink accents on dark background
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.98f, 0.98f, 1.00f);  // #F8F8F2
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.38f, 0.42f, 0.53f, 1.00f);  // #6272A4
        colors[ImGuiCol_WindowBg]               = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);  // #282A36
        colors[ImGuiCol_ChildBg]                = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.18f, 0.19f, 0.24f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.27f, 0.29f, 0.36f, 0.60f);  // #44475A
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.32f, 0.34f, 0.42f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.38f, 0.42f, 0.53f, 1.00f);  // #6272A4
        colors[ImGuiCol_TitleBg]                = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.13f, 0.14f, 0.17f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.38f, 0.42f, 0.53f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.45f, 0.50f, 0.62f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);  // #BD93F9 Purple
        colors[ImGuiCol_CheckMark]              = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.00f, 0.47f, 0.66f, 1.00f);  // #FF79C6 Pink
        colors[ImGuiCol_Button]                 = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.38f, 0.42f, 0.53f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.74f, 0.58f, 0.98f, 0.80f);
        colors[ImGuiCol_Header]                 = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.38f, 0.42f, 0.53f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.74f, 0.58f, 0.98f, 0.60f);
        colors[ImGuiCol_Separator]              = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.74f, 0.58f, 0.98f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.74f, 0.58f, 0.98f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.38f, 0.42f, 0.53f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.20f, 0.21f, 0.26f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.74f, 0.58f, 0.98f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.20f, 0.21f, 0.26f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.27f, 0.29f, 0.36f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.74f, 0.58f, 0.98f, 0.35f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.10f, 0.10f, 0.13f, 0.70f);
        break;

    case Theme::CatppuccinMocha:
        // Catppuccin Mocha - warm pastel colors
        colors[ImGuiCol_Text]                   = ImVec4(0.80f, 0.84f, 0.96f, 1.00f);  // #CDD6F4 Text
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.45f, 0.47f, 0.58f, 1.00f);  // #6C7086 Overlay0
        colors[ImGuiCol_WindowBg]               = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);  // #1E1E2E Base
        colors[ImGuiCol_ChildBg]                = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);  // #181825 Mantle
        colors[ImGuiCol_PopupBg]                = ImVec4(0.14f, 0.14f, 0.20f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.27f, 0.27f, 0.36f, 0.50f);  // #45475A Surface1
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);  // #313244 Surface0
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.24f, 0.24f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.30f, 0.30f, 0.40f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.09f, 0.09f, 0.14f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.35f, 0.35f, 0.46f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.95f, 0.55f, 0.66f, 1.00f);  // #F38BA8 Red
        colors[ImGuiCol_CheckMark]              = ImVec4(0.58f, 0.79f, 0.86f, 1.00f);  // #89DCEB Sky
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.58f, 0.79f, 0.86f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.65f, 0.89f, 0.63f, 1.00f);  // #A6E3A1 Green
        colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.58f, 0.79f, 0.86f, 0.70f);
        colors[ImGuiCol_Header]                 = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.58f, 0.79f, 0.86f, 0.50f);
        colors[ImGuiCol_Separator]              = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.58f, 0.79f, 0.86f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.58f, 0.79f, 0.86f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.58f, 0.79f, 0.86f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.58f, 0.79f, 0.86f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.58f, 0.79f, 0.86f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.27f, 0.27f, 0.36f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.09f, 0.09f, 0.14f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.58f, 0.79f, 0.86f, 0.35f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.58f, 0.79f, 0.86f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.07f, 0.07f, 0.10f, 0.70f);
        break;
    }

    current_theme_ = theme;
}

void Application::RegisterKeybinds() {
    keybinds_ = {
        // General
        {"Connect DMA", "Connect to DMA device", GLFW_KEY_D, GLFW_MOD_CONTROL, [this]() {
            if (dma_ && !dma_->IsConnected() && !dma_connecting_) {
                dma_connecting_ = true;
                LOG_INFO("Connecting to DMA device...");
                dma_connect_future_ = std::async(std::launch::async, [this]() {
                    return dma_->Initialize("fpga");
                });
            }
        }},
        {"Goto Address", "Jump to address", GLFW_KEY_G, GLFW_MOD_CONTROL, [this]() {
            show_goto_dialog_ = true;
        }},
        {"Navigate Back", "Go to previous address", GLFW_KEY_LEFT, GLFW_MOD_ALT, [this]() {
            NavigateBack();
        }},
        {"Navigate Forward", "Go to next address", GLFW_KEY_RIGHT, GLFW_MOD_ALT, [this]() {
            NavigateForward();
        }},
        {"Command Palette", "Open command palette", GLFW_KEY_P, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, [this]() {
            show_command_palette_ = true;
            command_search_[0] = '\0';
        }},
        {"Refresh", "Refresh process list", GLFW_KEY_F5, 0, [this]() {
            RefreshProcesses();
            if (selected_pid_ != 0) RefreshModules();
        }},
        {"Settings", "Open settings", GLFW_KEY_COMMA, GLFW_MOD_CONTROL, [this]() {
            show_settings_ = true;
        }},
        {"Toggle Fullscreen", "Toggle fullscreen mode", GLFW_KEY_F11, 0, [this]() {
            ToggleFullscreen();
        }},

        // Panel toggles - Ctrl+1 through Ctrl+9
        {"Processes", "Toggle process list", GLFW_KEY_1, GLFW_MOD_CONTROL, [this]() {
            panels_.process_list = !panels_.process_list;
        }},
        {"Modules", "Toggle module list", GLFW_KEY_2, GLFW_MOD_CONTROL, [this]() {
            panels_.module_list = !panels_.module_list;
        }},
        {"Memory", "Toggle memory viewer", GLFW_KEY_3, GLFW_MOD_CONTROL, [this]() {
            panels_.memory_viewer = !panels_.memory_viewer;
        }},
        {"Disassembly", "Toggle disassembly", GLFW_KEY_4, GLFW_MOD_CONTROL, [this]() {
            panels_.disassembly = !panels_.disassembly;
        }},
        #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
        {"Decompiler", "Toggle decompiler", GLFW_KEY_D, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, [this]() {
            panels_.decompiler = !panels_.decompiler;
        }},
#endif
        {"Pattern Scanner", "Toggle pattern scanner", GLFW_KEY_5, GLFW_MOD_CONTROL, [this]() {
            panels_.pattern_scanner = !panels_.pattern_scanner;
        }},
        {"String Scanner", "Toggle string scanner", GLFW_KEY_6, GLFW_MOD_CONTROL, [this]() {
            panels_.string_scanner = !panels_.string_scanner;
        }},
        {"Memory Watcher", "Toggle memory watcher", GLFW_KEY_7, GLFW_MOD_CONTROL, [this]() {
            panels_.memory_watcher = !panels_.memory_watcher;
        }},
        {"Emulator", "Toggle emulator", GLFW_KEY_8, GLFW_MOD_CONTROL, [this]() {
            panels_.emulator = !panels_.emulator;
        }},
        {"Console", "Toggle console", GLFW_KEY_GRAVE_ACCENT, GLFW_MOD_CONTROL, [this]() {
            panels_.console = !panels_.console;
        }},
        {"Bookmarks", "Toggle bookmarks", GLFW_KEY_B, GLFW_MOD_CONTROL, [this]() {
            panels_.bookmarks = !panels_.bookmarks;
        }},
        {"RTTI Scanner", "Toggle RTTI scanner", GLFW_KEY_9, GLFW_MOD_CONTROL, [this]() {
            panels_.rtti_scanner = !panels_.rtti_scanner;
        }},
        {"CS2 Schema", "Toggle CS2 schema dumper", GLFW_KEY_C, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, [this]() {
            panels_.cs2_schema = !panels_.cs2_schema;
        }},
        {"CS2 Entity Inspector", "Toggle CS2 entity inspector", GLFW_KEY_E, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, [this]() {
            panels_.cs2_entity_inspector = !panels_.cs2_entity_inspector;
        }},
        {"Function Recovery", "Toggle function recovery panel", GLFW_KEY_F, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, [this]() {
            panels_.function_recovery = !panels_.function_recovery;
        }},
    };
}

void Application::SetupDefaultLayout() {
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, nullptr, &dock_main);
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
    ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25f, nullptr, &dock_main);

    ImGuiID dock_left_bottom = ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.5f, nullptr, &dock_left);

    // Left side - Process explorer
    ImGui::DockBuilderDockWindow("Processes", dock_left);
    ImGui::DockBuilderDockWindow("Modules", dock_left_bottom);

    // Center - Main views
    ImGui::DockBuilderDockWindow("Memory", dock_main);
    ImGui::DockBuilderDockWindow("Disassembly", dock_main);
    ImGui::DockBuilderDockWindow("Decompiler", dock_main);

    // Right side - Analysis
    ImGui::DockBuilderDockWindow("Pattern Scanner", dock_right);
    ImGui::DockBuilderDockWindow("String Scanner", dock_right);
    ImGui::DockBuilderDockWindow("Memory Watcher", dock_right);
    ImGui::DockBuilderDockWindow("Emulator", dock_right);

    // Bottom - Output
    ImGui::DockBuilderDockWindow("Console", dock_bottom);

    ImGui::DockBuilderFinish(dockspace_id);
}

int Application::Run() {
    if (window_ == nullptr) {
        LOG_ERROR("Application not initialized");
        return 1;
    }

    running_ = true;

    while (running_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        if (IsMinimized()) {
            glfwWaitEvents();
            continue;
        }

        ProcessInput();
        BeginFrame();
        RenderDockspace();
        EndFrame();

        first_frame_ = false;
    }

    return 0;
}

void Application::ProcessInput() {
    static std::map<int, bool> key_states;

    for (const auto& kb : keybinds_) {
        bool key_pressed = glfwGetKey(window_, kb.key) == GLFW_PRESS;

        if (key_pressed) {
            bool mods_ok = true;
            if (kb.modifiers & GLFW_MOD_CONTROL) {
                mods_ok &= (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                           glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
            }
            if (kb.modifiers & GLFW_MOD_SHIFT) {
                mods_ok &= (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                           glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
            }
            if (kb.modifiers & GLFW_MOD_ALT) {
                mods_ok &= (glfwGetKey(window_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                           glfwGetKey(window_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
            }

            if (mods_ok && !key_states[kb.key]) {
                key_states[kb.key] = true;
                if (kb.action) kb.action();
            }
        } else {
            // Key released - reset state so it can trigger again
            key_states[kb.key] = false;
        }
    }
}

void Application::BeginFrame() {
    // Handle deferred font rebuild (must happen before NewFrame)
    if (pending_font_rebuild_) {
        RebuildFonts();
        pending_font_rebuild_ = false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Application::EndFrame() {
    ImGui::Render();

    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_context);
    }

    glfwSwapBuffers(window_);
}

void Application::RenderDockspace() {
    // Check for async DMA connection completion
    if (dma_connecting_ && dma_connect_future_.valid()) {
        auto status = dma_connect_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            bool success = dma_connect_future_.get();
            dma_connecting_ = false;
            if (success) {
                LOG_INFO("DMA connected successfully");
                RefreshProcesses();
            } else {
                LOG_ERROR("DMA connection failed");
#ifdef PLATFORM_LINUX
                // On Linux, DMA failure is often due to USB permission issues
                // Scan sysfs for FTDI devices (common FPGA vendors)
                // Known FPGA device vendor IDs: 0403 (FTDI), 0d7d (Phison - some DMA cards)
                std::vector<std::pair<std::string, std::string>> ftdi_vendors = {
                    {"0403", "FTDI"},  // Most common for DMA cards
                };

                for (const auto& [vendor_id, vendor_name] : ftdi_vendors) {
                    // Check /sys/bus/usb/devices for this vendor
                    for (const auto& entry : std::filesystem::directory_iterator("/sys/bus/usb/devices")) {
                        std::filesystem::path id_vendor_path = entry.path() / "idVendor";
                        std::filesystem::path id_product_path = entry.path() / "idProduct";

                        if (std::filesystem::exists(id_vendor_path) && std::filesystem::exists(id_product_path)) {
                            std::ifstream vendor_file(id_vendor_path);
                            std::ifstream product_file(id_product_path);
                            std::string found_vendor, found_product;

                            if (vendor_file >> found_vendor && product_file >> found_product) {
                                if (found_vendor == vendor_id) {
                                    udev_vendor_id_ = found_vendor;
                                    udev_product_id_ = found_product;
                                    show_udev_dialog_ = true;
                                    LOG_INFO("Detected {} device {}:{}, offering udev rule install",
                                        vendor_name, found_vendor, found_product);
                                    break;
                                }
                            }
                        }
                    }
                    if (show_udev_dialog_) break;
                }
#endif
            }
        }
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar |
                                     ImGuiWindowFlags_NoDocking |
                                     ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

    if (first_frame_) {
        SetupDefaultLayout();
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    RenderMenuBar();
    ImGui::End();

    // Render panels
    if (panels_.process_list) RenderProcessList();
    if (panels_.module_list) RenderModuleList();
    if (panels_.memory_viewer) RenderMemoryViewer();
    if (panels_.disassembly) RenderDisassembly();
    #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (panels_.decompiler) RenderDecompiler();
#endif
    if (panels_.pattern_scanner) RenderPatternScanner();
    if (panels_.string_scanner) RenderStringScanner();
    if (panels_.memory_watcher) RenderMemoryWatcher();
    if (panels_.rtti_scanner) RenderRTTIScanner();
    if (panels_.bookmarks) RenderBookmarks();
    if (panels_.emulator) RenderEmulatorPanel();
    if (panels_.cs2_schema) RenderCS2Schema();
    if (panels_.cs2_entity_inspector) RenderCS2EntityInspector();
    if (panels_.cfg_viewer) RenderCFGViewer();
    if (panels_.cs2_radar) RenderCS2Radar();
    if (panels_.cs2_dashboard) RenderCS2Dashboard();
    if (panels_.pointer_chain) RenderPointerChain();
    if (panels_.memory_regions) RenderMemoryRegions();
    if (panels_.function_recovery) RenderFunctionRecovery();
    if (panels_.xref_finder) RenderXRefFinder();
    if (panels_.vtable_reader) RenderVTableReader();
    if (panels_.cache_manager) RenderCacheManager();
    if (panels_.task_manager) RenderTaskManager();
    if (panels_.console) RenderConsole();

    // Dialogs
    if (show_command_palette_) RenderCommandPalette();
    if (show_about_) RenderAboutDialog();
    if (show_goto_dialog_) RenderGotoDialog();
    if (show_dump_dialog_) RenderDumpDialog();
    if (show_settings_) RenderSettingsDialog();
    if (show_demo_) ImGui::ShowDemoWindow(&show_demo_);
#ifdef PLATFORM_LINUX
    if (show_udev_dialog_) RenderUdevPermissionDialog();
#endif

    RenderStatusBar();
}

void Application::RenderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (dma_connecting_) {
                ImGui::MenuItem("Connecting...", nullptr, false, false);
            } else if (ImGui::MenuItem("Connect DMA", "Ctrl+D", false, dma_ && !dma_->IsConnected())) {
                dma_connecting_ = true;
                LOG_INFO("Connecting to DMA device...");
                dma_connect_future_ = std::async(std::launch::async, [this]() {
                    return dma_->Initialize("fpga");
                });
            }
            if (ImGui::MenuItem("Disconnect", nullptr, false, dma_ && dma_->IsConnected() && !dma_connecting_)) {
                dma_->Close();
                cached_processes_.clear();
                cached_modules_.clear();
                LOG_INFO("DMA disconnected");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Dump Module...", nullptr, false, selected_module_base_ != 0)) {
                show_dump_dialog_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                RequestExit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Processes", "Ctrl+1", &panels_.process_list);
            ImGui::MenuItem("Modules", "Ctrl+2", &panels_.module_list);
            ImGui::MenuItem("Memory", "Ctrl+3", &panels_.memory_viewer);
            ImGui::MenuItem("Disassembly", "Ctrl+4", &panels_.disassembly);
            #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
            ImGui::MenuItem("Decompiler", "Ctrl+Shift+D", &panels_.decompiler);
#endif
            ImGui::MenuItem("CFG Viewer", "Ctrl+Shift+G", &panels_.cfg_viewer);
            ImGui::Separator();
            ImGui::MenuItem("Pattern Scanner", "Ctrl+5", &panels_.pattern_scanner);
            ImGui::MenuItem("String Scanner", "Ctrl+6", &panels_.string_scanner);
            ImGui::MenuItem("Memory Watcher", "Ctrl+7", &panels_.memory_watcher);
            ImGui::MenuItem("RTTI Scanner", "Ctrl+9", &panels_.rtti_scanner);
            ImGui::MenuItem("Bookmarks", "Ctrl+B", &panels_.bookmarks);
            ImGui::MenuItem("Emulator", "Ctrl+8", &panels_.emulator);
            ImGui::MenuItem("Pointer Chain", nullptr, &panels_.pointer_chain);
            ImGui::MenuItem("Memory Regions", nullptr, &panels_.memory_regions);
            ImGui::MenuItem("Function Recovery", "Ctrl+Shift+F", &panels_.function_recovery);
            ImGui::MenuItem("XRef Finder", "Ctrl+X", &panels_.xref_finder);
            ImGui::MenuItem("VTable Reader", "Ctrl+V", &panels_.vtable_reader);
            ImGui::Separator();
            ImGui::MenuItem("Cache Manager", nullptr, &panels_.cache_manager);
            ImGui::MenuItem("Task Manager", nullptr, &panels_.task_manager);
            ImGui::Separator();
            if (ImGui::BeginMenu("Game Tools")) {
                ImGui::MenuItem("CS2 Schema Dumper", "Ctrl+Shift+C", &panels_.cs2_schema);
                ImGui::MenuItem("CS2 Entity Inspector", "Ctrl+Shift+E", &panels_.cs2_entity_inspector);
                ImGui::Separator();
                ImGui::MenuItem("CS2 Radar", "Ctrl+Shift+R", &panels_.cs2_radar);
                ImGui::MenuItem("CS2 Dashboard", "Ctrl+Shift+P", &panels_.cs2_dashboard);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem("Console", "Ctrl+`", &panels_.console);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                first_frame_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(is_fullscreen_ ? "Exit Fullscreen" : "Fullscreen", "F11")) {
                ToggleFullscreen();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Navigate")) {
            if (ImGui::MenuItem("Back", "Alt+Left", false, CanNavigateBack())) {
                NavigateBack();
            }
            if (ImGui::MenuItem("Forward", "Alt+Right", false, CanNavigateForward())) {
                NavigateForward();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Goto Address...", "Ctrl+G")) {
                show_goto_dialog_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh", "F5")) {
                RefreshProcesses();
                if (selected_pid_ != 0) RefreshModules();
            }
            // Show history count for debugging
            if (!address_history_.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("History: %d/%zu", history_index_ + 1, address_history_.size());
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            // MCP Server section
            bool mcp_running = mcp_server_ && mcp_server_->IsRunning();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "MCP Server");
            ImGui::Separator();

            if (mcp_running) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
                ImGui::Text("Status: Running");
                ImGui::PopStyleColor();

                if (ImGui::MenuItem("Stop MCP Server")) {
                    if (mcp_server_) {
                        mcp_server_->Stop();
                        LOG_INFO("MCP server stopped from menu");
                    }
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("Status: Stopped");
                ImGui::PopStyleColor();

                if (ImGui::MenuItem("Start MCP Server")) {
                    if (!mcp_config_) {
                        mcp_config_ = std::make_unique<mcp::MCPConfig>();
                        mcp::MCPServer::LoadConfig(*mcp_config_);
                    }
                    if (!mcp_server_) {
                        mcp_server_ = std::make_unique<mcp::MCPServer>(this);
                    }
                    if (mcp_config_->require_auth && mcp_config_->api_key.empty()) {
                        mcp_config_->api_key = mcp::MCPServer::GenerateApiKey();
                    }
                    if (mcp_server_->Start(*mcp_config_)) {
                        LOG_INFO("MCP server started from menu on port {}", mcp_config_->port);
                    }
                }
            }

            if (ImGui::MenuItem("MCP Settings...")) {
                show_settings_ = true;
            }

            ImGui::Separator();

            // Copy API key (if available)
            if (mcp_config_ && !mcp_config_->api_key.empty()) {
                if (ImGui::MenuItem("Copy API Key")) {
                    ImGui::SetClipboardText(mcp_config_->api_key.c_str());
                    LOG_INFO("API key copied to clipboard");
                }
            }

            ImGui::EndMenu();
        }

        // CS2 menu - only show when CS2 is selected
        if (IsCS2Process()) {
            if (ImGui::BeginMenu("CS2")) {
                // Status indicator
                if (cs2_auto_init_success_) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    ImGui::Text("Status: Initialized");
                    ImGui::PopStyleColor();
                } else if (cs2_auto_init_attempted_) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                    ImGui::Text("Status: Partial Init");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    ImGui::Text("Status: Not Initialized");
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();

                // Re-initialize button
                if (ImGui::MenuItem("Re-initialize CS2", nullptr, false, cs2_auto_init_attempted_)) {
                    cs2_schema_initialized_ = false;
                    cs2_entity_initialized_ = false;
                    cs2_auto_init_attempted_ = true;
                    cs2_auto_init_success_ = false;
                    radar_map_name_addr_ = 0;
                    InitializeCS2();
                }
                ImGui::Separator();

                // Quick panel access
                ImGui::TextDisabled("Panels:");
                ImGui::MenuItem("Radar", "Ctrl+Shift+R", &panels_.cs2_radar);
                ImGui::MenuItem("Dashboard", "Ctrl+Shift+P", &panels_.cs2_dashboard);
                ImGui::MenuItem("Schema Dumper", "Ctrl+Shift+C", &panels_.cs2_schema);
                ImGui::MenuItem("Entity Inspector", "Ctrl+Shift+E", &panels_.cs2_entity_inspector);

                // Open all CS2 panels
                ImGui::Separator();
                if (ImGui::MenuItem("Open All CS2 Panels")) {
                    panels_.cs2_radar = true;
                    panels_.cs2_dashboard = true;
                    panels_.cs2_schema = true;
                    panels_.cs2_entity_inspector = true;
                }

                // Map info
                if (!radar_detected_map_.empty()) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Current Map: %s", radar_detected_map_.c_str());
                }

                ImGui::EndMenu();
            }
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Settings", "Ctrl+,")) {
                show_settings_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Command Palette", "Ctrl+Shift+P")) {
                show_command_palette_ = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_);
            if (ImGui::MenuItem("About Orpheus")) {
                show_about_ = true;
            }
            ImGui::EndMenu();
        }

        // Right-aligned status
        float status_width = 200.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - status_width);

        if (dma_ && dma_->IsConnected()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
            std::string device = dma_->GetDeviceType();
            // If just "fpga" fallback, uppercase it; otherwise use as-is (e.g., "Enigma X1")
            if (device == "fpga") {
                device = "FPGA";
            }
            ImGui::Text("DMA: %s", device.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            ImGui::Text("DMA: Disconnected");
            ImGui::PopStyleColor();
        }

        ImGui::EndMenuBar();
    }
}

void Application::RenderStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = 22.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoDocking |
                              ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));

    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        // Left side - process/module info
        if (selected_pid_ != 0) {
            ImGui::Text("Process: %s (%u)", selected_process_name_.c_str(), selected_pid_);
            ImGui::SameLine(0, 20.0f);
        }
        if (!selected_module_name_.empty()) {
            ImGui::Text("Module: %s @ 0x%llX", selected_module_name_.c_str(), selected_module_base_);
        }

        // Right side - MCP status + messages
        float right_offset = 120.0f;
        if (!status_message_.empty()) {
            right_offset = 420.0f;
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - right_offset);

        // MCP server status indicator
        bool mcp_running = mcp_server_ && mcp_server_->IsRunning();
        if (mcp_running) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "MCP");
            if (ImGui::IsItemHovered()) {
                std::string bind_addr = mcp_config_ ? mcp_config_->bind_address : "127.0.0.1";
                uint16_t port = mcp_config_ ? mcp_config_->port : 8765;
                ImGui::SetTooltip("MCP Server running on %s:%d\nClick to open settings",
                    bind_addr.c_str(), port);
            }
            if (ImGui::IsItemClicked()) {
                show_settings_ = true;
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "MCP");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("MCP Server stopped\nClick to open settings");
            }
            if (ImGui::IsItemClicked()) {
                show_settings_ = true;
            }
        }

        if (!status_message_.empty()) {
            ImGui::SameLine(0, 20.0f);
            ImGui::Text("%s", status_message_.c_str());
        }

        // Version info on far right
        ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "v%s", orpheus::version::VERSION);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", orpheus::version::GetBuildInfo());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Application::RenderProcessList() {
    ImGui::Begin("Processes", &panels_.process_list);

    // Auto-refresh logic
    if (auto_refresh_enabled_ && dma_ && dma_->IsConnected()) {
        double current_time = glfwGetTime();
        if (current_time - last_process_refresh_ >= process_refresh_interval_) {
            RefreshProcesses();
            last_process_refresh_ = current_time;
        }
    }

    static char filter[256] = {};
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputTextWithHint("##filter", "Filter...", filter, sizeof(filter));
    ImGui::SameLine();

    // Auto-refresh toggle button
    if (auto_refresh_enabled_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    }
    if (ImGui::Button(auto_refresh_enabled_ ? "Live" : "Manual")) {
        auto_refresh_enabled_ = !auto_refresh_enabled_;
        if (auto_refresh_enabled_) {
            last_process_refresh_ = glfwGetTime();  // Reset timer on enable
        }
    }
    if (auto_refresh_enabled_) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle auto-refresh (currently: %.1fs interval)", process_refresh_interval_);
    }

    ImGui::Separator();

    if (ImGui::BeginTable("##ProcessTable", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {

        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 60.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Arch", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                process_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                process_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Build filtered list of indices
        std::string filter_str = filter;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        std::vector<size_t> filtered_indices;
        for (size_t i = 0; i < cached_processes_.size(); i++) {
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                std::string lower_name = cached_processes_[i].name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                if (lower_name.find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                }
            }
        }

        // Sort filtered indices
        auto& procs = cached_processes_;
        int sort_col = process_sort_column_;
        bool ascending = process_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&procs, sort_col, ascending](size_t a, size_t b) {
                const auto& pa = procs[a];
                const auto& pb = procs[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0: cmp = (pa.pid < pb.pid) ? -1 : (pa.pid > pb.pid) ? 1 : 0; break;
                    case 1: cmp = pa.name.compare(pb.name); break;
                    case 2: cmp = (pa.is_64bit == pb.is_64bit) ? 0 : (pa.is_64bit ? 1 : -1); break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Render sorted list
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& proc = cached_processes_[filtered_indices[row]];

                ImGui::TableNextRow();
                bool is_selected = (proc.pid == selected_pid_);

                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);
                if (ImGui::Selectable(std::to_string(proc.pid).c_str(), is_selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_pid_ = proc.pid;
                    selected_process_name_ = proc.name;

                    // Reset CS2 state on process change
                    cs2_auto_init_attempted_ = false;
                    cs2_auto_init_success_ = false;

                    RefreshModules();
                    LOG_INFO("Selected process: {} (PID: {})", proc.name, proc.pid);

                    // Auto-initialize CS2 systems if this is CS2
                    if (IsCS2Process()) {
                        cs2_auto_init_attempted_ = true;
                        InitializeCS2();
                    }
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::Text("%s", proc.name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(proc.is_64bit ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.5f, 1.0f),
                                  "%s", proc.is_64bit ? "x64" : "x86");
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void Application::RenderModuleList() {
    ImGui::Begin("Modules", &panels_.module_list);

    if (selected_pid_ != 0) {
        // Auto-refresh modules
        if (auto_refresh_enabled_ && dma_ && dma_->IsConnected()) {
            double current_time = glfwGetTime();
            if (current_time - last_module_refresh_ >= module_refresh_interval_) {
                RefreshModules();
                last_module_refresh_ = current_time;
            }
        }

        ImGui::Text("Process: %s (%zu modules)", selected_process_name_.c_str(), cached_modules_.size());

        // Search filter
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##modfilter", "Filter modules...", module_filter_, sizeof(module_filter_));
        ImGui::Separator();

        if (ImGui::BeginTable("##ModuleTable", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {

            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
            ImGui::TableSetupColumn("Base", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Handle sorting
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                    module_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                    module_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                    sort_specs->SpecsDirty = false;
                }
            }

            // Build filtered list
            std::string filter_str = module_filter_;
            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

            std::vector<size_t> filtered_indices;
            for (size_t i = 0; i < cached_modules_.size(); i++) {
                if (filter_str.empty()) {
                    filtered_indices.push_back(i);
                } else {
                    std::string lower_name = cached_modules_[i].name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    if (lower_name.find(filter_str) != std::string::npos) {
                        filtered_indices.push_back(i);
                    }
                }
            }

            // Sort filtered indices
            auto& mods = cached_modules_;
            int sort_col = module_sort_column_;
            bool ascending = module_sort_ascending_;
            std::sort(filtered_indices.begin(), filtered_indices.end(),
                [&mods, sort_col, ascending](size_t a, size_t b) {
                    const auto& ma = mods[a];
                    const auto& mb = mods[b];
                    int cmp = 0;
                    switch (sort_col) {
                        case 0: cmp = ma.name.compare(mb.name); break;
                        case 1: cmp = (ma.base_address < mb.base_address) ? -1 : (ma.base_address > mb.base_address) ? 1 : 0; break;
                        case 2: cmp = (ma.size < mb.size) ? -1 : (ma.size > mb.size) ? 1 : 0; break;
                    }
                    return ascending ? (cmp < 0) : (cmp > 0);
                });

            // Render sorted list with clipper
            ImGuiListClipper clipper;
            clipper.Begin((int)filtered_indices.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const auto& mod = cached_modules_[filtered_indices[row]];

                    ImGui::TableNextRow();
                    bool is_selected = (mod.name == selected_module_name_);

                    ImGui::TableNextColumn();
                    ImGui::PushID((int)filtered_indices[row]);
                    if (ImGui::Selectable(mod.name.c_str(), is_selected,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                        selected_module_name_ = mod.name;
                        selected_module_base_ = mod.base_address;
                        selected_module_size_ = mod.size;
                        NavigateToAddress(mod.base_address);
                        LOG_INFO("Selected module: {} @ 0x{:X}", mod.name, mod.base_address);
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(mod.base_address);
                    }

                    // Context menu for module
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("View in Memory")) {
                            memory_address_ = mod.base_address;
                            snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)mod.base_address);
                            memory_data_ = dma_->ReadMemory(selected_pid_, mod.base_address, 512);
                            panels_.memory_viewer = true;
                        }
                        if (ImGui::MenuItem("View in Disassembly")) {
                            disasm_address_ = mod.base_address;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)mod.base_address);
                            auto data = dma_->ReadMemory(selected_pid_, mod.base_address, 4096);
                            if (!data.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(data, mod.base_address);
                            }
                            panels_.disassembly = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Scan RTTI")) {
                            selected_module_name_ = mod.name;
                            selected_module_base_ = mod.base_address;
                            selected_module_size_ = mod.size;
                            panels_.rtti_scanner = true;
                            rtti_scanning_ = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Dump Module...")) {
                            selected_module_name_ = mod.name;
                            selected_module_base_ = mod.base_address;
                            selected_module_size_ = mod.size;
                            show_dump_dialog_ = true;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    ImGui::Text("0x%llX", (unsigned long long)mod.base_address);

                    ImGui::TableNextColumn();
                    ImGui::Text("0x%X", mod.size);
                }
            }

            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Select a process to view modules");
    }

    ImGui::End();
}

void Application::RenderMemoryViewer() {
    ImGui::Begin("Memory", &panels_.memory_viewer);

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected()) {
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("Address", address_input_, sizeof(address_input_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            memory_address_ = strtoull(address_input_, nullptr, 16);
            memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 256);
        }

        ImGui::SameLine();
        if (ImGui::Button("Read")) {
            memory_address_ = strtoull(address_input_, nullptr, 16);
            memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 512);
        }

        ImGui::SameLine();
        ImGui::Checkbox("ASCII", &show_ascii_);

        ImGui::Separator();

        if (!memory_data_.empty()) {
            if (font_mono_) ImGui::PushFont(font_mono_);

            // Fixed row height for virtualization
            const float row_height = ImGui::GetTextLineHeightWithSpacing();

            ImGui::BeginChild("##HexView", ImVec2(0, 0), true);

            ImGuiListClipper clipper;
            int total_rows = (int)(memory_data_.size() + bytes_per_row_ - 1) / bytes_per_row_;
            clipper.Begin(total_rows, row_height);

            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    size_t offset = row * bytes_per_row_;
                    uint64_t addr = memory_address_ + offset;

                    // Build entire line in a single buffer for cleaner rendering
                    // Format: ADDRESS  HH HH HH HH HH HH HH HH  HH HH HH HH HH HH HH HH  |ASCII...........|
                    char line_buf[256];
                    int pos = 0;

                    // Address
                    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%016llX  ", (unsigned long long)addr);

                    // Hex bytes with grouping
                    for (int col = 0; col < bytes_per_row_; col++) {
                        size_t idx = offset + col;
                        if (idx < memory_data_.size()) {
                            pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%02X ", memory_data_[idx]);
                        } else {
                            pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "   ");
                        }
                        // Add extra space at midpoint for visual grouping
                        if (col == 7) {
                            line_buf[pos++] = ' ';
                        }
                    }

                    // ASCII section with separator
                    if (show_ascii_) {
                        line_buf[pos++] = ' ';
                        line_buf[pos++] = '|';
                        for (int col = 0; col < bytes_per_row_; col++) {
                            size_t idx = offset + col;
                            if (idx < memory_data_.size()) {
                                char c = static_cast<char>(memory_data_[idx]);
                                line_buf[pos++] = (c >= 32 && c < 127) ? c : '.';
                            } else {
                                line_buf[pos++] = ' ';
                            }
                        }
                        line_buf[pos++] = '|';
                    }
                    line_buf[pos] = '\0';

                    // Color the address portion differently
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImGui::TextUnformatted(line_buf, line_buf + 18);  // Address (16 chars + 2 spaces)
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextUnformatted(line_buf + 18);  // Rest of the line
                }
            }

            ImGui::EndChild();
            if (font_mono_) ImGui::PopFont();
        } else {
            ImGui::TextDisabled("Enter an address and click Read");
        }
    } else {
        ImGui::TextDisabled("Select a process to view memory");
    }

    ImGui::End();
}

void Application::RenderDisassembly() {
    ImGui::Begin("Disassembly", &panels_.disassembly);

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected() && disassembler_) {
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("Address", disasm_address_input_, sizeof(disasm_address_input_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            disasm_address_ = strtoull(disasm_address_input_, nullptr, 16);
            auto code = dma_->ReadMemory(selected_pid_, disasm_address_, 1024);
            if (!code.empty()) {
                disasm_instructions_ = disassembler_->Disassemble(code, disasm_address_);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Disassemble")) {
            disasm_address_ = strtoull(disasm_address_input_, nullptr, 16);
            auto code = dma_->ReadMemory(selected_pid_, disasm_address_, 1024);
            if (!code.empty()) {
                disasm_instructions_ = disassembler_->Disassemble(code, disasm_address_);
            }
        }

        ImGui::Separator();

        if (font_mono_) ImGui::PushFont(font_mono_);

        // Fixed row height for virtualization
        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        ImGui::BeginChild("##DisasmView", ImVec2(0, 0), true);

        ImGuiListClipper clipper;
        clipper.Begin((int)disasm_instructions_.size(), row_height);

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const auto& instr = disasm_instructions_[i];

                ImGui::PushID(i);

                // Make the entire row selectable for context menu
                char row_label[128];
                snprintf(row_label, sizeof(row_label), "%016llX##row", (unsigned long long)instr.address);

                // Calculate approximate row width
                ImGui::BeginGroup();

                // Address
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%016llX", (unsigned long long)instr.address);
                ImGui::SameLine();

                // Bytes - use stack buffer for speed
                char bytes_buf[32];
                int pos = 0;
                for (size_t j = 0; j < instr.bytes.size() && j < 8; j++) {
                    pos += snprintf(bytes_buf + pos, sizeof(bytes_buf) - pos, "%02X ", instr.bytes[j]);
                }
                // Pad to fixed width
                while (pos < 24) bytes_buf[pos++] = ' ';
                bytes_buf[24] = '\0';

                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", bytes_buf);
                ImGui::SameLine();

                // Instruction with color coding
                ImVec4 instr_color(0.9f, 0.9f, 0.9f, 1.0f);
                if (instr.is_call) instr_color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                else if (instr.is_jump) instr_color = ImVec4(1.0f, 0.8f, 0.5f, 1.0f);
                else if (instr.is_ret) instr_color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);

                ImGui::TextColored(instr_color, "%s", instr.full_text.c_str());

                ImGui::EndGroup();

                // Right-click context menu on the row
                if (ImGui::BeginPopupContextItem("##disasm_ctx")) {
                    char addr_buf[32];
                    snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)instr.address);

                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    if (ImGui::MenuItem("View in Memory")) {
                        memory_address_ = instr.address;
                        snprintf(address_input_, sizeof(address_input_), "%llX", (unsigned long long)instr.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, instr.address, 256);
                        panels_.memory_viewer = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Generate Signature")) {
                        signature_address_ = instr.address;

                        // Read bytes starting from this address
                        auto sig_data = dma_->ReadMemory(selected_pid_, instr.address, 128);
                        if (!sig_data.empty()) {
                            analysis::SignatureGenerator generator;
                            analysis::SignatureOptions options;
                            options.wildcard_rip_relative = true;
                            options.wildcard_calls = true;
                            options.wildcard_jumps = true;
                            options.wildcard_large_immediates = true;
                            options.min_unique_bytes = 8;
                            options.max_length = 64;

                            auto sig = generator.Generate(sig_data, instr.address, options);

                            generated_signature_ = sig.pattern;
                            generated_signature_ida_ = analysis::SignatureGenerator::FormatIDA(sig);
                            generated_signature_ce_ = analysis::SignatureGenerator::FormatCE(sig);
                            generated_signature_mask_ = sig.pattern_mask;
                            generated_signature_length_ = static_cast<int>(sig.length);
                            generated_signature_unique_ = static_cast<int>(sig.unique_bytes);
                            generated_signature_ratio_ = sig.uniqueness_ratio;
                            generated_signature_valid_ = sig.is_valid;

                            show_signature_popup_ = true;
                        }
                    }
                    if (ImGui::MenuItem("Find XRefs to this address")) {
                        // Pre-fill XRef finder with this address
                        snprintf(xref_target_input_, sizeof(xref_target_input_), "%llX", (unsigned long long)instr.address);
                        panels_.xref_finder = true;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        if (font_mono_) ImGui::PopFont();
    } else {
        ImGui::TextDisabled("Select a process to disassemble");
    }

    ImGui::End();

    // Signature Generator popup dialog
    if (show_signature_popup_) {
        ImGui::OpenPopup("Signature Generator");
        show_signature_popup_ = false;  // Reset flag after opening
    }

    if (ImGui::BeginPopupModal("Signature Generator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Address header
        ImGui::Text("Address: 0x%llX", (unsigned long long)signature_address_);
        ImGui::Separator();

        // Validity indicator
        if (generated_signature_valid_) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Valid signature");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning: May not be unique enough");
        }
        ImGui::Separator();

        // Metrics
        ImGui::Text("Length: %d bytes | Unique: %d bytes | Ratio: %.1f%%",
                    generated_signature_length_, generated_signature_unique_,
                    generated_signature_ratio_ * 100.0f);
        ImGui::Separator();

        // IDA Pattern
        ImGui::Text("IDA Pattern:");
        if (font_mono_) ImGui::PushFont(font_mono_);
        ImGui::InputText("##ida_pattern", &generated_signature_ida_[0], generated_signature_ida_.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);
        if (font_mono_) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button("Copy##ida")) {
            ImGui::SetClipboardText(generated_signature_ida_.c_str());
        }

        // CE Pattern
        ImGui::Text("CE Pattern:");
        if (font_mono_) ImGui::PushFont(font_mono_);
        ImGui::InputText("##ce_pattern", &generated_signature_ce_[0], generated_signature_ce_.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);
        if (font_mono_) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button("Copy##ce")) {
            ImGui::SetClipboardText(generated_signature_ce_.c_str());
        }

        // Mask
        ImGui::Text("Mask:");
        if (font_mono_) ImGui::PushFont(font_mono_);
        ImGui::InputText("##mask", &generated_signature_mask_[0], generated_signature_mask_.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);
        if (font_mono_) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button("Copy##mask")) {
            ImGui::SetClipboardText(generated_signature_mask_.c_str());
        }

        ImGui::Separator();

        // Close button
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::RenderDecompiler() {
    ImGui::Begin("Decompiler", &panels_.decompiler);

#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    // Initialize decompiler on first use
    if (!decompiler_initialized_ && !decompiler_) {
        decompiler_ = std::make_unique<Decompiler>();
        DecompilerConfig config;

        // Get SLEIGH specs from RuntimeManager (extracted to AppData)
        auto sleigh_dir = RuntimeManager::Instance().GetSleighDirectory();
        if (!sleigh_dir.empty() && std::filesystem::exists(sleigh_dir)) {
            config.sleigh_spec_path = sleigh_dir.string();
        } else {
            LOG_WARN("SLEIGH directory not found, decompiler may not work");
            config.sleigh_spec_path = "";
        }
        LOG_INFO("SLEIGH specs path: {}", config.sleigh_spec_path);

        config.processor = "x86";
        config.address_size = 64;
        config.little_endian = true;
        config.compiler_spec = "windows";

        if (decompiler_->Initialize(config)) {
            decompiler_initialized_ = true;
            LOG_INFO("Ghidra decompiler initialized");
        } else {
            LOG_ERROR("Failed to initialize decompiler: {}", decompiler_->GetLastError());
        }
    }

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected()) {
        // Address input
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("Address", decompile_address_input_, sizeof(decompile_address_input_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            decompile_address_ = strtoull(decompile_address_input_, nullptr, 16);
        }

        ImGui::SameLine();
        bool has_address_input = strlen(decompile_address_input_) > 0;
        bool can_decompile = decompiler_initialized_ && has_address_input;
        if (!can_decompile) ImGui::BeginDisabled();
        if (ImGui::Button("Decompile")) {
            // Parse address from input when button is clicked
            decompile_address_ = strtoull(decompile_address_input_, nullptr, 16);
            if (decompile_address_ == 0) {
                decompiled_code_ = "// Error: Invalid address (enter a hex address like 7FF600001000)";
            } else if (decompiler_) {
                // Set up DMA callback for memory reading
                decompiler_->SetMemoryCallback([this](uint64_t addr, size_t size, uint8_t* buffer) -> bool {
                    auto data = dma_->ReadMemory(selected_pid_, addr, static_cast<uint32_t>(size));
                    if (data.size() >= size) {
                        memcpy(buffer, data.data(), size);
                        return true;
                    }
                    return false;
                });

                // Decompile the function
                auto result = decompiler_->DecompileFunction(decompile_address_);
                if (result.success) {
                    decompiled_code_ = result.c_code;
                    LOG_INFO("Decompiled function at 0x{:X}", decompile_address_);
                } else {
                    decompiled_code_ = "// Decompilation failed: " + result.error;
                    LOG_WARN("Decompilation failed: {}", result.error);
                }
            }
        }
        if (!can_decompile) ImGui::EndDisabled();

        if (!decompiler_initialized_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(Decompiler not initialized)");
        }

        ImGui::Separator();

        // Output area with syntax highlighting (basic C style)
        if (font_mono_) ImGui::PushFont(font_mono_);

        ImGui::BeginChild("##DecompilerOutput", ImVec2(0, 0), true,
            ImGuiWindowFlags_HorizontalScrollbar);

        if (!decompiled_code_.empty()) {
            // Simple syntax coloring - highlight keywords
            ImGui::TextUnformatted(decompiled_code_.c_str());
        } else {
            ImGui::TextDisabled("Enter an address and click Decompile");
        }

        ImGui::EndChild();
        if (font_mono_) ImGui::PopFont();
    } else {
        ImGui::TextDisabled("Select a process to decompile");
    }
#else
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
        "Decompiler not available");
    ImGui::TextDisabled("Build with -DORPHEUS_BUILD_DECOMPILER=ON to enable");
#endif

    ImGui::End();
}

void Application::RenderPatternScanner() {
    ImGui::Begin("Pattern Scanner", &panels_.pattern_scanner);

    // Check for async scan completion
    if (pattern_scanning_ && pattern_scan_future_.valid()) {
        auto status = pattern_scan_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            try {
                pattern_results_ = pattern_scan_future_.get();
                pattern_scan_error_.clear();
                LOG_INFO("Pattern scan found {} results", pattern_results_.size());
            } catch (const std::exception& e) {
                pattern_scan_error_ = e.what();
                pattern_results_.clear();
                LOG_ERROR("Pattern scan failed: {}", e.what());
            }
            pattern_scanning_ = false;
            pattern_scan_progress_ = 1.0f;
            pattern_scan_progress_stage_ = "Complete";
        }
    }

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected()) {
        // Module selection header
        if (selected_module_base_ != 0) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", selected_module_name_.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("@ 0x%llX (0x%X bytes)", (unsigned long long)selected_module_base_, selected_module_size_);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No module selected");
            ImGui::SameLine();
            ImGui::TextDisabled("- Select a module in the Modules panel");
        }
        ImGui::Separator();

        ImGui::SetNextItemWidth(-80.0f);
        ImGui::InputTextWithHint("##pattern", "48 8B 05 ?? ?? ?? ??", pattern_input_, sizeof(pattern_input_));
        ImGui::SameLine();

        bool can_scan = selected_module_base_ != 0 && !pattern_scanning_;
        if (!can_scan) ImGui::BeginDisabled();
        if (ImGui::Button("Scan")) {
            // Compile pattern on main thread for fast validation
            auto compiled_pattern = analysis::PatternScanner::Compile(pattern_input_);
            if (compiled_pattern) {
                pattern_scanning_ = true;
                pattern_scan_cancel_requested_ = false;
                pattern_results_.clear();
                pattern_scan_error_.clear();
                pattern_scan_progress_ = 0.0f;
                pattern_scan_progress_stage_ = "Starting...";

                // Capture state for async lambda
                uint32_t pid = selected_pid_;
                uint64_t module_base = selected_module_base_;
                uint32_t module_size = selected_module_size_;
                analysis::Pattern pattern = *compiled_pattern;
                auto* dma = dma_.get();

                pattern_scan_future_ = std::async(std::launch::async,
                    [pid, module_base, module_size, pattern, dma,
                     &cancel_flag = pattern_scan_cancel_requested_,
                     &progress_stage = pattern_scan_progress_stage_,
                     &progress = pattern_scan_progress_]() -> std::vector<uint64_t> {

                    std::vector<uint64_t> results;

                    // Chunk configuration
                    constexpr size_t CHUNK_SIZE = 2 * 1024 * 1024;  // 2MB chunks
                    const size_t pattern_len = pattern.bytes.size();
                    const size_t overlap_size = pattern_len > 0 ? pattern_len - 1 : 0;

                    // Calculate total chunks
                    size_t total_chunks = (module_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
                    if (total_chunks == 0) total_chunks = 1;

                    // Buffer to hold overlap bytes from previous chunk
                    std::vector<uint8_t> overlap_buffer;
                    overlap_buffer.reserve(overlap_size);

                    size_t bytes_processed = 0;
                    size_t chunk_index = 0;

                    while (bytes_processed < module_size) {
                        // Check for cancellation
                        if (cancel_flag.load()) {
                            progress_stage = "Cancelled";
                            return results;
                        }

                        // Calculate chunk parameters
                        size_t chunk_offset = bytes_processed;
                        size_t remaining = module_size - bytes_processed;
                        size_t chunk_size = std::min(CHUNK_SIZE, remaining);

                        // Update progress - reading phase
                        chunk_index++;
                        progress_stage = "Reading chunk " + std::to_string(chunk_index) + "/" + std::to_string(total_chunks) + "...";
                        progress = static_cast<float>(chunk_index - 1) / static_cast<float>(total_chunks * 2);

                        // Read chunk from memory
                        auto chunk_data = dma->ReadMemory(pid, module_base + chunk_offset, chunk_size);
                        if (chunk_data.empty()) {
                            // Skip unreadable chunks but continue
                            bytes_processed += chunk_size;
                            overlap_buffer.clear();
                            continue;
                        }

                        // Check for cancellation after read
                        if (cancel_flag.load()) {
                            progress_stage = "Cancelled";
                            return results;
                        }

                        // Update progress - scanning phase
                        progress_stage = "Scanning chunk " + std::to_string(chunk_index) + "/" + std::to_string(total_chunks) + "...";
                        progress = static_cast<float>(chunk_index * 2 - 1) / static_cast<float>(total_chunks * 2);

                        // Handle boundary overlap - scan across chunk boundary
                        if (!overlap_buffer.empty() && overlap_buffer.size() == overlap_size) {
                            // Create combined buffer: last (overlap_size) bytes of previous chunk + first (overlap_size) bytes of current chunk
                            std::vector<uint8_t> boundary_buffer;
                            boundary_buffer.reserve(overlap_size * 2);
                            boundary_buffer.insert(boundary_buffer.end(), overlap_buffer.begin(), overlap_buffer.end());
                            size_t bytes_to_add = std::min(overlap_size, chunk_data.size());
                            boundary_buffer.insert(boundary_buffer.end(), chunk_data.begin(), chunk_data.begin() + bytes_to_add);

                            // Scan boundary region - base address is at the start of overlap_buffer
                            uint64_t boundary_base = module_base + chunk_offset - overlap_size;
                            auto boundary_matches = analysis::PatternScanner::Scan(boundary_buffer, pattern, boundary_base);

                            // Only keep matches that actually span the boundary
                            for (uint64_t match_addr : boundary_matches) {
                                // Match spans boundary if it starts in overlap region but extends into current chunk
                                uint64_t match_end = match_addr + pattern_len;
                                uint64_t chunk_start_addr = module_base + chunk_offset;
                                if (match_addr < chunk_start_addr && match_end > chunk_start_addr) {
                                    results.push_back(match_addr);
                                }
                            }
                        }

                        // Scan main chunk
                        uint64_t chunk_base = module_base + chunk_offset;
                        auto chunk_matches = analysis::PatternScanner::Scan(chunk_data, pattern, chunk_base);
                        results.insert(results.end(), chunk_matches.begin(), chunk_matches.end());

                        // Save last (overlap_size) bytes for next iteration
                        overlap_buffer.clear();
                        if (chunk_data.size() >= overlap_size) {
                            overlap_buffer.insert(overlap_buffer.end(),
                                chunk_data.end() - overlap_size, chunk_data.end());
                        } else {
                            overlap_buffer = chunk_data;  // Chunk smaller than overlap, keep all
                        }

                        bytes_processed += chunk_size;
                    }

                    // Sort results by address and remove duplicates
                    std::sort(results.begin(), results.end());
                    results.erase(std::unique(results.begin(), results.end()), results.end());

                    progress_stage = "Complete";
                    progress = 1.0f;
                    return results;
                });
            } else {
                pattern_scan_error_ = "Invalid pattern syntax";
            }
        }
        if (!can_scan) ImGui::EndDisabled();

        // Cancel button (visible during scan)
        if (pattern_scanning_) {
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                pattern_scan_cancel_requested_ = true;
            }
        }

        // Progress indicator (visible during scan)
        if (pattern_scanning_) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", pattern_scan_progress_stage_.c_str());
            ImGui::ProgressBar(pattern_scan_progress_, ImVec2(-1, 0));
        }

        // Error display
        if (!pattern_scan_error_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", pattern_scan_error_.c_str());
        }

        ImGui::Separator();

        // Results header
        ImGui::Text("Results: %zu", pattern_results_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            pattern_results_.clear();
            pattern_scan_error_.clear();
        }

        ImGui::BeginChild("##PatternResults", ImVec2(0, 0), true);
        for (size_t i = 0; i < pattern_results_.size(); i++) {
            uint64_t addr = pattern_results_[i];
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << addr;

            ImGui::PushID(static_cast<int>(i));

            // Selectable row - double-click navigates to both views
            if (ImGui::Selectable(ss.str().c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    NavigateToAddress(addr);
                }
            }

            // Context menu on right-click
            if (ImGui::BeginPopupContextItem("##ResultContext")) {
                if (ImGui::MenuItem("View in Memory")) {
                    memory_address_ = addr;
                    snprintf(address_input_, sizeof(address_input_), "0x%llX", addr);
                    memory_data_ = dma_->ReadMemory(selected_pid_, addr, 256);
                    panels_.memory_viewer = true;
                }
                if (ImGui::MenuItem("View in Disassembly")) {
                    disasm_address_ = addr;
                    snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", addr);
                    if (disassembler_) {
                        auto data = dma_->ReadMemory(selected_pid_, addr, 512);
                        if (!data.empty()) {
                            disasm_instructions_ = disassembler_->Disassemble(data, addr);
                        }
                    }
                    panels_.disassembly = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy Address")) {
                    ImGui::SetClipboardText(ss.str().c_str());
                }
                ImGui::EndPopup();
            }

            // Tooltip with module offset
            if (ImGui::IsItemHovered()) {
                uint64_t offset = addr - selected_module_base_;
                ImGui::SetTooltip("%s+0x%llX\nRight-click for options", selected_module_name_.c_str(), offset);
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Select a process and module to scan");
    }

    ImGui::End();
}

void Application::RenderStringScanner() {
    ImGui::Begin("String Scanner", &panels_.string_scanner);

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected()) {
        // Module selection header
        if (selected_module_base_ != 0) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", selected_module_name_.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("@ 0x%llX (0x%X bytes)", (unsigned long long)selected_module_base_, selected_module_size_);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No module selected");
            ImGui::SameLine();
            ImGui::TextDisabled("- Select a module in the Modules panel");
        }
        ImGui::Separator();

        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("Min Length", &string_min_length_);
        ImGui::SameLine();
        ImGui::Checkbox("ASCII", &scan_ascii_);
        ImGui::SameLine();
        ImGui::Checkbox("Unicode", &scan_unicode_);
        ImGui::SameLine();

        bool can_scan = selected_module_base_ != 0 && !string_scanning_;
        if (!can_scan) ImGui::BeginDisabled();
        if (ImGui::Button("Scan")) {
            string_scanning_ = true;
            string_results_.clear();

            analysis::StringScanOptions opts;
            opts.min_length = string_min_length_;
            opts.scan_ascii = scan_ascii_;
            opts.scan_utf16 = scan_unicode_;

            auto data = dma_->ReadMemory(selected_pid_, selected_module_base_, selected_module_size_);
            if (!data.empty()) {
                string_results_ = analysis::StringScanner::Scan(data, opts, selected_module_base_);
                LOG_INFO("String scan found {} results", string_results_.size());
            }
            string_scanning_ = false;
        }
        if (!can_scan) ImGui::EndDisabled();

        ImGui::Separator();

        // Results header
        ImGui::Text("Results: %zu", string_results_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            string_results_.clear();
        }
        ImGui::Separator();

        // Use fixed row height for proper virtualization
        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        if (ImGui::BeginTable("##StringTable", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {

            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("String", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin((int)string_results_.size(), row_height);

            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const auto& str = string_results_[i];

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                    ImGui::TableNextColumn();

                    ImGui::PushID(i);

                    // Format address
                    char addr_buf[32];
                    snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)str.address);

                    if (ImGui::Selectable(addr_buf, false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            NavigateToAddress(str.address);
                        }
                    }

                    // Context menu on right-click
                    if (ImGui::BeginPopupContextItem("##StringContext")) {
                        if (ImGui::MenuItem("View in Memory")) {
                            memory_address_ = str.address;
                            snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)str.address);
                            memory_data_ = dma_->ReadMemory(selected_pid_, str.address, 256);
                            panels_.memory_viewer = true;
                        }
                        if (ImGui::MenuItem("View in Disassembly")) {
                            disasm_address_ = str.address;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)str.address);
                            if (disassembler_) {
                                auto data = dma_->ReadMemory(selected_pid_, str.address, 512);
                                if (!data.empty()) {
                                    disasm_instructions_ = disassembler_->Disassemble(data, str.address);
                                }
                            }
                            panels_.disassembly = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Copy Address")) {
                            ImGui::SetClipboardText(addr_buf);
                        }
                        if (ImGui::MenuItem("Copy String")) {
                            ImGui::SetClipboardText(str.value.c_str());
                        }
                        ImGui::EndPopup();
                    }

                    // Tooltip with full string (for long strings)
                    if (ImGui::IsItemHovered()) {
                        uint64_t offset = str.address - selected_module_base_;
                        if (str.value.length() > 80) {
                            ImGui::SetTooltip("%s+0x%llX\n\n%.500s%s\n\nRight-click for options",
                                selected_module_name_.c_str(), (unsigned long long)offset,
                                str.value.c_str(),
                                str.value.length() > 500 ? "..." : "");
                        } else {
                            ImGui::SetTooltip("%s+0x%llX\nRight-click for options",
                                selected_module_name_.c_str(), (unsigned long long)offset);
                        }
                    }

                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    const char* type_str = str.type == analysis::StringType::ASCII ? "ASCII" :
                                          str.type == analysis::StringType::UTF16_LE ? "UTF16" : "UTF8";
                    ImGui::Text("%s", type_str);

                    ImGui::TableNextColumn();
                    // Truncate long strings for display (full string in tooltip)
                    if (str.value.length() > 100) {
                        ImGui::Text("%.100s...", str.value.c_str());
                    } else {
                        ImGui::Text("%s", str.value.c_str());
                    }
                }
            }

            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Select a process and module to scan");
    }

    ImGui::End();
}

void Application::RenderMemoryWatcher() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory Watcher", &panels_.memory_watcher);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a process first");
        ImGui::End();
        return;
    }

    // Initialize or recreate memory watcher for current process
    if (!memory_watcher_ || watcher_pid_ != selected_pid_) {
        if (memory_watcher_) {
            memory_watcher_->StopAutoScan();
        }
        memory_watcher_ = std::make_unique<analysis::MemoryWatcher>(
            [this](uint64_t addr, size_t size) -> std::vector<uint8_t> {
                return dma_->ReadMemory(selected_pid_, addr, size);
            }
        );
        watcher_pid_ = selected_pid_;
        LOG_INFO("Memory watcher initialized for PID {}", selected_pid_);
    }

    // Status header
    ImGui::BeginChild("WatcherHeader", ImVec2(0, 80), true);
    {
        bool is_scanning = memory_watcher_->IsScanning();
        if (is_scanning) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "SCANNING");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "STOPPED");
        }
        ImGui::SameLine(100);
        ImGui::Text("Process: %s (PID: %u)", selected_process_name_.c_str(), selected_pid_);
        ImGui::SameLine(400);
        ImGui::Text("Total Changes: %zu", memory_watcher_->GetTotalChangeCount());

        ImGui::Spacing();

        // Scan controls
        if (is_scanning) {
            if (ImGui::Button("Stop Auto-Scan", ImVec2(120, 0))) {
                memory_watcher_->StopAutoScan();
            }
        } else {
            if (ImGui::Button("Start Auto-Scan", ImVec2(120, 0))) {
                memory_watcher_->StartAutoScan(watch_scan_interval_);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Single Scan", ImVec2(100, 0))) {
            memory_watcher_->Scan();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("##Interval", &watch_scan_interval_, 10, 100)) {
            watch_scan_interval_ = std::max(10, std::min(10000, watch_scan_interval_));
        }
        ImGui::SameLine();
        ImGui::Text("ms");
        ImGui::SameLine(500);
        if (ImGui::Button("Clear History", ImVec2(100, 0))) {
            memory_watcher_->ClearHistory();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Two-column layout: Watches on left, History on right
    float panel_width = ImGui::GetContentRegionAvail().x;

    // Left panel: Watch list and Add form
    ImGui::BeginChild("WatchesPanel", ImVec2(panel_width * 0.5f - 5, 0), true);
    {
        // Add Watch form
        ImGui::Text("Add Watch");
        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Address##WatchAddr", watch_addr_input_, sizeof(watch_addr_input_),
                         ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputText("Size##WatchSize", watch_size_input_, sizeof(watch_size_input_),
                         ImGuiInputTextFlags_CharsDecimal);

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("Name##WatchName", watch_name_input_, sizeof(watch_name_input_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        const char* watch_types[] = { "Read", "Write", "ReadWrite", "Value" };
        ImGui::Combo("Type##WatchType", &watch_type_index_, watch_types, IM_ARRAYSIZE(watch_types));

        if (ImGui::Button("Add Watch", ImVec2(100, 0))) {
            uint64_t addr = 0;
            if (strlen(watch_addr_input_) > 0) {
                addr = strtoull(watch_addr_input_, nullptr, 16);
            }
            size_t size = static_cast<size_t>(atoi(watch_size_input_));
            if (addr != 0 && size > 0 && size <= 1024) {
                analysis::WatchType type = static_cast<analysis::WatchType>(watch_type_index_);
                std::string name = strlen(watch_name_input_) > 0 ? watch_name_input_ : "";
                memory_watcher_->AddWatch(addr, size, type, name);
                watch_addr_input_[0] = '\0';
                watch_name_input_[0] = '\0';
                LOG_INFO("Added watch at 0x{:X} ({} bytes)", addr, size);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All", ImVec2(80, 0))) {
            memory_watcher_->ClearAllWatches();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Watch list
        auto watches = memory_watcher_->GetWatches();
        ImGui::Text("Watches (%zu)", watches.size());
        ImGui::Separator();

        if (ImGui::BeginTable("WatchTable", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, 0))) {
            ImGui::TableSetupColumn("En", ImGuiTableColumnFlags_WidthFixed, 25);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Changes", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();

            // We need watch IDs to manage them, but GetWatches doesn't return IDs
            // For now, we'll iterate and use index+1 as a proxy
            // TODO: Improve MemoryWatcher API to return IDs with regions
            uint32_t watch_idx = 1;
            for (const auto& watch : watches) {
                ImGui::TableNextRow();
                ImGui::PushID(watch_idx);

                // Enabled checkbox
                ImGui::TableNextColumn();
                bool enabled = watch.enabled;
                if (ImGui::Checkbox("##En", &enabled)) {
                    memory_watcher_->SetWatchEnabled(watch_idx, enabled);
                }

                // Address
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                ImGui::Text("0x%llX", watch.address);
                if (font_mono_) ImGui::PopFont();

                // Context menu for address
                if (ImGui::BeginPopupContextItem("##WatchContext")) {
                    if (ImGui::MenuItem("View in Memory")) {
                        memory_address_ = watch.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", watch.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, watch.address, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem("Copy Address")) {
                        char addr_str[32];
                        snprintf(addr_str, sizeof(addr_str), "0x%llX", watch.address);
                        ImGui::SetClipboardText(addr_str);
                    }
                    ImGui::EndPopup();
                }

                // Size
                ImGui::TableNextColumn();
                ImGui::Text("%zu", watch.size);

                // Name
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", watch.name.c_str());

                // Change count
                ImGui::TableNextColumn();
                if (watch.change_count > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%u", watch.change_count);
                } else {
                    ImGui::TextDisabled("0");
                }

                // Actions
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X##Remove")) {
                    memory_watcher_->RemoveWatch(watch_idx);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Remove watch");
                }

                ImGui::PopID();
                watch_idx++;
            }

            ImGui::EndTable();
        }

        // Current values display
        if (!watches.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Current Values");
            ImGui::Separator();

            for (size_t i = 0; i < watches.size() && i < 5; i++) {
                const auto& watch = watches[i];
                if (!watch.last_value.empty()) {
                    ImGui::Text("%s: ", watch.name.c_str());
                    ImGui::SameLine();
                    if (font_mono_) ImGui::PushFont(font_mono_);

                    // Show as hex bytes (limited to 16)
                    std::stringstream ss;
                    for (size_t j = 0; j < watch.last_value.size() && j < 16; j++) {
                        ss << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(watch.last_value[j]) << " ";
                    }
                    if (watch.last_value.size() > 16) {
                        ss << "...";
                    }
                    ImGui::TextWrapped("%s", ss.str().c_str());

                    // Also show as common types if size matches
                    if (watch.size == 4) {
                        int32_t val32 = *reinterpret_cast<const int32_t*>(watch.last_value.data());
                        float valf = *reinterpret_cast<const float*>(watch.last_value.data());
                        ImGui::SameLine();
                        ImGui::TextDisabled("(i32: %d, f32: %.3f)", val32, valf);
                    } else if (watch.size == 8) {
                        int64_t val64 = *reinterpret_cast<const int64_t*>(watch.last_value.data());
                        double vald = *reinterpret_cast<const double*>(watch.last_value.data());
                        ImGui::SameLine();
                        ImGui::TextDisabled("(i64: %lld, f64: %.3f)", val64, vald);
                    }

                    if (font_mono_) ImGui::PopFont();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: Change history
    ImGui::BeginChild("HistoryPanel", ImVec2(0, 0), true);
    {
        auto recent_changes = memory_watcher_->GetRecentChanges(100);

        ImGui::Text("Change History (%zu)", recent_changes.size());
        ImGui::Separator();

        if (ImGui::BeginTable("ChangeTable", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Old Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("New Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // Show most recent changes first (reverse order)
            for (auto it = recent_changes.rbegin(); it != recent_changes.rend(); ++it) {
                const auto& change = *it;
                ImGui::TableNextRow();

                // Timestamp
                ImGui::TableNextColumn();
                auto time_t = std::chrono::system_clock::to_time_t(change.timestamp);
                std::tm tm;
#ifdef _WIN32
                localtime_s(&tm, &time_t);
#else
                localtime_r(&time_t, &tm);
#endif
                ImGui::Text("%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

                // Address
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                ImGui::Text("0x%llX", (unsigned long long)change.address);
                if (font_mono_) ImGui::PopFont();

                // Context menu
                ImGui::PushID(static_cast<int>(std::distance(recent_changes.rbegin(), it)));
                if (ImGui::BeginPopupContextItem("##ChangeContext")) {
                    if (ImGui::MenuItem("View in Memory")) {
                        memory_address_ = change.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)change.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, change.address, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem("Add Watch Here")) {
                        snprintf(watch_addr_input_, sizeof(watch_addr_input_), "%llX", (unsigned long long)change.address);
                        snprintf(watch_size_input_, sizeof(watch_size_input_), "%zu", change.new_value.size());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                // Old value (hex bytes, limited)
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                {
                    std::stringstream ss;
                    for (size_t j = 0; j < change.old_value.size() && j < 8; j++) {
                        ss << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(change.old_value[j]) << " ";
                    }
                    if (change.old_value.size() > 8) ss << "...";
                    ImGui::TextWrapped("%s", ss.str().c_str());
                }
                if (font_mono_) ImGui::PopFont();

                // New value (hex bytes, limited)
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                {
                    std::stringstream ss;
                    for (size_t j = 0; j < change.new_value.size() && j < 8; j++) {
                        ss << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(change.new_value[j]) << " ";
                    }
                    if (change.new_value.size() > 8) ss << "...";
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", ss.str().c_str());
                }
                if (font_mono_) ImGui::PopFont();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void Application::RenderRTTIScanner() {
    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("RTTI Scanner", &panels_.rtti_scanner);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a process first");
        ImGui::End();
        return;
    }

    if (selected_module_base_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a module first (right-click module -> Scan RTTI)");
        ImGui::End();
        return;
    }

    // Header with module info and scan button
    ImGui::Text("Module: %s @ 0x%llX (%.2f MB)",
        selected_module_name_.c_str(),
        (unsigned long long)selected_module_base_,
        selected_module_size_ / (1024.0f * 1024.0f));
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);

    if (rtti_scanning_) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Scanning...");
    } else {
        if (ImGui::Button("Scan Module", ImVec2(100, 0))) {
            rtti_scanning_ = true;
        }
    }

    // Perform scan if requested
    if (rtti_scanning_) {
        rtti_results_.clear();
        rtti_scanned_module_base_ = selected_module_base_;
        rtti_scanned_module_name_ = selected_module_name_;

        // Create RTTI parser with DMA read function
        auto read_func = [this](uint64_t addr, size_t size) -> std::vector<uint8_t> {
            return dma_->ReadMemory(selected_pid_, addr, size);
        };

        analysis::RTTIParser parser(read_func, selected_module_base_);

        // Scan module - this will automatically find .rdata/.data sections
        size_t found = parser.ScanModule(selected_module_base_, [this](const analysis::RTTIClassInfo& info) {
            rtti_results_.push_back(info);
        });

        LOG_INFO("RTTI scan found {} classes in {}", found, selected_module_name_);
        rtti_scanning_ = false;
    }

    // Filter input
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##RTTIFilter", "Filter by name...", rtti_filter_, sizeof(rtti_filter_));

    // Results summary
    size_t filtered_count = 0;
    std::string filter_str(rtti_filter_);
    std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

    for (const auto& info : rtti_results_) {
        if (filter_str.empty()) {
            filtered_count++;
        } else {
            std::string name_lower = info.demangled_name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            if (name_lower.find(filter_str) != std::string::npos) {
                filtered_count++;
            }
        }
    }

    ImGui::Text("Classes: %zu / %zu", filtered_count, rtti_results_.size());
    if (!rtti_scanned_module_name_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(scanned: %s)", rtti_scanned_module_name_.c_str());
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        rtti_results_.clear();
        rtti_scanned_module_name_.clear();
        rtti_scanned_module_base_ = 0;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Export CSV")) {
        // Simple CSV export
        std::string filename = rtti_scanned_module_name_ + "_rtti.csv";
        std::ofstream out(filename);
        if (out.is_open()) {
            out << "Vtable,Methods,Flags,Type,Hierarchy\n";
            for (const auto& info : rtti_results_) {
                // Extract just the class name for type column
                std::string type_name = info.demangled_name;
                if (type_name.substr(0, 6) == "class ") type_name = type_name.substr(6);
                else if (type_name.substr(0, 7) == "struct ") type_name = type_name.substr(7);

                out << "0x" << std::hex << info.vtable_address << ","
                    << std::dec << info.method_count << ","
                    << info.GetFlags() << ","
                    << "\"" << type_name << "\","
                    << "\"" << info.GetHierarchyString() << "\"\n";
            }
            out.close();
            LOG_INFO("Exported RTTI to {}", filename);
        }
    }

    ImGui::Separator();

    // Results table - IDA Class Informer style
    const float row_height = ImGui::GetTextLineHeightWithSpacing();

    if (ImGui::BeginTable("##RTTITable", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit)) {

        ImGui::TableSetupColumn("Vftable", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Methods", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                rtti_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                rtti_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Build filtered list of indices
        std::vector<size_t> filtered_indices;
        for (size_t i = 0; i < rtti_results_.size(); i++) {
            const auto& info = rtti_results_[i];
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                std::string name_lower = info.demangled_name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (name_lower.find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                }
            }
        }

        // Sort filtered indices based on current sort column
        auto& results = rtti_results_;
        int sort_col = rtti_sort_column_;
        bool ascending = rtti_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&results, sort_col, ascending](size_t a, size_t b) {
                const auto& info_a = results[a];
                const auto& info_b = results[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0: // Vtable
                        cmp = (info_a.vtable_address < info_b.vtable_address) ? -1 :
                              (info_a.vtable_address > info_b.vtable_address) ? 1 : 0;
                        break;
                    case 1: // Methods
                        cmp = (info_a.method_count < info_b.method_count) ? -1 :
                              (info_a.method_count > info_b.method_count) ? 1 : 0;
                        break;
                    case 2: // Flags
                        cmp = info_a.GetFlags().compare(info_b.GetFlags());
                        break;
                    case 3: // Type
                        cmp = info_a.demangled_name.compare(info_b.demangled_name);
                        break;
                    case 4: // Hierarchy
                        cmp = info_a.GetHierarchyString().compare(info_b.GetHierarchyString());
                        break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Use clipper for virtualization
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size(), row_height);

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& info = rtti_results_[filtered_indices[row]];

                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);

                // Vtable address column
                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);

                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "%llX", (unsigned long long)info.vtable_address);

                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(info.vtable_address);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##RTTIContext")) {
                    if (ImGui::MenuItem("View Vtable in Memory")) {
                        memory_address_ = info.vtable_address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)info.vtable_address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, info.vtable_address, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem("View Vtable in Disassembly")) {
                        // Read first function pointer and disassemble there
                        auto vtable_data = dma_->ReadMemory(selected_pid_, info.vtable_address, 8);
                        if (vtable_data.size() >= 8) {
                            uint64_t first_func = *reinterpret_cast<uint64_t*>(vtable_data.data());
                            disasm_address_ = first_func;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)first_func);
                            auto code = dma_->ReadMemory(selected_pid_, first_func, 4096);
                            if (!code.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(code, first_func);
                            }
                            panels_.disassembly = true;
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Vtable Address")) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)info.vtable_address);
                        ImGui::SetClipboardText(buf);
                    }
                    if (ImGui::MenuItem("Copy Class Name")) {
                        ImGui::SetClipboardText(info.demangled_name.c_str());
                    }
                    if (ImGui::MenuItem("Copy Mangled Name")) {
                        ImGui::SetClipboardText(info.mangled_name.c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();

                // Methods column
                ImGui::TableNextColumn();
                ImGui::Text("%u", info.method_count);

                // Flags column (M=Multiple inheritance, V=Virtual base)
                ImGui::TableNextColumn();
                std::string flags = info.GetFlags();
                if (!flags.empty()) {
                    // Color code: M=yellow, V=cyan
                    if (flags.find('M') != std::string::npos && flags.find('V') != std::string::npos) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 1.0f, 1.0f), "%s", flags.c_str());
                    } else if (flags.find('M') != std::string::npos) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", flags.c_str());
                    } else if (flags.find('V') != std::string::npos) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", flags.c_str());
                    }
                } else {
                    ImGui::TextDisabled("-");
                }

                // Type column (just the class/struct name)
                ImGui::TableNextColumn();
                std::string type_name = info.demangled_name;
                if (type_name.substr(0, 6) == "class ") type_name = type_name.substr(6);
                else if (type_name.substr(0, 7) == "struct ") type_name = type_name.substr(7);
                ImGui::TextUnformatted(type_name.c_str());

                // Hierarchy column
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(info.GetHierarchyString().c_str());
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void Application::RenderBookmarks() {
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bookmarks", &panels_.bookmarks);

    if (!bookmarks_) {
        ImGui::TextDisabled("Bookmark manager not initialized");
        ImGui::End();
        return;
    }

    // Header with count and actions
    ImGui::Text("Bookmarks: %zu", bookmarks_->Count());
    ImGui::SameLine(ImGui::GetWindowWidth() - 180);

    if (ImGui::SmallButton("Add Current")) {
        if (memory_address_ != 0) {
            show_add_bookmark_popup_ = true;
            snprintf(bookmark_label_, sizeof(bookmark_label_), "Bookmark_%zu", bookmarks_->Count() + 1);
            bookmark_notes_[0] = '\0';
            strncpy(bookmark_category_, "General", sizeof(bookmark_category_) - 1);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add bookmark at current memory address (0x%llX)", (unsigned long long)memory_address_);
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Save")) {
        bookmarks_->Save();
        status_message_ = "Bookmarks saved";
        status_timer_ = 2.0f;
    }

    // Filter
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##bmfilter", "Filter by label, category, or notes...", bookmark_filter_, sizeof(bookmark_filter_));

    ImGui::Separator();

    // Add bookmark popup
    if (show_add_bookmark_popup_) {
        ImGui::OpenPopup("Add Bookmark");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Add Bookmark", &show_add_bookmark_popup_, ImGuiWindowFlags_AlwaysAutoResize)) {
        uint64_t addr = (bookmark_edit_index_ >= 0) ?
            bookmarks_->GetAll()[bookmark_edit_index_].address : memory_address_;

        ImGui::Text("Address: 0x%llX", (unsigned long long)addr);
        if (!selected_module_name_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", selected_module_name_.c_str());
        }

        ImGui::Spacing();

        ImGui::SetNextItemWidth(250);
        ImGui::InputText("Label", bookmark_label_, sizeof(bookmark_label_));

        ImGui::SetNextItemWidth(250);
        ImGui::InputText("Category", bookmark_category_, sizeof(bookmark_category_));

        ImGui::SetNextItemWidth(250);
        ImGui::InputTextMultiline("Notes", bookmark_notes_, sizeof(bookmark_notes_),
            ImVec2(250, 60));

        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (bookmark_edit_index_ >= 0) {
                // Update existing
                Bookmark bm;
                bm.address = addr;
                bm.label = bookmark_label_;
                bm.notes = bookmark_notes_;
                bm.category = bookmark_category_;
                bm.module = selected_module_name_;
                bookmarks_->Update(bookmark_edit_index_, bm);
            } else {
                // Add new
                bookmarks_->Add(addr, bookmark_label_, bookmark_notes_,
                               bookmark_category_, selected_module_name_);
            }
            bookmark_edit_index_ = -1;
            show_add_bookmark_popup_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            bookmark_edit_index_ = -1;
            show_add_bookmark_popup_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Bookmark list
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginTable("##BookmarkTable", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 50.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Filter and display
        std::string filter_str = bookmark_filter_;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        const auto& all_bookmarks = bookmarks_->GetAll();
        std::vector<size_t> filtered_indices;

        for (size_t i = 0; i < all_bookmarks.size(); i++) {
            const auto& bm = all_bookmarks[i];
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                std::string label_lower = bm.label;
                std::string cat_lower = bm.category;
                std::string notes_lower = bm.notes;
                std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), ::tolower);
                std::transform(cat_lower.begin(), cat_lower.end(), cat_lower.begin(), ::tolower);
                std::transform(notes_lower.begin(), notes_lower.end(), notes_lower.begin(), ::tolower);

                if (label_lower.find(filter_str) != std::string::npos ||
                    cat_lower.find(filter_str) != std::string::npos ||
                    notes_lower.find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                }
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size(), row_height);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                size_t idx = filtered_indices[row];
                const auto& bm = all_bookmarks[idx];

                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                ImGui::PushID((int)idx);

                // Address column
                ImGui::TableNextColumn();
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)bm.address);

                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(bm.address);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##BookmarkCtx")) {
                    if (ImGui::MenuItem("Go to Address")) {
                        NavigateToAddress(bm.address);
                    }
                    if (ImGui::MenuItem("View in Memory")) {
                        memory_address_ = bm.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)bm.address);
                        if (dma_ && dma_->IsConnected() && selected_pid_ != 0) {
                            memory_data_ = dma_->ReadMemory(selected_pid_, bm.address, 512);
                        }
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem("View in Disassembly")) {
                        disasm_address_ = bm.address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX",
                                (unsigned long long)bm.address);
                        if (dma_ && dma_->IsConnected() && selected_pid_ != 0 && disassembler_) {
                            auto code = dma_->ReadMemory(selected_pid_, bm.address, 1024);
                            if (!code.empty()) {
                                disasm_instructions_ = disassembler_->Disassemble(code, bm.address);
                            }
                        }
                        panels_.disassembly = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Edit")) {
                        bookmark_edit_index_ = (int)idx;
                        strncpy(bookmark_label_, bm.label.c_str(), sizeof(bookmark_label_) - 1);
                        strncpy(bookmark_notes_, bm.notes.c_str(), sizeof(bookmark_notes_) - 1);
                        strncpy(bookmark_category_, bm.category.c_str(), sizeof(bookmark_category_) - 1);
                        show_add_bookmark_popup_ = true;
                    }
                    if (ImGui::MenuItem("Delete")) {
                        bookmarks_->Remove(idx);
                    }
                    ImGui::EndPopup();
                }

                // Tooltip with full info
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", bm.label.c_str());
                    ImGui::TextDisabled("0x%llX", (unsigned long long)bm.address);
                    if (!bm.module.empty()) {
                        ImGui::TextDisabled("Module: %s", bm.module.c_str());
                    }
                    if (!bm.notes.empty()) {
                        ImGui::Separator();
                        ImGui::TextWrapped("%s", bm.notes.c_str());
                    }
                    ImGui::TextDisabled("Double-click to navigate, right-click for options");
                    ImGui::EndTooltip();
                }

                // Label column
                ImGui::TableNextColumn();
                ImGui::Text("%s", bm.label.c_str());

                // Category column
                ImGui::TableNextColumn();
                if (!bm.category.empty()) {
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", bm.category.c_str());
                }

                // Actions column
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X")) {
                    bookmarks_->Remove(idx);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Delete bookmark");
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    // Auto-save reminder
    if (bookmarks_->IsDirty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Unsaved changes");
    }

    ImGui::End();
}

void Application::RenderEmulatorPanel() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Emulator", &panels_.emulator);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a process first");
        ImGui::End();
        return;
    }

    // Emulator status
    bool has_emulator = emulator_ != nullptr && emulator_->IsInitialized();
    bool correct_pid = has_emulator && emulator_pid_ == selected_pid_;

    // Status header
    ImGui::BeginChild("EmuHeader", ImVec2(0, 60), true);
    if (correct_pid) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "EMULATOR ACTIVE");
        ImGui::SameLine(200);
        ImGui::Text("PID: %u  |  Process: %s", emulator_pid_, selected_process_name_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Destroy Emulator", ImVec2(150, 0))) {
            emulator_.reset();
            emulator_pid_ = 0;
            emu_last_result_ = "Emulator destroyed";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset CPU", ImVec2(100, 0))) {
            emulator_->ResetCPU();
            emu_last_result_ = "CPU registers reset to initial state";
        }
        ImGui::SameLine();
        if (ImGui::Button("Full Reset", ImVec2(100, 0))) {
            emulator_->Reset();
            // Need to reinitialize after full reset
            if (emulator_->Initialize(dma_.get(), selected_pid_)) {
                emu_last_result_ = "Full reset complete - memory mappings cleared";
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "NO EMULATOR");
        ImGui::SameLine(200);
        ImGui::Text("Selected: PID %u  |  %s", selected_pid_, selected_process_name_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Create Emulator", ImVec2(150, 0))) {
            emulator_ = std::make_unique<emulation::Emulator>();
            if (emulator_->Initialize(dma_.get(), selected_pid_)) {
                emulator_pid_ = selected_pid_;
                emu_last_result_ = "Emulator initialized - lazy memory mapping enabled";
                LOG_INFO("Emulator created for PID {}", selected_pid_);
            } else {
                emu_last_result_ = "Failed: " + emulator_->GetLastError();
                emulator_.reset();
                LOG_ERROR("Failed to create emulator: {}", emu_last_result_);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Unicorn x64 CPU emulation with DMA memory access");
    }
    ImGui::EndChild();

    if (!correct_pid) {
        ImGui::TextDisabled("Create an emulator to access these features.");
        ImGui::End();
        return;
    }

    ImGui::Spacing();

    // Main content area with tabs
    if (ImGui::BeginTabBar("EmulatorTabs", ImGuiTabBarFlags_None)) {

        // === EXECUTION TAB ===
        if (ImGui::BeginTabItem("Execution")) {
            ImGui::BeginChild("ExecutionChild", ImVec2(0, 0), false);

            ImGui::Text("Execute Code");
            ImGui::Separator();
            ImGui::Spacing();

            // Run to address
            ImGui::Text("Run until address:");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_start", "Start address (hex)", emu_start_addr_, sizeof(emu_start_addr_));
            ImGui::SameLine();
            ImGui::Text("->");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_end", "End address (hex)", emu_end_addr_, sizeof(emu_end_addr_));
            ImGui::SameLine();
            if (ImGui::Button("Run to End", ImVec2(100, 0))) {
                if (strlen(emu_start_addr_) > 0 && strlen(emu_end_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        uint64_t end = std::stoull(emu_end_addr_, nullptr, 16);
                        auto result = emulator_->Run(start, end);
                        if (result.success) {
                            std::stringstream ss;
                            ss << "Execution complete | Final RIP: 0x" << std::hex << std::uppercase << result.final_rip;
                            ss << " | RAX: 0x" << result.registers["rax"];
                            emu_last_result_ = ss.str();
                        } else {
                            emu_last_result_ = "Execution failed: " + result.error;
                        }
                    } catch (const std::exception& e) {
                        emu_last_result_ = std::string("Invalid address: ") + e.what();
                    }
                } else {
                    emu_last_result_ = "Enter both start and end addresses";
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Step N instructions
            ImGui::Text("Step instructions:");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_step_addr", "Start address (hex)", emu_start_addr_, sizeof(emu_start_addr_));
            ImGui::SameLine();
            ImGui::Text("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::InputText("##emu_count", emu_instr_count_, sizeof(emu_instr_count_));
            ImGui::SameLine();
            ImGui::Text("instructions");
            ImGui::SameLine();
            if (ImGui::Button("Step", ImVec2(100, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        size_t count = std::stoul(emu_instr_count_);
                        auto result = emulator_->RunInstructions(start, count);
                        if (result.success) {
                            std::stringstream ss;
                            ss << "Stepped " << std::dec << count << " instructions | Final RIP: 0x"
                               << std::hex << std::uppercase << result.final_rip;
                            emu_last_result_ = ss.str();
                        } else {
                            emu_last_result_ = "Step failed: " + result.error;
                        }
                    } catch (const std::exception& e) {
                        emu_last_result_ = std::string("Invalid input: ") + e.what();
                    }
                } else {
                    emu_last_result_ = "Enter a start address";
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Quick actions
            ImGui::Text("Quick Actions:");
            if (ImGui::Button("Step 1", ImVec2(60, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        auto result = emulator_->RunInstructions(start, 1);
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << result.final_rip;
                        strncpy(emu_start_addr_, ss.str().c_str() + 2, sizeof(emu_start_addr_) - 1);
                        emu_last_result_ = result.success ? "Stepped 1 instruction" : result.error;
                    } catch (...) {}
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Step 10", ImVec2(60, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        auto result = emulator_->RunInstructions(start, 10);
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << result.final_rip;
                        strncpy(emu_start_addr_, ss.str().c_str() + 2, sizeof(emu_start_addr_) - 1);
                        emu_last_result_ = result.success ? "Stepped 10 instructions" : result.error;
                    } catch (...) {}
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Step 100", ImVec2(60, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        auto result = emulator_->RunInstructions(start, 100);
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << result.final_rip;
                        strncpy(emu_start_addr_, ss.str().c_str() + 2, sizeof(emu_start_addr_) - 1);
                        emu_last_result_ = result.success ? "Stepped 100 instructions" : result.error;
                    } catch (...) {}
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // === REGISTERS TAB ===
        if (ImGui::BeginTabItem("Registers")) {
            ImGui::BeginChild("RegistersChild", ImVec2(0, 0), false);

            static char reg_edit_value[32] = {};
            static std::string editing_reg = "";

            ImGui::Text("CPU Registers");
            ImGui::Separator();
            ImGui::Spacing();

            // Read all registers once
            struct RegDisplay {
                const char* name;
                emulation::Reg reg;
                uint64_t value;
                bool valid;
            };

            std::vector<RegDisplay> regs = {
                {"RAX", emulation::Reg::RAX, 0, false},
                {"RBX", emulation::Reg::RBX, 0, false},
                {"RCX", emulation::Reg::RCX, 0, false},
                {"RDX", emulation::Reg::RDX, 0, false},
                {"RSI", emulation::Reg::RSI, 0, false},
                {"RDI", emulation::Reg::RDI, 0, false},
                {"RBP", emulation::Reg::RBP, 0, false},
                {"RSP", emulation::Reg::RSP, 0, false},
                {"R8",  emulation::Reg::R8,  0, false},
                {"R9",  emulation::Reg::R9,  0, false},
                {"R10", emulation::Reg::R10, 0, false},
                {"R11", emulation::Reg::R11, 0, false},
                {"R12", emulation::Reg::R12, 0, false},
                {"R13", emulation::Reg::R13, 0, false},
                {"R14", emulation::Reg::R14, 0, false},
                {"R15", emulation::Reg::R15, 0, false},
                {"RIP", emulation::Reg::RIP, 0, false},
                {"RFLAGS", emulation::Reg::RFLAGS, 0, false},
            };

            // Fetch values
            for (auto& r : regs) {
                auto val = emulator_->GetRegister(r.reg);
                if (val) {
                    r.value = *val;
                    r.valid = true;
                }
            }

            // Display in a table
            if (ImGui::BeginTable("RegTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < regs.size(); i += 2) {
                    ImGui::TableNextRow();

                    // First register in row
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", regs[i].name);
                    ImGui::TableNextColumn();
                    if (regs[i].valid) {
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "0x%016llX", regs[i].value);
                        ImGui::SameLine();
                        std::string btn1 = "Set##" + std::string(regs[i].name);
                        if (ImGui::SmallButton(btn1.c_str())) {
                            editing_reg = regs[i].name;
                            snprintf(reg_edit_value, sizeof(reg_edit_value), "%llX", regs[i].value);
                            ImGui::OpenPopup("EditRegPopup");
                        }
                    } else {
                        ImGui::TextDisabled("N/A");
                    }

                    // Second register in row (if exists)
                    if (i + 1 < regs.size()) {
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", regs[i+1].name);
                        ImGui::TableNextColumn();
                        if (regs[i+1].valid) {
                            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "0x%016llX", regs[i+1].value);
                            ImGui::SameLine();
                            std::string btn2 = "Set##" + std::string(regs[i+1].name);
                            if (ImGui::SmallButton(btn2.c_str())) {
                                editing_reg = regs[i+1].name;
                                snprintf(reg_edit_value, sizeof(reg_edit_value), "%llX", regs[i+1].value);
                                ImGui::OpenPopup("EditRegPopup");
                            }
                        } else {
                            ImGui::TextDisabled("N/A");
                        }
                    }
                }
                ImGui::EndTable();
            }

            // Edit popup
            if (ImGui::BeginPopup("EditRegPopup")) {
                ImGui::Text("Set %s:", editing_reg.c_str());
                ImGui::SetNextItemWidth(180.0f);
                bool entered = ImGui::InputText("##editval", reg_edit_value, sizeof(reg_edit_value),
                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::SameLine();
                if (ImGui::Button("Apply") || entered) {
                    try {
                        uint64_t new_val = std::stoull(reg_edit_value, nullptr, 16);
                        auto reg = emulation::ParseRegister(editing_reg);
                        if (reg && emulator_->SetRegister(*reg, new_val)) {
                            emu_last_result_ = editing_reg + " = 0x" + std::string(reg_edit_value);
                        } else {
                            emu_last_result_ = "Failed to set register";
                        }
                    } catch (...) {
                        emu_last_result_ = "Invalid hex value";
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // === MEMORY TAB ===
        if (ImGui::BeginTabItem("Memory")) {
            ImGui::BeginChild("MemoryChild", ImVec2(0, 0), false);

            ImGui::Text("Memory Mapping");
            ImGui::Separator();
            ImGui::Spacing();

            // Map module
            ImGui::Text("Map entire module into emulator:");
            ImGui::SetNextItemWidth(250.0f);
            ImGui::InputTextWithHint("##emu_module", "Module name (e.g., game.exe)", emu_map_module_, sizeof(emu_map_module_));
            ImGui::SameLine();
            if (ImGui::Button("Map Module", ImVec2(120, 0))) {
                if (strlen(emu_map_module_) > 0) {
                    if (emulator_->MapModule(emu_map_module_)) {
                        emu_last_result_ = std::string("Successfully mapped: ") + emu_map_module_;
                    } else {
                        emu_last_result_ = "Failed to map module: " + emulator_->GetLastError();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Map region
            ImGui::Text("Map memory region:");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_addr", "Address (hex)", emu_map_addr_, sizeof(emu_map_addr_));
            ImGui::SameLine();
            ImGui::Text("Size:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputText("##emu_size", emu_map_size_, sizeof(emu_map_size_));
            ImGui::SameLine();
            if (ImGui::Button("Map Region", ImVec2(120, 0))) {
                if (strlen(emu_map_addr_) > 0 && strlen(emu_map_size_) > 0) {
                    try {
                        uint64_t addr = std::stoull(emu_map_addr_, nullptr, 16);
                        size_t size = std::stoul(emu_map_size_);
                        if (emulator_->MapRegion(addr, size)) {
                            std::stringstream ss;
                            ss << "Mapped 0x" << std::hex << size << " bytes at 0x" << addr;
                            emu_last_result_ = ss.str();
                        } else {
                            emu_last_result_ = "Failed to map region: " + emulator_->GetLastError();
                        }
                    } catch (const std::exception& e) {
                        emu_last_result_ = std::string("Invalid input: ") + e.what();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Statistics
            ImGui::Text("Memory Statistics:");
            const auto& accessed = emulator_->GetAccessedPages();
            ImGui::BulletText("Accessed pages: %zu", accessed.size());
            ImGui::BulletText("Total accessed memory: %zu KB", accessed.size() * 4);

            ImGui::Spacing();
            ImGui::TextDisabled("Note: With lazy mapping enabled, pages are automatically");
            ImGui::TextDisabled("fetched from the target process when accessed during emulation.");

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // === HELP TAB ===
        if (ImGui::BeginTabItem("Help")) {
            ImGui::BeginChild("HelpChild", ImVec2(0, 0), false);

            ImGui::Text("Unicorn x64 Emulator");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped(
                "This emulator runs x64 code in an isolated environment, reading memory "
                "from the target process via DMA as needed. It's useful for:"
            );
            ImGui::Spacing();
            ImGui::BulletText("Running pointer decryption routines");
            ImGui::BulletText("Analyzing obfuscated code");
            ImGui::BulletText("Understanding complex algorithms");
            ImGui::BulletText("Testing code paths without affecting the target");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Workflow:");
            ImGui::BulletText("1. Select a process and create an emulator");
            ImGui::BulletText("2. Set registers to initial values (Registers tab)");
            ImGui::BulletText("3. Optionally pre-map modules/regions (Memory tab)");
            ImGui::BulletText("4. Run code from start to end address");
            ImGui::BulletText("5. Check result in registers (e.g., RAX for return value)");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Tips:");
            ImGui::BulletText("Lazy mapping is enabled - pages load on-demand");
            ImGui::BulletText("Use 'Reset CPU' to clear registers between runs");
            ImGui::BulletText("Use 'Full Reset' to also clear mapped memory");
            ImGui::BulletText("The emulator has a 5 second timeout by default");

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Result display at bottom
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Last Result:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", emu_last_result_.c_str());

    ImGui::End();
}

void Application::RenderCS2Schema() {
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Schema Dumper", &panels_.cs2_schema);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a process first");
        ImGui::End();
        return;
    }

    // Check if CS2 process
    bool is_cs2 = selected_process_name_.find("cs2") != std::string::npos;
    if (!is_cs2) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
            "Selected process: %s", selected_process_name_.c_str());
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "This tool is designed for Counter-Strike 2 (cs2.exe)");
        ImGui::Separator();
    }

    // Initialization status
    if (!cs2_schema_initialized_ || cs2_schema_pid_ != selected_pid_) {
        ImGui::TextWrapped("CS2 Schema Dumper extracts class/field offsets from the game's SchemaSystem.");

        // Find schemasystem.dll
        uint64_t schemasystem_base = 0;
        for (const auto& mod : cached_modules_) {
            std::string lower_name = mod.name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (lower_name == "schemasystem.dll") {
                schemasystem_base = mod.base_address;
                break;
            }
        }

        if (schemasystem_base == 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "schemasystem.dll not found! Make sure CS2 is fully loaded.");
        } else {
            ImGui::Text("schemasystem.dll: 0x%llX", schemasystem_base);

            if (ImGui::Button("Initialize Schema Dumper", ImVec2(200, 0))) {
                cs2_schema_ = std::make_unique<orpheus::dumper::CS2SchemaDumper>(dma_.get(), selected_pid_);
                if (cs2_schema_->Initialize(schemasystem_base)) {
                    cs2_schema_pid_ = selected_pid_;
                    cs2_schema_initialized_ = true;
                    LOG_INFO("CS2 Schema Dumper initialized");
                } else {
                    LOG_ERROR("Failed to initialize CS2 Schema Dumper: {}",
                        cs2_schema_->GetLastError());
                    cs2_schema_.reset();
                }
            }
        }
    } else {
        // Initialized - show controls
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Schema System: 0x%llX",
            cs2_schema_->GetSchemaSystemAddress());

        const auto& scopes = cs2_schema_->GetScopes();
        ImGui::Text("Type Scopes: %zu", scopes.size());

        ImGui::Separator();

        // Dump controls
        if (cs2_schema_dumping_) {
            ImGui::ProgressBar((float)cs2_schema_progress_ / std::max(1, cs2_schema_total_),
                ImVec2(-1, 0), "Dumping...");
        } else {
            if (ImGui::Button("Dump All Schemas", ImVec2(150, 0))) {
                cs2_schema_dumping_ = true;
                cs2_schema_progress_ = 0;
                cs2_schema_total_ = 1;

                auto all_schemas = cs2_schema_->DumpAll([this](int current, int total) {
                    cs2_schema_progress_ = current;
                    cs2_schema_total_ = total;
                });

                // Flatten for display
                cs2_cached_classes_.clear();
                for (const auto& [scope, classes] : all_schemas) {
                    for (const auto& cls : classes) {
                        cs2_cached_classes_.push_back(cls);
                    }
                }

                cs2_schema_dumping_ = false;
                LOG_INFO("Dumped {} classes, {} total fields",
                    cs2_schema_->GetTotalClassCount(),
                    cs2_schema_->GetTotalFieldCount());
            }

            ImGui::SameLine();
            if (ImGui::Button("Export JSON", ImVec2(100, 0))) {
                if (cs2_schema_->ExportToJson("cs2_schema.json")) {
                    LOG_INFO("Exported schema to cs2_schema.json");
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Export Header", ImVec2(100, 0))) {
                if (cs2_schema_->ExportToHeader("cs2_offsets.h")) {
                    LOG_INFO("Exported schema to cs2_offsets.h");
                }
            }
        }

        // Stats
        if (!cs2_cached_classes_.empty()) {
            ImGui::Text("Classes: %zu | Fields: %zu",
                cs2_schema_->GetTotalClassCount(),
                cs2_schema_->GetTotalFieldCount());
        }

        ImGui::Separator();

        // Filters
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##class_filter", "Filter classes...",
            cs2_class_filter_, sizeof(cs2_class_filter_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##field_filter", "Filter fields...",
            cs2_field_filter_, sizeof(cs2_field_filter_));

        // Two-pane view: classes on left, fields on right
        float avail_width = ImGui::GetContentRegionAvail().x;
        float avail_height = ImGui::GetContentRegionAvail().y;

        // Class list (left pane)
        ImGui::BeginChild("ClassList", ImVec2(avail_width * 0.4f, avail_height), true);
        ImGui::Text("Classes");
        ImGui::Separator();

        std::string class_filter_lower = cs2_class_filter_;
        std::transform(class_filter_lower.begin(), class_filter_lower.end(),
            class_filter_lower.begin(), ::tolower);

        ImGuiListClipper clipper;
        std::vector<size_t> filtered_indices;

        for (size_t i = 0; i < cs2_cached_classes_.size(); i++) {
            if (!class_filter_lower.empty()) {
                std::string name_lower = cs2_cached_classes_[i].name;
                std::transform(name_lower.begin(), name_lower.end(),
                    name_lower.begin(), ::tolower);
                if (name_lower.find(class_filter_lower) == std::string::npos) {
                    continue;
                }
            }
            filtered_indices.push_back(i);
        }

        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                size_t idx = filtered_indices[row];
                const auto& cls = cs2_cached_classes_[idx];

                bool selected = (cs2_selected_class_ == cls.name);
                if (ImGui::Selectable(cls.name.c_str(), selected)) {
                    cs2_selected_class_ = cls.name;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Module: %s", cls.module.c_str());
                    ImGui::Text("Size: 0x%X (%u bytes)", cls.size, cls.size);
                    if (!cls.base_class.empty()) {
                        ImGui::Text("Base: %s", cls.base_class.c_str());
                    }
                    ImGui::Text("Fields: %zu", cls.fields.size());
                    ImGui::EndTooltip();
                }
            }
        }
        clipper.End();

        ImGui::EndChild();

        ImGui::SameLine();

        // Field list (right pane)
        ImGui::BeginChild("FieldList", ImVec2(0, avail_height), true);

        const orpheus::dumper::SchemaClass* selected_cls = nullptr;
        for (const auto& cls : cs2_cached_classes_) {
            if (cls.name == cs2_selected_class_) {
                selected_cls = &cls;
                break;
            }
        }

        if (selected_cls) {
            ImGui::Text("%s", selected_cls->name.c_str());
            if (!selected_cls->base_class.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                    ": %s", selected_cls->base_class.c_str());
            }
            ImGui::Text("Size: 0x%X | Module: %s",
                selected_cls->size, selected_cls->module.c_str());
            ImGui::Separator();

            std::string field_filter_lower = cs2_field_filter_;
            std::transform(field_filter_lower.begin(), field_filter_lower.end(),
                field_filter_lower.begin(), ::tolower);

            if (ImGui::BeginTable("Fields", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {

                ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& field : selected_cls->fields) {
                    if (!field_filter_lower.empty()) {
                        std::string name_lower = field.name;
                        std::transform(name_lower.begin(), name_lower.end(),
                            name_lower.begin(), ::tolower);
                        if (name_lower.find(field_filter_lower) == std::string::npos) {
                            continue;
                        }
                    }

                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                        "0x%04X", field.offset);

                    // Copy offset on click
                    if (ImGui::IsItemClicked()) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "0x%X", field.offset);
                        ImGui::SetClipboardText(buf);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Click to copy offset");
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", field.name.c_str());

                    // Copy field name on click
                    if (ImGui::IsItemClicked()) {
                        ImGui::SetClipboardText(field.name.c_str());
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Click to copy name");
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        "%s", field.type_name.c_str());
                }

                ImGui::EndTable();
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "Select a class to view fields");
        }

        ImGui::EndChild();
    }

    ImGui::End();
}

void Application::RenderCS2EntityInspector() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Entity Inspector", &panels_.cs2_entity_inspector);

    // Check if process selected
    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
            "No process selected. Select cs2.exe from the process list.");
        ImGui::End();
        return;
    }

    // Check if CS2 process
    bool is_cs2 = selected_process_name_.find("cs2") != std::string::npos;
    if (!is_cs2) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
            "This tool is designed for Counter-Strike 2 (cs2.exe)");
        ImGui::End();
        return;
    }

    // Check if schema is initialized (required for field lookups)
    if (!cs2_schema_initialized_ || cs2_schema_pid_ != selected_pid_) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
            "Schema not initialized. Open CS2 Schema Dumper first and dump schemas.");
        if (ImGui::Button("Open Schema Dumper")) {
            panels_.cs2_schema = true;
        }
        ImGui::End();
        return;
    }

    // Initialize entity system if needed
    if (!cs2_entity_initialized_) {
        ImGui::TextWrapped("Entity Inspector uses RTTI + Schema to identify and inspect live entities.");
        ImGui::Spacing();

        if (ImGui::Button("Initialize Entity System", ImVec2(200, 0))) {
            // Find client.dll
            for (const auto& mod : cached_modules_) {
                std::string name_lower = mod.name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (name_lower == "client.dll") {
                    cs2_client_base_ = mod.base_address;
                    cs2_client_size_ = mod.size;
                    break;
                }
            }

            if (cs2_client_base_ == 0) {
                LOG_ERROR("client.dll not found!");
            } else {
                // Read a portion of client.dll for pattern scanning
                // Scan first 20MB which should contain the signatures
                size_t scan_size = std::min(static_cast<size_t>(cs2_client_size_), static_cast<size_t>(20 * 1024 * 1024));
                auto client_data = dma_->ReadMemory(selected_pid_, cs2_client_base_, scan_size);

                if (!client_data.empty()) {
                    // Pattern scan for CGameEntitySystem
                    // Pattern: 48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B F0
                    auto entity_pattern = analysis::PatternScanner::Compile(
                        "48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B F0", "EntitySystem");

                    if (entity_pattern) {
                        auto entity_results = analysis::PatternScanner::Scan(
                            client_data, *entity_pattern, cs2_client_base_, 1);

                        if (!entity_results.empty()) {
                            // Resolve RIP-relative address
                            uint64_t instr_addr = entity_results[0];
                            auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                            if (offset_data.size() >= 4) {
                                int32_t rip_offset;
                                std::memcpy(&rip_offset, offset_data.data(), 4);
                                uint64_t ptr_addr = instr_addr + 7 + rip_offset;

                                auto ptr_data = dma_->ReadMemory(selected_pid_, ptr_addr, 8);
                                if (ptr_data.size() >= 8) {
                                    std::memcpy(&cs2_entity_system_, ptr_data.data(), 8);
                                    LOG_INFO("Found CGameEntitySystem: 0x{:X}", cs2_entity_system_);
                                }
                            }
                        }
                    }

                    // Pattern scan for LocalPlayerController array
                    auto lpc_pattern = analysis::PatternScanner::Compile(
                        "48 8D 0D ?? ?? ?? ?? 48 8B 04 C1", "LocalPlayerArray");

                    if (lpc_pattern) {
                        auto lpc_results = analysis::PatternScanner::Scan(
                            client_data, *lpc_pattern, cs2_client_base_, 1);

                        if (!lpc_results.empty()) {
                            uint64_t instr_addr = lpc_results[0];
                            auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                            if (offset_data.size() >= 4) {
                                int32_t rip_offset;
                                std::memcpy(&rip_offset, offset_data.data(), 4);
                                cs2_local_player_array_ = instr_addr + 7 + rip_offset;
                                LOG_INFO("Found LocalPlayerController array: 0x{:X}", cs2_local_player_array_);
                            }
                        }
                    }
                }

                if (cs2_entity_system_ != 0 && cs2_local_player_array_ != 0) {
                    cs2_entity_initialized_ = true;
                    LOG_INFO("CS2 Entity System initialized successfully");
                } else {
                    LOG_ERROR("Failed to find entity system patterns");
                }
            }
        }
        ImGui::End();
        return;
    }

    // Top section: Local Player info
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Entity System: 0x%llX", cs2_entity_system_);
    ImGui::SameLine(300);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Client: 0x%llX", cs2_client_base_);

    ImGui::Separator();

    // Local Player Section
    if (ImGui::CollapsingHeader("Local Player", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Read local player controller
        auto lpc_data = dma_->ReadMemory(selected_pid_, cs2_local_player_array_, 8);
        uint64_t local_controller = 0;
        if (lpc_data.size() >= 8) {
            std::memcpy(&local_controller, lpc_data.data(), 8);
        }

        if (local_controller != 0) {
            ImGui::Text("Controller: 0x%llX", local_controller);

            // Read key fields using schema offsets
            if (cs2_schema_) {
                // m_hPlayerPawn offset
                uint32_t pawn_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_hPlayerPawn");
                uint32_t health_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_iPawnHealth");
                uint32_t armor_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_iPawnArmor");
                uint32_t alive_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_bPawnIsAlive");

                if (pawn_offset > 0) {
                    auto pawn_data = dma_->ReadMemory(selected_pid_, local_controller + pawn_offset, 4);
                    if (pawn_data.size() >= 4) {
                        uint32_t pawn_handle;
                        std::memcpy(&pawn_handle, pawn_data.data(), 4);
                        int pawn_index = pawn_handle & 0x7FFF;
                        ImGui::Text("Pawn Handle: 0x%X (index %d)", pawn_handle, pawn_index);
                    }
                }

                if (health_offset > 0) {
                    auto health_data = dma_->ReadMemory(selected_pid_, local_controller + health_offset, 4);
                    if (health_data.size() >= 4) {
                        int32_t health;
                        std::memcpy(&health, health_data.data(), 4);
                        ImVec4 health_color = health > 50 ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) :
                                              health > 25 ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) :
                                                           ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                        ImGui::TextColored(health_color, "Health: %d", health);
                    }
                }

                if (armor_offset > 0) {
                    auto armor_data = dma_->ReadMemory(selected_pid_, local_controller + armor_offset, 4);
                    if (armor_data.size() >= 4) {
                        int32_t armor;
                        std::memcpy(&armor, armor_data.data(), 4);
                        ImGui::SameLine();
                        ImGui::Text("Armor: %d", armor);
                    }
                }

                if (alive_offset > 0) {
                    auto alive_data = dma_->ReadMemory(selected_pid_, local_controller + alive_offset, 1);
                    if (alive_data.size() >= 1) {
                        bool alive = alive_data[0] != 0;
                        ImGui::SameLine();
                        if (alive) {
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[ALIVE]");
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[DEAD]");
                        }
                    }
                }
            }

            // Button to inspect controller
            if (ImGui::Button("Inspect Controller")) {
                cs2_selected_entity_ = local_controller;
                cs2_selected_entity_class_ = "CCSPlayerController";
                cs2_field_cache_.clear();
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Not in game (no local player)");
        }
    }

    // Player List Section - enumerate all connected players
    if (ImGui::CollapsingHeader("Player List")) {
        // Verified offsets from research
        constexpr uint32_t OFFSET_PLAYER_NAME = 0x6E8;
        constexpr uint32_t OFFSET_TEAM_NUM = 0x3EB;
        constexpr uint32_t OFFSET_PAWN_HANDLE = 0x8FC;
        constexpr uint32_t OFFSET_PAWN_IS_ALIVE = 0x904;
        constexpr uint32_t OFFSET_PAWN_HEALTH = 0x908;
        constexpr uint32_t OFFSET_CONNECTED = 0x6E4;
        constexpr uint32_t OFFSET_STEAM_ID = 0x770;

        // Read chunk 0 pointer (controllers are in indices 1-64)
        auto chunk0_ptr_data = dma_->ReadMemory(selected_pid_, cs2_entity_system_ + 0x10, 8);
        uint64_t chunk0_ptr = 0;
        if (chunk0_ptr_data.size() >= 8) {
            std::memcpy(&chunk0_ptr, chunk0_ptr_data.data(), 8);
        }

        if (chunk0_ptr != 0) {
            uint64_t chunk0_base = chunk0_ptr & ~0xFULL;  // Mask off flag bits

            // Player table
            if (ImGui::BeginTable("##PlayerTable", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                ImVec2(0, 200))) {

                ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 30);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("Bot", ImGuiTableColumnFlags_WidthFixed, 30);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableHeadersRow();

                for (int idx = 1; idx <= 64; idx++) {
                    // Calculate entry address: chunk_base + 0x08 + slot * 0x70
                    uint64_t entry_addr = chunk0_base + 0x08 + idx * 0x70;
                    auto ctrl_data = dma_->ReadMemory(selected_pid_, entry_addr, 8);
                    if (ctrl_data.size() < 8) continue;

                    uint64_t controller;
                    std::memcpy(&controller, ctrl_data.data(), 8);
                    if (controller == 0 || controller < 0x10000000000ULL) continue;

                    // Check connection state (0=Connected, 1=Connecting, 2=Reconnecting, 3+=Disconnected)
                    auto conn_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_CONNECTED, 4);
                    if (conn_data.size() < 4) continue;
                    uint32_t connected;
                    std::memcpy(&connected, conn_data.data(), 4);
                    if (connected > 2) continue;  // Skip disconnected/reserved/never connected

                    // Read player name
                    auto name_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PLAYER_NAME, 64);
                    if (name_data.empty()) continue;
                    std::string name(reinterpret_cast<char*>(name_data.data()));
                    if (name.empty()) continue;

                    // Read other fields
                    auto steam_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_STEAM_ID, 8);
                    uint64_t steam_id = 0;
                    if (steam_data.size() >= 8) std::memcpy(&steam_id, steam_data.data(), 8);
                    bool is_bot = (steam_id == 0);

                    auto team_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_TEAM_NUM, 1);
                    uint8_t team = team_data.size() >= 1 ? team_data[0] : 0;

                    auto alive_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_IS_ALIVE, 1);
                    bool is_alive = alive_data.size() >= 1 && alive_data[0] != 0;

                    auto health_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_HEALTH, 4);
                    uint32_t health = 0;
                    if (health_data.size() >= 4) std::memcpy(&health, health_data.data(), 4);

                    ImGui::TableNextRow();

                    // Index
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", idx);

                    // Name
                    ImGui::TableNextColumn();
                    if (!is_alive) {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", name.c_str());
                    } else {
                        ImGui::Text("%s", name.c_str());
                    }

                    // Team
                    ImGui::TableNextColumn();
                    if (team == 2) {
                        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "T");
                    } else if (team == 3) {
                        ImGui::TextColored(ImVec4(0.3f, 0.7f, 0.9f, 1.0f), "CT");
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "?");
                    }

                    // Health
                    ImGui::TableNextColumn();
                    if (is_alive) {
                        ImVec4 hp_color = health > 50 ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) :
                                          health > 25 ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) :
                                                        ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                        ImGui::TextColored(hp_color, "%d", health);
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
                    }

                    // Bot
                    ImGui::TableNextColumn();
                    if (is_bot) {
                        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.9f, 1.0f), "Y");
                    }

                    // Actions
                    ImGui::TableNextColumn();
                    ImGui::PushID(idx);
                    if (ImGui::SmallButton("Inspect")) {
                        cs2_selected_entity_ = controller;
                        cs2_selected_entity_class_ = "CCSPlayerController";
                        cs2_field_cache_.clear();
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Entity system not ready");
        }
    }

    ImGui::Separator();

    // Two-column layout: Entity browser | Field inspector
    float panel_width = ImGui::GetContentRegionAvail().x;

    // Left panel: Entity inspector for selected entity
    ImGui::BeginChild("##EntityInspector", ImVec2(0, 0), true);

    if (cs2_selected_entity_ != 0) {
        ImGui::Text("Inspecting: 0x%llX", cs2_selected_entity_);
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Class: %s",
            cs2_selected_entity_class_.empty() ? "Unknown" : cs2_selected_entity_class_.c_str());

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
        ImGui::Checkbox("Auto-refresh", &cs2_entity_auto_refresh_);

        if (ImGui::Button("Refresh Fields") || (cs2_entity_auto_refresh_ && cs2_field_cache_.empty())) {
            cs2_field_cache_.clear();

            // Get fields from schema
            if (cs2_schema_ && !cs2_selected_entity_class_.empty()) {
                auto cls = cs2_schema_->FindClass(cs2_selected_entity_class_);
                if (cls) {
                    for (const auto& field : cls->fields) {
                        FieldCacheEntry entry;
                        entry.name = field.name;
                        entry.type = field.type_name;
                        entry.offset = field.offset;

                        // Read and format value based on type
                        size_t read_size = 8;  // Default
                        if (field.type_name.find("bool") != std::string::npos) read_size = 1;
                        else if (field.type_name.find("int8") != std::string::npos) read_size = 1;
                        else if (field.type_name.find("int16") != std::string::npos ||
                                 field.type_name.find("uint16") != std::string::npos) read_size = 2;
                        else if (field.type_name.find("int32") != std::string::npos ||
                                 field.type_name.find("uint32") != std::string::npos ||
                                 field.type_name.find("float") != std::string::npos) read_size = 4;

                        auto value_data = dma_->ReadMemory(selected_pid_,
                            cs2_selected_entity_ + field.offset, read_size);

                        if (!value_data.empty()) {
                            std::stringstream ss;
                            if (field.type_name.find("bool") != std::string::npos) {
                                ss << (value_data[0] ? "true" : "false");
                            } else if (field.type_name.find("float") != std::string::npos) {
                                float val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << std::fixed << std::setprecision(2) << val;
                            } else if (field.type_name.find("int32") != std::string::npos) {
                                int32_t val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << val;
                            } else if (field.type_name.find("uint32") != std::string::npos) {
                                uint32_t val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << val;
                            } else if (field.type_name.find("Handle") != std::string::npos) {
                                uint32_t val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << "0x" << std::hex << val << " (idx " << std::dec << (val & 0x7FFF) << ")";
                            } else if (read_size == 8) {
                                uint64_t val;
                                std::memcpy(&val, value_data.data(), 8);
                                ss << "0x" << std::hex << val;
                            } else {
                                // Hex dump for unknown types
                                for (size_t i = 0; i < value_data.size() && i < 8; i++) {
                                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)value_data[i];
                                }
                            }
                            entry.value = ss.str();
                        } else {
                            entry.value = "???";
                        }

                        cs2_field_cache_.push_back(entry);
                    }
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Go to Memory")) {
            NavigateToAddress(cs2_selected_entity_);
        }

        ImGui::Separator();

        // Field filter
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##field_filter", "Filter fields...",
            cs2_entity_filter_, sizeof(cs2_entity_filter_));

        // Field table
        if (ImGui::BeginTable("##FieldTable", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {

            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableHeadersRow();

            std::string filter_lower = cs2_entity_filter_;
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

            for (const auto& field : cs2_field_cache_) {
                // Filter
                if (!filter_lower.empty()) {
                    std::string name_lower = field.name;
                    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                    if (name_lower.find(filter_lower) == std::string::npos) continue;
                }

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("0x%X", field.offset);

                ImGui::TableNextColumn();
                ImGui::Text("%s", field.name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", field.type.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", field.value.c_str());
            }

            ImGui::EndTable();
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "No entity selected. Click 'Inspect Controller' above or enter an address:");

        static char addr_buf[32] = {};
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputTextWithHint("##entity_addr", "0x...", addr_buf, sizeof(addr_buf),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
            try {
                cs2_selected_entity_ = std::stoull(addr_buf, nullptr, 16);
                cs2_selected_entity_class_.clear();
                cs2_field_cache_.clear();

                // Try to identify class via RTTI
                auto vtable_data = dma_->ReadMemory(selected_pid_, cs2_selected_entity_, 8);
                if (vtable_data.size() >= 8) {
                    uint64_t vtable;
                    std::memcpy(&vtable, vtable_data.data(), 8);

                    // Check if vtable is in client.dll range
                    if (vtable >= cs2_client_base_ && vtable < cs2_client_base_ + cs2_client_size_) {
                        // Create read function for RTTI parser
                        auto read_func = [this](uint64_t addr, size_t size) {
                            return dma_->ReadMemory(selected_pid_, addr, size);
                        };
                        analysis::RTTIParser rtti(read_func, cs2_client_base_);
                        auto info = rtti.ParseVTable(vtable);
                        if (info && !info->demangled_name.empty()) {
                            // Strip "class " prefix
                            std::string name = info->demangled_name;
                            if (name.substr(0, 6) == "class ") {
                                name = name.substr(6);
                            }
                            cs2_selected_entity_class_ = name;
                        }
                    }
                }
            } catch (...) {
                LOG_ERROR("Invalid address format");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Inspect")) {
            // Trigger the InputText callback
        }
    }

    ImGui::EndChild();

    // Auto-refresh logic
    if (cs2_entity_auto_refresh_ && cs2_selected_entity_ != 0) {
        cs2_entity_refresh_timer_ += ImGui::GetIO().DeltaTime;
        if (cs2_entity_refresh_timer_ > 0.5f) {  // Refresh every 500ms
            cs2_entity_refresh_timer_ = 0;
            cs2_field_cache_.clear();  // Will be repopulated next frame
        }
    }

    ImGui::End();
}

void Application::RenderConsole() {
    ImGui::Begin("Console", &panels_.console);

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##console_filter", "Filter...", console_filter_, sizeof(console_filter_));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &console_auto_scroll_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        Logger::Instance().ClearBuffer();
    }

    ImGui::Separator();

    ImGui::BeginChild("##ConsoleScroll", ImVec2(0, 0), true);

    auto entries = Logger::Instance().GetRecentEntries(500);

    for (const auto& entry : entries) {
        if (console_filter_[0] != '\0') {
            if (entry.message.find(console_filter_) == std::string::npos) continue;
        }

        ImVec4 color;
        const char* prefix;
        switch (entry.level) {
            case spdlog::level::trace:   color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); prefix = "TRC"; break;
            case spdlog::level::debug:   color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); prefix = "DBG"; break;
            case spdlog::level::info:    color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); prefix = "INF"; break;
            case spdlog::level::warn:    color = ImVec4(0.9f, 0.7f, 0.2f, 1.0f); prefix = "WRN"; break;
            case spdlog::level::err:     color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); prefix = "ERR"; break;
            case spdlog::level::critical: color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); prefix = "CRT"; break;
            default: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); prefix = "???"; break;
        }

        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "[%s]", entry.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(color, "[%s]", prefix);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", entry.message.c_str());
    }

    if (console_auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::End();
}

void Application::RenderCommandPalette() {
    ImGui::OpenPopup("Command Palette");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 300));

    if (ImGui::BeginPopupModal("Command Palette", &show_command_palette_,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##cmd_search", "Type a command...", command_search_, sizeof(command_search_));

        ImGui::Separator();

        ImGui::BeginChild("##CmdList", ImVec2(0, 0), false);

        std::string search_lower = command_search_;
        std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);

        for (const auto& kb : keybinds_) {
            std::string name_lower = kb.name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

            if (search_lower.empty() || name_lower.find(search_lower) != std::string::npos) {
                if (ImGui::Selectable(kb.name.c_str())) {
                    if (kb.action) kb.action();
                    show_command_palette_ = false;
                }
                ImGui::SameLine(400);
                ImGui::TextDisabled("%s", kb.description.c_str());
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

void Application::RenderAboutDialog() {
    ImGui::OpenPopup("About Orpheus");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("About Orpheus", &show_about_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Orpheus - DMA Reversing Framework");
        ImGui::Separator();
        ImGui::Text("Version: 1.0.0");
        ImGui::Spacing();
        ImGui::TextWrapped("A comprehensive DMA-based reverse engineering tool for security research.");
        ImGui::Spacing();
        ImGui::TextDisabled("For educational and authorized security research only.");
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            show_about_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::RenderGotoDialog() {
    ImGui::OpenPopup("Goto Address");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Goto Address", &show_goto_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char goto_addr[32] = {};

        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("Address", goto_addr, sizeof(goto_addr),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            uint64_t addr = strtoull(goto_addr, nullptr, 16);
            NavigateToAddress(addr);
            show_goto_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Go")) {
            uint64_t addr = strtoull(goto_addr, nullptr, 16);
            NavigateToAddress(addr);
            show_goto_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_goto_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::RenderDumpDialog() {
    ImGui::OpenPopup("Dump Module");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Dump Module", &show_dump_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Module: %s", selected_module_name_.c_str());
        ImGui::Text("Base: 0x%llX", selected_module_base_);
        ImGui::Text("Size: 0x%X (%u bytes)", selected_module_size_, selected_module_size_);

        ImGui::Separator();

        // Initialize filename if empty
        if (ImGui::IsWindowAppearing() && dump_filename_[0] == '\0') {
            snprintf(dump_filename_, sizeof(dump_filename_), "%s_dumped.exe", selected_module_name_.c_str());
        }

        ImGui::SetNextItemWidth(400.0f);
        ImGui::InputText("Filename", dump_filename_, sizeof(dump_filename_));

        ImGui::Spacing();
        ImGui::Text("Options:");
        ImGui::Checkbox("Fix PE Headers", &dump_fix_headers_);
        ImGui::Checkbox("Rebuild IAT", &dump_rebuild_iat_);
        ImGui::Checkbox("Unmap Sections (File Alignment)", &dump_unmap_sections_);

        ImGui::Separator();

        if (dump_in_progress_) {
            ImGui::ProgressBar(dump_progress_, ImVec2(-1, 0), "Dumping...");
            ImGui::TextDisabled("Please wait...");
        } else {
            if (ImGui::Button("Dump", ImVec2(120, 0))) {
                DumpModule(selected_module_base_, selected_module_size_, dump_filename_);
                show_dump_dialog_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_dump_dialog_ = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void Application::RefreshProcesses() {
    if (dma_ && dma_->IsConnected()) {
        cached_processes_ = dma_->GetProcessList();
    }
}

void Application::RefreshModules() {
    if (dma_ && dma_->IsConnected() && selected_pid_ != 0) {
        cached_modules_ = dma_->GetModuleList(selected_pid_);
    }
}

bool Application::IsCS2Process() const {
    std::string name_lower = selected_process_name_;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    return name_lower.find("cs2") != std::string::npos;
}

bool Application::InitializeCS2() {
    if (!dma_ || !dma_->IsConnected() || selected_pid_ == 0) {
        return false;
    }

    if (!IsCS2Process()) {
        return false;
    }

    LOG_INFO("Auto-initializing CS2 systems...");

    // Find required modules
    uint64_t schemasystem_base = 0;
    uint64_t client_base = 0;
    uint32_t client_size = 0;

    for (const auto& mod : cached_modules_) {
        std::string name_lower = mod.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower == "schemasystem.dll") {
            schemasystem_base = mod.base_address;
        } else if (name_lower == "client.dll") {
            client_base = mod.base_address;
            client_size = mod.size;
        }
    }

    if (schemasystem_base == 0) {
        LOG_WARN("CS2 auto-init: schemasystem.dll not found - game may still be loading");
        return false;
    }

    if (client_base == 0) {
        LOG_WARN("CS2 auto-init: client.dll not found - game may still be loading");
        return false;
    }

    // Initialize Schema System
    if (!cs2_schema_initialized_ || cs2_schema_pid_ != selected_pid_) {
        cs2_schema_ = std::make_unique<orpheus::dumper::CS2SchemaDumper>(dma_.get(), selected_pid_);
        if (cs2_schema_->Initialize(schemasystem_base)) {
            cs2_schema_pid_ = selected_pid_;
            cs2_schema_initialized_ = true;
            LOG_INFO("CS2 Schema System initialized");
        } else {
            LOG_ERROR("Failed to initialize CS2 Schema Dumper: {}", cs2_schema_->GetLastError());
            cs2_schema_.reset();
            return false;
        }
    }

    // Initialize Entity System
    if (!cs2_entity_initialized_) {
        cs2_client_base_ = client_base;
        cs2_client_size_ = client_size;

        // Read a portion of client.dll for pattern scanning (first 20MB)
        size_t scan_size = std::min(static_cast<size_t>(client_size), static_cast<size_t>(20 * 1024 * 1024));
        auto client_data = dma_->ReadMemory(selected_pid_, client_base, scan_size);

        if (!client_data.empty()) {
            // Pattern scan for CGameEntitySystem
            auto entity_pattern = analysis::PatternScanner::Compile(
                "48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B F0", "EntitySystem");

            if (entity_pattern) {
                auto entity_results = analysis::PatternScanner::Scan(
                    client_data, *entity_pattern, client_base, 1);

                if (!entity_results.empty()) {
                    uint64_t instr_addr = entity_results[0];
                    auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                    if (offset_data.size() >= 4) {
                        int32_t rip_offset;
                        std::memcpy(&rip_offset, offset_data.data(), 4);
                        uint64_t ptr_addr = instr_addr + 7 + rip_offset;

                        auto ptr_data = dma_->ReadMemory(selected_pid_, ptr_addr, 8);
                        if (ptr_data.size() >= 8) {
                            std::memcpy(&cs2_entity_system_, ptr_data.data(), 8);
                            LOG_INFO("Found CGameEntitySystem: 0x{:X}", cs2_entity_system_);
                        }
                    }
                }
            }

            // Pattern scan for LocalPlayerController array
            auto lpc_pattern = analysis::PatternScanner::Compile(
                "48 8D 0D ?? ?? ?? ?? 48 8B 04 C1", "LocalPlayerArray");

            if (lpc_pattern) {
                auto lpc_results = analysis::PatternScanner::Scan(
                    client_data, *lpc_pattern, client_base, 1);

                if (!lpc_results.empty()) {
                    uint64_t instr_addr = lpc_results[0];
                    auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                    if (offset_data.size() >= 4) {
                        int32_t rip_offset;
                        std::memcpy(&rip_offset, offset_data.data(), 4);
                        cs2_local_player_array_ = instr_addr + 7 + rip_offset;
                        LOG_INFO("Found LocalPlayerController array: 0x{:X}", cs2_local_player_array_);
                    }
                }
            }

            // Pattern scan for map name (GlobalVars->mapname or similar)
            // Pattern: 48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? E8 (GlobalVars access)
            auto globals_pattern = analysis::PatternScanner::Compile(
                "48 89 15 ?? ?? ?? ?? 48 89 42 60", "GlobalVars");

            if (globals_pattern) {
                auto globals_results = analysis::PatternScanner::Scan(
                    client_data, *globals_pattern, client_base, 1);

                if (!globals_results.empty()) {
                    uint64_t instr_addr = globals_results[0];
                    auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                    if (offset_data.size() >= 4) {
                        int32_t rip_offset;
                        std::memcpy(&rip_offset, offset_data.data(), 4);
                        uint64_t globals_ptr = instr_addr + 7 + rip_offset;

                        auto ptr_data = dma_->ReadMemory(selected_pid_, globals_ptr, 8);
                        if (ptr_data.size() >= 8) {
                            uint64_t global_vars;
                            std::memcpy(&global_vars, ptr_data.data(), 8);
                            // Map name is at offset 0x188 in GlobalVars
                            radar_map_name_addr_ = global_vars + 0x188;
                            LOG_INFO("Found GlobalVars: 0x{:X}, map name at 0x{:X}",
                                global_vars, radar_map_name_addr_);
                        }
                    }
                }
            }
        }

        if (cs2_entity_system_ != 0 && cs2_local_player_array_ != 0) {
            cs2_entity_initialized_ = true;
            LOG_INFO("CS2 Entity System initialized successfully");
        } else {
            LOG_WARN("CS2 Entity System partially initialized - some patterns not found");
        }
    }

    // Mark auto-init as successful if at least schema is initialized
    cs2_auto_init_success_ = cs2_schema_initialized_;

    if (cs2_auto_init_success_) {
        // Auto-open CS2 panels
        panels_.cs2_radar = true;
        panels_.cs2_dashboard = true;
        LOG_INFO("CS2 auto-initialization complete - radar and dashboard enabled");
    }

    return cs2_auto_init_success_;
}

void Application::NavigateToAddress(uint64_t address, bool add_to_history) {
    // Add to history if this is a user navigation (not back/forward)
    if (add_to_history && address != 0) {
        // If we navigated back and are now going to a new address, trim forward history
        if (history_index_ >= 0 && history_index_ < (int)address_history_.size() - 1) {
            address_history_.erase(address_history_.begin() + history_index_ + 1, address_history_.end());
        }

        // Don't add duplicates of the current address
        if (address_history_.empty() || address_history_.back() != address) {
            address_history_.push_back(address);

            // Limit history size
            while (address_history_.size() > MAX_HISTORY_SIZE) {
                address_history_.pop_front();
            }

            history_index_ = (int)address_history_.size() - 1;
        }
    }

    // Update memory viewer
    memory_address_ = address;
    snprintf(address_input_, sizeof(address_input_), "%llX", (unsigned long long)address);
    if (dma_ && dma_->IsConnected() && selected_pid_ != 0) {
        memory_data_ = dma_->ReadMemory(selected_pid_, address, 512);
    }

    // Update disassembly
    disasm_address_ = address;
    snprintf(disasm_address_input_, sizeof(disasm_address_input_), "%llX", (unsigned long long)address);
    if (dma_ && dma_->IsConnected() && selected_pid_ != 0 && disassembler_) {
        auto code = dma_->ReadMemory(selected_pid_, address, 1024);
        if (!code.empty()) {
            disasm_instructions_ = disassembler_->Disassemble(code, address);
        }
    }

    LOG_INFO("Navigated to 0x{:X}", address);
}

void Application::NavigateBack() {
    if (CanNavigateBack()) {
        history_index_--;
        NavigateToAddress(address_history_[history_index_], false);
    }
}

void Application::NavigateForward() {
    if (CanNavigateForward()) {
        history_index_++;
        NavigateToAddress(address_history_[history_index_], false);
    }
}

void Application::DumpModule(uint64_t base_address, uint32_t size, const std::string& filename) {
    if (!dma_ || !dma_->IsConnected() || selected_pid_ == 0) {
        LOG_ERROR("Cannot dump: no DMA connection or process selected");
        return;
    }

    LOG_INFO("Dumping module from 0x{:X} (size: 0x{:X}) to {}", base_address, size, filename);

    dump_in_progress_ = true;
    dump_progress_ = 0.0f;

    // Create PEDumper with our DMA read function
    auto read_func = [this](uint64_t addr, size_t read_size) -> std::vector<uint8_t> {
        return dma_->ReadMemory(selected_pid_, addr, read_size);
    };

    analysis::PEDumper dumper(read_func);

    // Configure dump options
    analysis::DumpOptions opts;
    opts.fix_headers = dump_fix_headers_;
    opts.rebuild_iat = dump_rebuild_iat_;
    opts.unmap_sections = dump_unmap_sections_;
    opts.file_alignment = 0x200;

    dump_progress_ = 0.3f;

    // Perform the dump
    auto dumped_data = dumper.Dump(base_address, opts);

    dump_progress_ = 0.8f;

    if (dumped_data.empty()) {
        LOG_ERROR("Failed to dump module: {}", dumper.GetLastError());
        dump_in_progress_ = false;
        return;
    }

    // Write to file
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        LOG_ERROR("Failed to open file for writing: {}", filename);
        dump_in_progress_ = false;
        return;
    }

    out.write(reinterpret_cast<const char*>(dumped_data.data()), dumped_data.size());
    out.close();

    dump_progress_ = 1.0f;
    dump_in_progress_ = false;

    LOG_INFO("Successfully dumped {} bytes to {}", dumped_data.size(), filename);
    status_message_ = "Module dumped successfully to " + std::string(filename);
}

void Application::RequestExit() {
    running_ = false;
}

void Application::GetWindowSize(int& width, int& height) const {
    if (window_) {
        glfwGetWindowSize(window_, &width, &height);
    } else {
        width = height = 0;
    }
}

bool Application::IsMinimized() const {
    if (window_) {
        int w, h;
        glfwGetWindowSize(window_, &w, &h);
        return w == 0 || h == 0;
    }
    return true;
}

void Application::RenderSettingsDialog() {
    ImGui::OpenPopup("Settings");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Settings", &show_settings_, ImGuiWindowFlags_NoResize)) {
        // Tabbed interface
        if (ImGui::BeginTabBar("SettingsTabs")) {
            // ===== MCP Tab =====
            if (ImGui::BeginTabItem("MCP Server")) {
                ImGui::Spacing();
                ImGui::TextWrapped("Model Context Protocol server provides REST API access for LLMs like Claude to interact with Orpheus.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (!mcp_config_) {
                    mcp_config_ = std::make_unique<mcp::MCPConfig>();
                    // Try to load existing config
                    mcp::MCPServer::LoadConfig(*mcp_config_);
                }

                if (ImGui::Checkbox("Enable MCP Server", &mcp_config_->enabled)) {
                    mcp_config_dirty_ = true;
                }
                ImGui::SameLine();
                ImGui::Button("?");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Starts an HTTP server that allows external tools to interact with Orpheus");
                }

                ImGui::Spacing();

                // Port configuration
                ImGui::Text("Server Port:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                int port = static_cast<int>(mcp_config_->port);
                if (ImGui::InputInt("##port", &port, 0, 0)) {
                    if (port > 0 && port <= 65535) {
                        mcp_config_->port = static_cast<uint16_t>(port);
                        mcp_config_dirty_ = true;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(default: 8765)");

                ImGui::Spacing();

                // Bind address configuration
                ImGui::Text("Bind Address:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                char bind_addr_input[128];
                strncpy(bind_addr_input, mcp_config_->bind_address.c_str(), sizeof(bind_addr_input) - 1);
                bind_addr_input[sizeof(bind_addr_input) - 1] = '\0';
                if (ImGui::InputText("##bind_addr", bind_addr_input, sizeof(bind_addr_input))) {
                    mcp_config_->bind_address = bind_addr_input;
                    mcp_config_dirty_ = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Localhost")) {
                    mcp_config_->bind_address = "127.0.0.1";
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Secure: Only accessible from this machine");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("All Interfaces")) {
                    mcp_config_->bind_address = "0.0.0.0";
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Listen on all network interfaces (allows remote access)");
                }

                ImGui::Spacing();

                // Authentication
                if (ImGui::Checkbox("Require Authentication", &mcp_config_->require_auth)) {
                    mcp_config_dirty_ = true;
                }
                ImGui::SameLine();
                ImGui::Button("?##auth");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Require Bearer token for API access");
                }

                if (mcp_config_->require_auth) {
                    ImGui::Spacing();
                    ImGui::Text("API Key:");
                    ImGui::SameLine();

                    // Show masked API key
                    std::string masked_key = mcp_config_->api_key.empty() ?
                        "<not generated>" :
                        (mcp_config_->api_key.substr(0, 8) + "..." + mcp_config_->api_key.substr(mcp_config_->api_key.length() - 8));
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", masked_key.c_str());

                    ImGui::SameLine();
                    if (ImGui::Button("Generate New Key")) {
                        mcp_config_->api_key = mcp::MCPServer::GenerateApiKey();
                        mcp::MCPServer::SaveConfig(*mcp_config_);
                        LOG_INFO("Generated new MCP API key");
                    }

                    if (!mcp_config_->api_key.empty()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Copy to Clipboard")) {
                            ImGui::SetClipboardText(mcp_config_->api_key.c_str());
                            LOG_INFO("API key copied to clipboard");
                        }
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Feature permissions header
                ImGui::Text("Feature Permissions:");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Control which MCP tool endpoints are enabled.\nGreen = enabled, Red = disabled");
                }

                // Quick toggle buttons
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220);
                if (ImGui::SmallButton("Safe Defaults")) {
                    mcp_config_->allow_read = true;
                    mcp_config_->allow_write = false;  // Keep write disabled
                    mcp_config_->allow_scan = true;
                    mcp_config_->allow_dump = true;
                    mcp_config_->allow_disasm = true;
                    mcp_config_->allow_emu = true;
                    mcp_config_->allow_rtti = true;
                    mcp_config_->allow_cs2_schema = true;
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enable all safe features (write stays OFF)");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Enable All")) {
                    mcp_config_->allow_read = true;
                    mcp_config_->allow_write = true;
                    mcp_config_->allow_scan = true;
                    mcp_config_->allow_dump = true;
                    mcp_config_->allow_disasm = true;
                    mcp_config_->allow_emu = true;
                    mcp_config_->allow_rtti = true;
                    mcp_config_->allow_cs2_schema = true;
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enable ALL features including write");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Disable All")) {
                    mcp_config_->allow_read = false;
                    mcp_config_->allow_write = false;
                    mcp_config_->allow_scan = false;
                    mcp_config_->allow_dump = false;
                    mcp_config_->allow_disasm = false;
                    mcp_config_->allow_emu = false;
                    mcp_config_->allow_rtti = false;
                    mcp_config_->allow_cs2_schema = false;
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Disable all features");
                }
                ImGui::Spacing();

                // Helper lambda for permission row with status indicator
                auto RenderPermissionRow = [&](const char* label, bool* enabled, const char* tooltip, bool dangerous = false) {
                    // Status indicator
                    if (*enabled) {
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[ON]");
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[OFF]");
                    }
                    ImGui::SameLine();

                    if (ImGui::Checkbox(label, enabled)) {
                        mcp_config_dirty_ = true;
                    }

                    if (dangerous) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "!");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("DANGEROUS - Use with caution!");
                        }
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tooltip);
                    }
                };

                // Memory Operations group
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
                ImGui::Text("Memory Operations");
                ImGui::PopStyleColor();
                ImGui::Indent(10.0f);
                RenderPermissionRow("Memory Read", &mcp_config_->allow_read,
                    "read_memory - Read raw bytes from process memory");
                RenderPermissionRow("Memory Write", &mcp_config_->allow_write,
                    "write_memory - Write bytes to process memory", true);
                RenderPermissionRow("Pointer Resolution", &mcp_config_->allow_read,
                    "resolve_pointer - Follow pointer chains (uses read)");
                ImGui::Unindent(10.0f);
                ImGui::Spacing();

                // Analysis Tools group
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
                ImGui::Text("Analysis Tools");
                ImGui::PopStyleColor();
                ImGui::Indent(10.0f);
                RenderPermissionRow("Pattern/String Scan", &mcp_config_->allow_scan,
                    "scan_pattern, scan_strings, find_xrefs - Memory scanning operations");
                RenderPermissionRow("Disassembly", &mcp_config_->allow_disasm,
                    "disassemble - x64 instruction disassembly");
                RenderPermissionRow("Module Dumping", &mcp_config_->allow_dump,
                    "dump_module - Dump modules to disk");
                ImGui::Unindent(10.0f);
                ImGui::Spacing();

                // Advanced Features group
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
                ImGui::Text("Advanced Features");
                ImGui::PopStyleColor();
                ImGui::Indent(10.0f);
                RenderPermissionRow("Emulation", &mcp_config_->allow_emu,
                    "emu_* - Unicorn x64 CPU emulation for decryption/analysis");
                RenderPermissionRow("RTTI Analysis", &mcp_config_->allow_rtti,
                    "rtti_* - MSVC RTTI parsing (vtables, class hierarchy, inheritance)");
                RenderPermissionRow("CS2 Schema", &mcp_config_->allow_cs2_schema,
                    "cs2_schema_* - Counter-Strike 2 schema dumping (class offsets, fields)");
                ImGui::Unindent(10.0f);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Permission summary
                int enabled_count = 0;
                if (mcp_config_->allow_read) enabled_count++;
                if (mcp_config_->allow_write) enabled_count++;
                if (mcp_config_->allow_scan) enabled_count++;
                if (mcp_config_->allow_dump) enabled_count++;
                if (mcp_config_->allow_disasm) enabled_count++;
                if (mcp_config_->allow_emu) enabled_count++;
                if (mcp_config_->allow_rtti) enabled_count++;
                if (mcp_config_->allow_cs2_schema) enabled_count++;

                ImVec4 summary_color = enabled_count == 8 ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) :
                                       enabled_count == 0 ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) :
                                                           ImVec4(0.9f, 0.8f, 0.3f, 1.0f);
                ImGui::TextColored(summary_color, "Features: %d/8 enabled", enabled_count);
                if (mcp_config_->allow_write) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "(Write enabled!)");
                }

                ImGui::Spacing();

                // Server status
                bool is_running = mcp_server_ && mcp_server_->IsRunning();
                ImGui::Text("Server Status:");
                ImGui::SameLine();
                if (is_running) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "RUNNING");
                    ImGui::SameLine();
                    if (ImGui::Button("Stop Server")) {
                        if (mcp_server_) {
                            mcp_server_->Stop();
                            LOG_INFO("MCP server stopped");
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "STOPPED");
                    ImGui::SameLine();
                    if (ImGui::Button("Start Server")) {
                        if (!mcp_server_) {
                            mcp_server_ = std::make_unique<mcp::MCPServer>(this);
                        }
                        if (mcp_config_->require_auth && mcp_config_->api_key.empty()) {
                            mcp_config_->api_key = mcp::MCPServer::GenerateApiKey();
                            LOG_INFO("Auto-generated MCP API key");
                        }
                        if (mcp_server_->Start(*mcp_config_)) {
                            LOG_INFO("MCP server started on port {}", mcp_config_->port);
                        } else {
                            LOG_ERROR("Failed to start MCP server");
                        }
                    }
                }

                if (is_running) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                        "Listening on all interfaces (0.0.0.0:%d)", mcp_config_->port);
                    ImGui::TextDisabled("Accessible via http://localhost:%d or http://<your-ip>:%d",
                        mcp_config_->port, mcp_config_->port);
                }

                ImGui::EndTabItem();
            }

            // ===== Appearance Tab =====
            if (ImGui::BeginTabItem("Appearance")) {
                ImGui::Spacing();
                ImGui::Text("UI Customization");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Theme selector
                ImGui::Text("Theme:");
                ImGui::SameLine();
                const char* themes[] = { "Dark", "Light", "Nord", "Dracula", "Catppuccin Mocha" };
                int theme_index = static_cast<int>(current_theme_);
                ImGui::SetNextItemWidth(200);
                if (ImGui::Combo("##theme", &theme_index, themes, IM_ARRAYSIZE(themes))) {
                    current_theme_ = static_cast<Theme>(theme_index);
                    ApplyTheme(current_theme_);
                }

                // Theme previews
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Dark: Classic dark theme with grey tones");
                    ImGui::Text("Light: Bright theme for well-lit environments");
                    ImGui::Text("Nord: Stylish dark theme with blue accents");
                    ImGui::Text("Dracula: Dark theme with purple/pink accents");
                    ImGui::Text("Catppuccin Mocha: Warm pastel dark theme");
                    ImGui::EndTooltip();
                }

                ImGui::Spacing();
                ImGui::Spacing();

                // Font size
                ImGui::Text("Font Size:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("##fontsize", &font_size_, 12.0f, 20.0f, "%.0f px")) {
                    theme_changed_ = true;  // Mark that font size changed
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply##fontsize")) {
                    if (theme_changed_) {
                        pending_font_rebuild_ = true;  // Defer to next frame start
                        status_message_ = "Font size updated";
                        status_timer_ = 2.0f;
                        theme_changed_ = false;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Adjust font size and click Apply to update");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Quick theme buttons
                ImGui::Text("Quick Select:");
                ImGui::SameLine();
                if (ImGui::Button("Dark")) {
                    current_theme_ = Theme::Dark;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Light")) {
                    current_theme_ = Theme::Light;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Nord")) {
                    current_theme_ = Theme::Nord;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Dracula")) {
                    current_theme_ = Theme::Dracula;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Mocha")) {
                    current_theme_ = Theme::CatppuccinMocha;
                    ApplyTheme(current_theme_);
                }

                ImGui::Spacing();
                ImGui::Spacing();

                // Current theme indicator
                const char* theme_name = themes[static_cast<int>(current_theme_)];
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Active Theme: %s", theme_name);

                ImGui::EndTabItem();
            }

            // ===== General Tab =====
            if (ImGui::BeginTabItem("General")) {
                ImGui::Spacing();
                ImGui::Text("General Application Settings");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Auto-refresh settings
                ImGui::Text("Auto-Refresh");
                ImGui::Spacing();

                ImGui::Checkbox("Enable Live Refresh", &auto_refresh_enabled_);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Automatically refresh process and module lists at specified intervals");
                }

                ImGui::Spacing();

                ImGui::BeginDisabled(!auto_refresh_enabled_);

                ImGui::Text("Process List Interval:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("##proc_interval", &process_refresh_interval_, 0.5f, 10.0f, "%.1f sec");

                ImGui::Text("Module List Interval:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("##mod_interval", &module_refresh_interval_, 0.5f, 5.0f, "%.1f sec");

                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::TextDisabled("Lower intervals = more responsive, but higher DMA overhead");

                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Telemetry settings
                ImGui::Text("Telemetry");
                ImGui::Spacing();

                bool telemetry_enabled = Telemetry::Instance().IsEnabled();
                if (ImGui::Checkbox("Send anonymous usage data", &telemetry_enabled)) {
                    Telemetry::Instance().SetEnabled(telemetry_enabled);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Just want to see if this thing is actually useful!\n\n"
                        "Only sends: version, platform, session duration,\n"
                        "and approximate region (via Cloudflare).\n\n"
                        "No process info, memory data, or personal details."
                    );
                }

                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Memory cache settings
                ImGui::Text("Performance");
                ImGui::Spacing();

                if (dma_) {
                    bool cache_enabled = dma_->IsCacheEnabled();
                    if (ImGui::Checkbox("Enable DMA read cache", &cache_enabled)) {
                        dma_->SetCacheEnabled(cache_enabled);
                    }
                    if (ImGui::IsItemHovered()) {
                        auto stats = dma_->GetCacheStats();
                        ImGui::SetTooltip(
                            "Cache recently read memory pages to reduce DMA traffic.\n"
                            "Reduces reads by 50-80%% for repetitive access patterns.\n\n"
                            "Hit rate: %.1f%% (%llu hits, %llu misses)\n"
                            "Pages cached: %zu (%.1f KB)",
                            stats.HitRate() * 100.0,
                            stats.hits, stats.misses,
                            stats.current_pages,
                            stats.current_bytes / 1024.0
                        );
                    }
                } else {
                    ImGui::BeginDisabled();
                    bool dummy = false;
                    ImGui::Checkbox("Enable DMA read cache", &dummy);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Connect to DMA first");
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Bottom buttons
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            // Only save MCP config if it was modified
            if (mcp_config_ && mcp_config_dirty_) {
                mcp::MCPServer::SaveConfig(*mcp_config_);
                mcp_config_dirty_ = false;
            }
            show_settings_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::Shutdown() {
    LOG_INFO("Application::Shutdown() - cleaning up resources");

    // Stop MCP server first (may have active connections)
    if (mcp_server_) {
        LOG_INFO("Stopping MCP server...");
        mcp_server_->Stop();
        mcp_server_.reset();
    }
    mcp_config_.reset();

    if (memory_watcher_) {
        LOG_INFO("Stopping memory watcher...");
        memory_watcher_->StopAutoScan();
        memory_watcher_.reset();
    }

    disassembler_.reset();

    // Close DMA connection
    if (dma_) {
        LOG_INFO("Closing DMA connection...");
        dma_->Close();
        dma_.reset();
    }

    // Cleanup ImGui and GLFW last
    if (window_) {
        LOG_INFO("Cleaning up graphics context...");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }

    LOG_INFO("Application shutdown complete");
}

#ifdef PLATFORM_LINUX
void Application::RenderUdevPermissionDialog() {
    ImGui::OpenPopup("USB Permission Required");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("USB Permission Required", &show_udev_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "USB Device Access Denied");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Your FPGA device was detected but Orpheus doesn't have permission to access it.\n\n"
            "This can be fixed by installing a udev rule that grants access to your user."
        );
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Detected Device:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s:%s",
            udev_vendor_id_.c_str(), udev_product_id_.c_str());

        ImGui::Spacing();
        ImGui::TextWrapped("The following udev rule will be installed:");
        ImGui::Spacing();

        // Show the rule that will be installed
        std::string rule = "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"" + udev_vendor_id_ +
                          "\", ATTR{idProduct}==\"" + udev_product_id_ + "\", MODE=\"0666\"";
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("%s", rule.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static bool install_in_progress = false;
        static bool install_success = false;
        static std::string install_message;

        if (install_in_progress) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Installing...");
        } else if (!install_message.empty()) {
            if (install_success) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", install_message.c_str());
                ImGui::Spacing();
                ImGui::TextWrapped("Please unplug and replug your device, then try connecting again.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", install_message.c_str());
            }
        }

        ImGui::Spacing();

        if (!install_in_progress && install_message.empty()) {
            if (ImGui::Button("Install udev Rule", ImVec2(150, 0))) {
                install_in_progress = true;

                // Create the udev rule - use single quotes in shell to preserve double quotes
                std::string rule_escaped = "SUBSYSTEM==\\\"usb\\\", ATTR{idVendor}==\\\"" + udev_vendor_id_ +
                    "\\\", ATTR{idProduct}==\\\"" + udev_product_id_ + "\\\", MODE=\\\"0666\\\"";

                // Use pkexec to install the rule with elevated privileges
                std::string cmd = "pkexec sh -c 'printf \"%s\\n\" \"# Orpheus FPGA DMA device\" \"" +
                    rule_escaped + "\" > /etc/udev/rules.d/99-orpheus-fpga.rules && udevadm control --reload-rules && udevadm trigger'";

                int result = system(cmd.c_str());
                install_in_progress = false;

                if (result == 0) {
                    install_success = true;
                    install_message = "udev rule installed successfully!";
                    LOG_INFO("Installed udev rule for device {}:{}", udev_vendor_id_, udev_product_id_);
                } else {
                    install_success = false;
                    install_message = "Failed to install udev rule. You may need to install it manually.";
                    LOG_ERROR("Failed to install udev rule, exit code: {}", result);
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Close", ImVec2(80, 0))) {
            show_udev_dialog_ = false;
            install_message.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
#endif

// ============================================================================
// CFG Viewer
// ============================================================================

void Application::RenderCFGViewer() {
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("CFG Viewer", &panels_.cfg_viewer);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No process selected");
        ImGui::End();
        return;
    }

    // Initialize CFG builder if needed
    if (!cfg_builder_) {
        bool is_64bit = true;  // Assume x64 for now
        cfg_builder_ = std::make_unique<analysis::CFGBuilder>(
            [this](uint64_t addr, size_t size) {
                return dma_->ReadMemory(selected_pid_, addr, size);
            },
            is_64bit
        );
    }

    // Address input toolbar
    ImGui::Text("Function:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    bool address_entered = ImGui::InputText("##cfg_addr", cfg_address_input_, sizeof(cfg_address_input_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (ImGui::Button("Build CFG") || address_entered) {
        uint64_t addr = strtoull(cfg_address_input_, nullptr, 16);
        if (addr != 0) {
            cfg_function_addr_ = addr;
            auto new_cfg = std::make_unique<analysis::ControlFlowGraph>(
                cfg_builder_->BuildCFG(addr)
            );
            if (!new_cfg->nodes.empty()) {
                cfg_builder_->ComputeLayout(*new_cfg);
                cfg_ = std::move(new_cfg);
                cfg_selected_node_ = 0;
                cfg_needs_layout_ = false;
                LOG_INFO("Built CFG for 0x{:X}: {} nodes, {} edges",
                         addr, cfg_->node_count, cfg_->edge_count);
            } else {
                LOG_WARN("Failed to build CFG at 0x{:X}", addr);
            }
        }
    }

    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("##zoom", &cfg_zoom_, 0.25f, 2.0f, "%.2fx");

    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        cfg_scroll_x_ = 0;
        cfg_scroll_y_ = 0;
        cfg_zoom_ = 1.0f;
    }

    // CFG info
    if (cfg_ && !cfg_->nodes.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %u nodes, %u edges%s",
                           cfg_->node_count, cfg_->edge_count,
                           cfg_->has_loops ? ", has loops" : "");
    }

    ImGui::Separator();

    // Graph canvas
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 50) canvas_size.x = 50;
    if (canvas_size.y < 50) canvas_size.y = 50;

    // Draw canvas background
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                            IM_COL32(30, 30, 35, 255));
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                      IM_COL32(60, 60, 70, 255));

    // Handle mouse input for panning
    ImGui::InvisibleButton("cfg_canvas", canvas_size);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        cfg_scroll_x_ += delta.x;
        cfg_scroll_y_ += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // Handle zoom with scroll wheel
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            float old_zoom = cfg_zoom_;
            cfg_zoom_ += wheel * 0.1f;
            cfg_zoom_ = std::clamp(cfg_zoom_, 0.25f, 2.0f);

            // Zoom towards mouse position
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 mouse_canvas = {mouse_pos.x - canvas_pos.x - cfg_scroll_x_,
                                   mouse_pos.y - canvas_pos.y - cfg_scroll_y_};
            float zoom_factor = cfg_zoom_ / old_zoom;
            cfg_scroll_x_ -= mouse_canvas.x * (zoom_factor - 1.0f);
            cfg_scroll_y_ -= mouse_canvas.y * (zoom_factor - 1.0f);
        }
    }

    // Clip to canvas
    draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

    if (cfg_ && !cfg_->nodes.empty()) {
        // Colors
        ImU32 col_node_normal = IM_COL32(50, 60, 80, 255);
        ImU32 col_node_entry = IM_COL32(40, 100, 60, 255);
        ImU32 col_node_exit = IM_COL32(100, 50, 50, 255);
        ImU32 col_node_call = IM_COL32(80, 70, 50, 255);
        ImU32 col_node_cond = IM_COL32(70, 50, 90, 255);
        ImU32 col_node_loop = IM_COL32(100, 80, 40, 255);
        ImU32 col_node_selected = IM_COL32(100, 150, 200, 255);
        ImU32 col_node_border = IM_COL32(100, 110, 130, 255);
        ImU32 col_edge_fallthrough = IM_COL32(100, 150, 100, 200);
        ImU32 col_edge_branch = IM_COL32(150, 100, 100, 200);
        ImU32 col_edge_unconditional = IM_COL32(100, 100, 150, 200);
        ImU32 col_edge_backedge = IM_COL32(200, 100, 50, 200);
        ImU32 col_text = IM_COL32(220, 220, 220, 255);
        ImU32 col_text_addr = IM_COL32(150, 200, 255, 255);

        // Draw edges first (under nodes)
        for (const auto& edge : cfg_->edges) {
            auto from_it = cfg_->nodes.find(edge.from);
            auto to_it = cfg_->nodes.find(edge.to);
            if (from_it == cfg_->nodes.end() || to_it == cfg_->nodes.end()) continue;

            const auto& from_node = from_it->second;
            const auto& to_node = to_it->second;

            // Calculate edge positions
            float from_x = canvas_pos.x + cfg_scroll_x_ + (from_node.x + from_node.width / 2) * cfg_zoom_;
            float from_y = canvas_pos.y + cfg_scroll_y_ + (from_node.y + from_node.height) * cfg_zoom_;
            float to_x = canvas_pos.x + cfg_scroll_x_ + (to_node.x + to_node.width / 2) * cfg_zoom_;
            float to_y = canvas_pos.y + cfg_scroll_y_ + to_node.y * cfg_zoom_;

            // Select edge color
            ImU32 edge_col = col_edge_fallthrough;
            if (edge.is_back_edge) {
                edge_col = col_edge_backedge;
            } else {
                switch (edge.type) {
                    case analysis::CFGEdge::Type::Branch:
                        edge_col = col_edge_branch;
                        break;
                    case analysis::CFGEdge::Type::Unconditional:
                        edge_col = col_edge_unconditional;
                        break;
                    default:
                        edge_col = col_edge_fallthrough;
                        break;
                }
            }

            // Draw edge with curve for back edges
            if (edge.is_back_edge) {
                // Draw curved back edge on the right side
                float ctrl_x = std::max(from_x, to_x) + 50 * cfg_zoom_;
                float mid_y = (from_y + to_y) / 2;
                draw_list->AddBezierCubic(
                    ImVec2(from_x, from_y),
                    ImVec2(ctrl_x, from_y),
                    ImVec2(ctrl_x, to_y),
                    ImVec2(to_x, to_y),
                    edge_col, 2.0f * cfg_zoom_
                );
            } else {
                // Draw straight edge with slight curve
                float mid_y = (from_y + to_y) / 2;
                draw_list->AddBezierCubic(
                    ImVec2(from_x, from_y),
                    ImVec2(from_x, mid_y),
                    ImVec2(to_x, mid_y),
                    ImVec2(to_x, to_y),
                    edge_col, 1.5f * cfg_zoom_
                );
            }

            // Draw arrowhead
            float arrow_size = 8 * cfg_zoom_;
            ImVec2 arrow_tip(to_x, to_y);
            ImVec2 arrow_left(to_x - arrow_size * 0.5f, to_y - arrow_size);
            ImVec2 arrow_right(to_x + arrow_size * 0.5f, to_y - arrow_size);
            draw_list->AddTriangleFilled(arrow_tip, arrow_left, arrow_right, edge_col);
        }

        // Draw nodes
        for (const auto& [addr, node] : cfg_->nodes) {
            float x = canvas_pos.x + cfg_scroll_x_ + node.x * cfg_zoom_;
            float y = canvas_pos.y + cfg_scroll_y_ + node.y * cfg_zoom_;
            float w = node.width * cfg_zoom_;
            float h = node.height * cfg_zoom_;

            // Skip if outside visible area
            if (x + w < canvas_pos.x || x > canvas_pos.x + canvas_size.x ||
                y + h < canvas_pos.y || y > canvas_pos.y + canvas_size.y) {
                continue;
            }

            // Select node color based on type
            ImU32 node_col = col_node_normal;
            if (addr == cfg_selected_node_) {
                node_col = col_node_selected;
            } else if (node.is_loop_header) {
                node_col = col_node_loop;
            } else {
                switch (node.type) {
                    case analysis::CFGNode::Type::Entry:
                        node_col = col_node_entry;
                        break;
                    case analysis::CFGNode::Type::Exit:
                        node_col = col_node_exit;
                        break;
                    case analysis::CFGNode::Type::Call:
                        node_col = col_node_call;
                        break;
                    case analysis::CFGNode::Type::ConditionalJump:
                        node_col = col_node_cond;
                        break;
                    default:
                        break;
                }
            }

            // Draw node background
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), node_col, 4.0f * cfg_zoom_);
            draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), col_node_border, 4.0f * cfg_zoom_);

            // Draw address header
            char addr_buf[32];
            snprintf(addr_buf, sizeof(addr_buf), "%llX", (unsigned long long)addr);
            float text_scale = cfg_zoom_ * 0.8f;
            if (text_scale > 0.4f) {
                ImVec2 text_pos(x + 5 * cfg_zoom_, y + 3 * cfg_zoom_);
                draw_list->AddText(nullptr, 12 * cfg_zoom_, text_pos, col_text_addr, addr_buf);
            }

            // Draw instruction count
            if (text_scale > 0.5f) {
                char info_buf[64];
                snprintf(info_buf, sizeof(info_buf), "%zu instr",
                        node.instructions.size());
                ImVec2 info_pos(x + 5 * cfg_zoom_, y + 18 * cfg_zoom_);
                draw_list->AddText(nullptr, 10 * cfg_zoom_, info_pos, col_text, info_buf);
            }

            // Handle click on node
            ImVec2 mouse_pos = ImGui::GetMousePos();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                mouse_pos.x >= x && mouse_pos.x <= x + w &&
                mouse_pos.y >= y && mouse_pos.y <= y + h) {
                cfg_selected_node_ = addr;
            }
        }
    } else {
        // No CFG loaded
        const char* hint = "Enter a function address and click 'Build CFG' to visualize control flow";
        ImVec2 text_size = ImGui::CalcTextSize(hint);
        ImVec2 text_pos(canvas_pos.x + (canvas_size.x - text_size.x) / 2,
                       canvas_pos.y + (canvas_size.y - text_size.y) / 2);
        draw_list->AddText(text_pos, IM_COL32(100, 100, 100, 200), hint);
    }

    draw_list->PopClipRect();

    // Selected node details panel (if a node is selected)
    if (cfg_ && cfg_selected_node_ != 0) {
        auto it = cfg_->nodes.find(cfg_selected_node_);
        if (it != cfg_->nodes.end()) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::Separator();

            const auto& node = it->second;
            ImGui::Text("Selected: 0x%llX - 0x%llX (%u bytes, %zu instructions)",
                       (unsigned long long)node.address,
                       (unsigned long long)node.end_address,
                       node.size,
                       node.instructions.size());

            // Show first few instructions
            if (!node.instructions.empty()) {
                ImGui::BeginChild("node_instrs", ImVec2(0, 100), true);
                for (size_t i = 0; i < std::min(node.instructions.size(), (size_t)10); i++) {
                    const auto& instr = node.instructions[i];
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%llX:",
                                      (unsigned long long)instr.address);
                    ImGui::SameLine();
                    ImGui::Text("%s", instr.full_text.c_str());
                }
                if (node.instructions.size() > 10) {
                    ImGui::TextDisabled("... %zu more", node.instructions.size() - 10);
                }
                ImGui::EndChild();
            }

            // Buttons
            if (ImGui::Button("Go to Address")) {
                NavigateToAddress(node.address);
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect")) {
                cfg_selected_node_ = 0;
            }
        }
    }

    ImGui::End();
}

// ============================================================================
// CS2 Radar Panel
// ============================================================================

bool Application::LoadRadarMap(const std::string& map_name) {
    // Unload any existing map
    UnloadRadarMap();

    // Try embedded resources first
    if constexpr (orpheus::embedded::has_maps) {
        const auto* map_res = orpheus::embedded::GetMapResource(map_name);
        if (map_res) {
            // Parse info from embedded data
            std::string info_str(reinterpret_cast<const char*>(map_res->info_data), map_res->info_size);
            std::istringstream info_stream(info_str);
            std::string line;
            while (std::getline(info_stream, line)) {
                if (line.find("pos_x") != std::string::npos) {
                    size_t quote1 = line.find_last_of("\"");
                    if (quote1 != std::string::npos) {
                        size_t quote2 = line.rfind("\"", quote1 - 1);
                        if (quote2 != std::string::npos) {
                            radar_map_.pos_x = std::stof(line.substr(quote2 + 1, quote1 - quote2 - 1));
                        }
                    }
                } else if (line.find("pos_y") != std::string::npos) {
                    size_t quote1 = line.find_last_of("\"");
                    if (quote1 != std::string::npos) {
                        size_t quote2 = line.rfind("\"", quote1 - 1);
                        if (quote2 != std::string::npos) {
                            radar_map_.pos_y = std::stof(line.substr(quote2 + 1, quote1 - quote2 - 1));
                        }
                    }
                } else if (line.find("scale") != std::string::npos) {
                    size_t quote1 = line.find_last_of("\"");
                    if (quote1 != std::string::npos) {
                        size_t quote2 = line.rfind("\"", quote1 - 1);
                        if (quote2 != std::string::npos) {
                            radar_map_.scale = std::stof(line.substr(quote2 + 1, quote1 - quote2 - 1));
                        }
                    }
                }
            }
            LOG_INFO("Map info loaded from embedded: pos_x={}, pos_y={}, scale={}",
                     radar_map_.pos_x, radar_map_.pos_y, radar_map_.scale);

            // Load radar image from embedded data
            int width, height, channels;
            unsigned char* pixels = stbi_load_from_memory(
                map_res->radar_data, static_cast<int>(map_res->radar_size),
                &width, &height, &channels, 4);

            if (pixels) {
                // Create OpenGL texture
                GLuint texture_id;
                glGenTextures(1, &texture_id);
                glBindTexture(GL_TEXTURE_2D, texture_id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

                stbi_image_free(pixels);

                radar_map_.texture_id = texture_id;
                radar_map_.texture_width = width;
                radar_map_.texture_height = height;
                radar_map_.name = map_name;
                radar_map_.loaded = true;

                LOG_INFO("Radar map loaded from embedded: {} ({}x{})", map_name, width, height);
                return true;
            }
        }
    }

    // Fall back to filesystem loading
    namespace fs = std::filesystem;
    fs::path resources_dir = RuntimeManager::Instance().GetResourceDirectory();
    fs::path map_dir = resources_dir / "maps" / map_name;

    // Look for radar image
    fs::path radar_image;
    for (const auto& filename : {"radar.png", "radar.jpg", "radar_psd.png"}) {
        fs::path candidate = map_dir / filename;
        if (fs::exists(candidate)) {
            radar_image = candidate;
            break;
        }
    }

    if (radar_image.empty()) {
        LOG_WARN("No radar image found for map: {} (looked in {})", map_name, map_dir.string());
        return false;
    }

    // Load map info from info.txt
    fs::path info_path = map_dir / "info.txt";
    if (fs::exists(info_path)) {
        std::ifstream info_file(info_path);
        std::string line;
        while (std::getline(info_file, line)) {
            if (line.find("pos_x") != std::string::npos) {
                size_t quote1 = line.find_last_of("\"");
                if (quote1 != std::string::npos) {
                    size_t quote2 = line.rfind("\"", quote1 - 1);
                    if (quote2 != std::string::npos) {
                        radar_map_.pos_x = std::stof(line.substr(quote2 + 1, quote1 - quote2 - 1));
                    }
                }
            } else if (line.find("pos_y") != std::string::npos) {
                size_t quote1 = line.find_last_of("\"");
                if (quote1 != std::string::npos) {
                    size_t quote2 = line.rfind("\"", quote1 - 1);
                    if (quote2 != std::string::npos) {
                        radar_map_.pos_y = std::stof(line.substr(quote2 + 1, quote1 - quote2 - 1));
                    }
                }
            } else if (line.find("scale") != std::string::npos) {
                size_t quote1 = line.find_last_of("\"");
                if (quote1 != std::string::npos) {
                    size_t quote2 = line.rfind("\"", quote1 - 1);
                    if (quote2 != std::string::npos) {
                        radar_map_.scale = std::stof(line.substr(quote2 + 1, quote1 - quote2 - 1));
                    }
                }
            }
        }
        LOG_INFO("Map info loaded: pos_x={}, pos_y={}, scale={}",
                 radar_map_.pos_x, radar_map_.pos_y, radar_map_.scale);
    } else {
        LOG_WARN("No info.txt found for map: {}, using defaults", map_name);
        radar_map_.pos_x = -2048;
        radar_map_.pos_y = 2048;
        radar_map_.scale = 4.0f;
    }

    // Load the radar image texture
    int width, height, channels;
    unsigned char* pixels = stbi_load(radar_image.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        LOG_ERROR("Failed to load radar image: {}", radar_image.string());
        return false;
    }

    // Create OpenGL texture
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    stbi_image_free(pixels);

    radar_map_.texture_id = texture_id;
    radar_map_.texture_width = width;
    radar_map_.texture_height = height;
    radar_map_.name = map_name;
    radar_map_.loaded = true;

    LOG_INFO("Radar map loaded: {} ({}x{})", map_name, width, height);
    return true;
}

void Application::UnloadRadarMap() {
    if (radar_map_.texture_id != 0) {
        glDeleteTextures(1, &radar_map_.texture_id);
        radar_map_.texture_id = 0;
    }
    radar_map_.loaded = false;
    radar_map_.name.clear();
}

void Application::RefreshRadarData() {
    if (!dma_ || !dma_->IsConnected() || selected_pid_ == 0) {
        radar_players_.clear();
        return;
    }

    // Check if CS2 entity system is initialized
    if (!cs2_entity_initialized_ || cs2_entity_system_ == 0) {
        radar_players_.clear();
        return;
    }

    // Auto-detect current map from CS2 memory
    if (radar_auto_detect_map_ && radar_map_name_addr_ != 0) {
        // Read map name string pointer
        auto map_ptr_data = dma_->ReadMemory(selected_pid_, radar_map_name_addr_, 8);
        if (map_ptr_data.size() >= 8) {
            uint64_t map_str_ptr;
            std::memcpy(&map_str_ptr, map_ptr_data.data(), 8);
            if (map_str_ptr != 0) {
                auto map_name_data = dma_->ReadMemory(selected_pid_, map_str_ptr, 64);
                if (!map_name_data.empty()) {
                    std::string full_path(reinterpret_cast<char*>(map_name_data.data()));
                    // Extract map name from path like "maps/de_dust2"
                    size_t slash_pos = full_path.rfind('/');
                    std::string map_name = (slash_pos != std::string::npos)
                        ? full_path.substr(slash_pos + 1)
                        : full_path;

                    // Remove any file extension
                    size_t dot_pos = map_name.rfind('.');
                    if (dot_pos != std::string::npos) {
                        map_name = map_name.substr(0, dot_pos);
                    }

                    // Only update if map changed
                    if (!map_name.empty() && map_name != radar_detected_map_) {
                        radar_detected_map_ = map_name;
                        LOG_INFO("Detected map: {}", map_name);

                        // Auto-load the map if it's different from current
                        if (radar_current_map_ != map_name) {
                            radar_current_map_ = map_name;
                            if (!LoadRadarMap(map_name)) {
                                LOG_WARN("No radar image for map: {}", map_name);
                            }
                        }
                    }
                }
            }
        }
    }

    radar_players_.clear();

    // Verified offsets from HandleCS2ListPlayers
    constexpr uint32_t OFFSET_PLAYER_NAME = 0x6E8;
    constexpr uint32_t OFFSET_TEAM_NUM = 0x3EB;
    constexpr uint32_t OFFSET_PAWN_HANDLE = 0x8FC;
    constexpr uint32_t OFFSET_PAWN_IS_ALIVE = 0x904;
    constexpr uint32_t OFFSET_PAWN_HEALTH = 0x908;
    constexpr uint32_t OFFSET_CONNECTED = 0x6E4;
    constexpr uint32_t OFFSET_IS_LOCAL = 0x778;
    constexpr uint32_t OFFSET_SCENE_NODE = 0x330;
    constexpr uint32_t OFFSET_ABS_ORIGIN = 0xD0;
    constexpr uint32_t OFFSET_SPOTTED_STATE = 0x2700;
    constexpr uint32_t OFFSET_SPOTTED = 0x08;

    // Read chunk 0 pointer
    auto chunk0_data = dma_->ReadMemory(selected_pid_, cs2_entity_system_ + 0x10, 8);
    if (chunk0_data.size() < 8) return;

    uint64_t chunk0_ptr;
    std::memcpy(&chunk0_ptr, chunk0_data.data(), 8);
    uint64_t chunk0_base = chunk0_ptr & ~0xFULL;
    if (chunk0_base == 0) return;

    // Iterate player controller indices 1-64
    for (int idx = 1; idx <= 64; idx++) {
        uint64_t entry_addr = chunk0_base + 0x08 + idx * 0x70;
        auto controller_data = dma_->ReadMemory(selected_pid_, entry_addr, 8);
        if (controller_data.size() < 8) continue;

        uint64_t controller;
        std::memcpy(&controller, controller_data.data(), 8);
        if (controller == 0 || controller < 0x10000000000ULL) continue;

        // Read connection state
        auto connected_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_CONNECTED, 4);
        if (connected_data.size() < 4) continue;
        uint32_t connected;
        std::memcpy(&connected, connected_data.data(), 4);
        if (connected > 2) continue;  // Skip disconnected

        // Read player name
        auto name_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PLAYER_NAME, 64);
        if (name_data.empty()) continue;
        std::string name(reinterpret_cast<char*>(name_data.data()));
        if (name.empty()) continue;

        RadarPlayer player;
        player.name = name;

        // Read team
        auto team_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_TEAM_NUM, 1);
        if (!team_data.empty()) player.team = team_data[0];

        // Read alive status and health
        auto alive_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_IS_ALIVE, 1);
        if (!alive_data.empty()) player.is_alive = alive_data[0] != 0;

        auto health_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_HEALTH, 4);
        if (health_data.size() >= 4) {
            std::memcpy(&player.health, health_data.data(), 4);
        }

        // Read is local
        auto local_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_IS_LOCAL, 1);
        if (!local_data.empty()) player.is_local = local_data[0] != 0;

        // Read position from pawn if alive
        if (player.is_alive) {
            auto pawn_handle_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_HANDLE, 4);
            if (pawn_handle_data.size() >= 4) {
                uint32_t pawn_handle;
                std::memcpy(&pawn_handle, pawn_handle_data.data(), 4);
                int pawn_index = pawn_handle & 0x7FFF;

                // Resolve pawn from entity list
                int chunk_idx = pawn_index / 512;
                int slot = pawn_index % 512;

                auto pawn_chunk_data = dma_->ReadMemory(selected_pid_,
                    cs2_entity_system_ + 0x10 + chunk_idx * 8, 8);
                if (pawn_chunk_data.size() >= 8) {
                    uint64_t pawn_chunk;
                    std::memcpy(&pawn_chunk, pawn_chunk_data.data(), 8);
                    pawn_chunk &= ~0xFULL;

                    if (pawn_chunk != 0) {
                        auto pawn_data = dma_->ReadMemory(selected_pid_,
                            pawn_chunk + 0x08 + slot * 0x70, 8);
                        if (pawn_data.size() >= 8) {
                            uint64_t pawn;
                            std::memcpy(&pawn, pawn_data.data(), 8);

                            if (pawn != 0) {
                                // Read scene node
                                auto scene_node_data = dma_->ReadMemory(selected_pid_,
                                    pawn + OFFSET_SCENE_NODE, 8);
                                if (scene_node_data.size() >= 8) {
                                    uint64_t scene_node;
                                    std::memcpy(&scene_node, scene_node_data.data(), 8);

                                    if (scene_node != 0) {
                                        // Read position
                                        auto pos_data = dma_->ReadMemory(selected_pid_,
                                            scene_node + OFFSET_ABS_ORIGIN, 12);
                                        if (pos_data.size() >= 12) {
                                            std::memcpy(&player.x, pos_data.data(), 4);
                                            std::memcpy(&player.y, pos_data.data() + 4, 4);
                                            std::memcpy(&player.z, pos_data.data() + 8, 4);
                                        }
                                    }
                                }

                                // Read spotted state
                                auto spotted_data = dma_->ReadMemory(selected_pid_,
                                    pawn + OFFSET_SPOTTED_STATE + OFFSET_SPOTTED, 1);
                                if (!spotted_data.empty()) {
                                    player.is_spotted = spotted_data[0] != 0;
                                }
                            }
                        }
                    }
                }
            }
        }

        radar_players_.push_back(player);
    }
}

ImVec2 Application::WorldToRadar(float world_x, float world_y,
                                  const ImVec2& canvas_pos, const ImVec2& canvas_size) {
    if (!radar_map_.loaded || radar_map_.scale == 0) {
        return ImVec2(0, 0);
    }

    // Transform world coordinates to radar image coordinates
    // radar_x = (world_x - pos_x) / scale
    // radar_y = (pos_y - world_y) / scale  (Y is inverted)
    float radar_x = (world_x - radar_map_.pos_x) / radar_map_.scale;
    float radar_y = (radar_map_.pos_y - world_y) / radar_map_.scale;

    // Normalize to 0-1 based on image dimensions
    float norm_x = radar_x / radar_map_.texture_width;
    float norm_y = radar_y / radar_map_.texture_height;

    // Apply zoom and scroll, then map to canvas
    float final_x = canvas_pos.x + radar_scroll_x_ + norm_x * canvas_size.x * radar_zoom_;
    float final_y = canvas_pos.y + radar_scroll_y_ + norm_y * canvas_size.y * radar_zoom_;

    return ImVec2(final_x, final_y);
}

void Application::RenderCS2Radar() {
    ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Radar", &panels_.cs2_radar);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No process selected");
        ImGui::End();
        return;
    }

    // Toolbar - Row 1: Map selection
    ImGui::Text("Map:");
    ImGui::SameLine();

    // Auto-detect checkbox
    ImGui::Checkbox("Auto", &radar_auto_detect_map_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-detect current map from CS2 memory");
    }
    ImGui::SameLine();

    // Manual dropdown (disabled when auto-detect is on)
    static const char* maps[] = { "de_dust2", "de_mirage", "de_inferno", "de_nuke",
                                   "de_overpass", "de_ancient", "de_anubis", "de_vertigo",
                                   "cs_office", "ar_shoots" };

    // Find current map index for dropdown
    int current_map_idx = 0;
    for (int i = 0; i < IM_ARRAYSIZE(maps); i++) {
        if (radar_current_map_ == maps[i]) {
            current_map_idx = i;
            break;
        }
    }

    ImGui::BeginDisabled(radar_auto_detect_map_);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("##map", &current_map_idx, maps, IM_ARRAYSIZE(maps))) {
        radar_current_map_ = maps[current_map_idx];
        LoadRadarMap(maps[current_map_idx]);
    }
    ImGui::EndDisabled();

    // Show detected map if auto-detect is on and map is detected
    if (radar_auto_detect_map_ && !radar_detected_map_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "(%s)", radar_detected_map_.c_str());
    }

    // Row 2: Options
    ImGui::Checkbox("Center on local", &radar_center_on_local_);
    ImGui::SameLine();
    ImGui::Checkbox("Names", &radar_show_names_);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-refresh", &radar_auto_refresh_);

    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("##radar_zoom", &radar_zoom_, 0.5f, 3.0f, "%.1fx");

    // Auto-refresh timer
    if (radar_auto_refresh_) {
        radar_refresh_timer_ += ImGui::GetIO().DeltaTime;
        if (radar_refresh_timer_ >= radar_refresh_interval_) {
            RefreshRadarData();
            radar_refresh_timer_ = 0.0f;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        RefreshRadarData();
    }

    ImGui::Separator();

    // Radar canvas
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    // Make it square
    float min_dim = std::min(canvas_size.x, canvas_size.y);
    canvas_size = ImVec2(min_dim, min_dim);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background
    draw_list->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(20, 25, 30, 255));

    // Draw map image if loaded
    if (radar_map_.loaded && radar_map_.texture_id != 0) {
        // Calculate image bounds with zoom and scroll
        float img_w = canvas_size.x * radar_zoom_;
        float img_h = canvas_size.y * radar_zoom_;

        ImVec2 img_min(canvas_pos.x + radar_scroll_x_, canvas_pos.y + radar_scroll_y_);
        ImVec2 img_max(img_min.x + img_w, img_min.y + img_h);

        // Clip to canvas
        draw_list->PushClipRect(canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

        // Draw the map image
        draw_list->AddImage(
            (ImTextureID)(intptr_t)radar_map_.texture_id,
            img_min, img_max,
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, 200)
        );

        // Center on local player if enabled
        if (radar_center_on_local_ && !radar_players_.empty()) {
            for (const auto& player : radar_players_) {
                if (player.is_local && player.is_alive) {
                    // Calculate where local player would be on the image
                    float radar_x = (player.x - radar_map_.pos_x) / radar_map_.scale;
                    float radar_y = (radar_map_.pos_y - player.y) / radar_map_.scale;
                    float norm_x = radar_x / radar_map_.texture_width;
                    float norm_y = radar_y / radar_map_.texture_height;

                    // Center the view
                    radar_scroll_x_ = canvas_size.x / 2 - norm_x * img_w;
                    radar_scroll_y_ = canvas_size.y / 2 - norm_y * img_h;
                    break;
                }
            }
        }

        // Draw players
        for (const auto& player : radar_players_) {
            if (!player.is_alive) continue;

            ImVec2 pos = WorldToRadar(player.x, player.y, canvas_pos, canvas_size);

            // Skip if outside canvas
            if (pos.x < canvas_pos.x || pos.x > canvas_pos.x + canvas_size.x ||
                pos.y < canvas_pos.y || pos.y > canvas_pos.y + canvas_size.y) {
                continue;
            }

            // Choose color based on team
            ImU32 color;
            if (player.is_local) {
                color = IM_COL32(50, 200, 50, 255);  // Green for local
            } else if (player.team == 2) {
                color = IM_COL32(220, 150, 50, 255);  // Orange/yellow for T
            } else if (player.team == 3) {
                color = IM_COL32(80, 150, 220, 255);  // Blue for CT
            } else {
                color = IM_COL32(150, 150, 150, 255);  // Gray for unknown
            }

            // Draw player dot
            float radius = player.is_local ? 8.0f : 6.0f;
            draw_list->AddCircleFilled(pos, radius * radar_zoom_, color);
            draw_list->AddCircle(pos, radius * radar_zoom_, IM_COL32(255, 255, 255, 150), 12, 1.5f);

            // Draw name
            if (radar_show_names_) {
                ImVec2 text_pos(pos.x + 10, pos.y - 6);
                draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 200), player.name.c_str());
            }
        }

        draw_list->PopClipRect();
    } else {
        // No map loaded - show placeholder
        const char* hint = "Select a map from the dropdown";
        ImVec2 text_size = ImGui::CalcTextSize(hint);
        draw_list->AddText(
            ImVec2(canvas_pos.x + (canvas_size.x - text_size.x) / 2,
                   canvas_pos.y + (canvas_size.y - text_size.y) / 2),
            IM_COL32(100, 100, 100, 200), hint
        );

        // Still draw players without map background
        draw_list->PushClipRect(canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

        for (const auto& player : radar_players_) {
            if (!player.is_alive) continue;

            // Simple mapping without map info (just use raw coordinates scaled)
            float norm_x = (player.x + 4096) / 8192.0f;  // Rough estimate
            float norm_y = (4096 - player.y) / 8192.0f;
            ImVec2 pos(canvas_pos.x + norm_x * canvas_size.x,
                       canvas_pos.y + norm_y * canvas_size.y);

            ImU32 color = player.is_local ? IM_COL32(50, 200, 50, 255) :
                          (player.team == 2 ? IM_COL32(220, 150, 50, 255) :
                                              IM_COL32(80, 150, 220, 255));
            draw_list->AddCircleFilled(pos, 6.0f, color);

            if (radar_show_names_) {
                draw_list->AddText(ImVec2(pos.x + 10, pos.y - 6),
                                  IM_COL32(255, 255, 255, 200), player.name.c_str());
            }
        }

        draw_list->PopClipRect();
    }

    // Handle mouse input for panning
    ImGui::InvisibleButton("radar_canvas", canvas_size);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        radar_center_on_local_ = false;  // Disable centering when user pans
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        radar_scroll_x_ += delta.x;
        radar_scroll_y_ += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // Handle zoom with scroll wheel
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            radar_center_on_local_ = false;
            float old_zoom = radar_zoom_;
            radar_zoom_ += wheel * 0.2f;
            radar_zoom_ = std::clamp(radar_zoom_, 0.5f, 3.0f);

            // Zoom towards mouse position
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 mouse_canvas(mouse_pos.x - canvas_pos.x - radar_scroll_x_,
                                mouse_pos.y - canvas_pos.y - radar_scroll_y_);
            float zoom_factor = radar_zoom_ / old_zoom;
            radar_scroll_x_ -= mouse_canvas.x * (zoom_factor - 1.0f);
            radar_scroll_y_ -= mouse_canvas.y * (zoom_factor - 1.0f);
        }
    }

    // Border
    draw_list->AddRect(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(60, 65, 75, 255));

    // Player count
    int alive_count = 0;
    for (const auto& p : radar_players_) if (p.is_alive) alive_count++;
    ImGui::Text("Players: %zu (%d alive)", radar_players_.size(), alive_count);

    ImGui::End();
}

// ============================================================================
// CS2 Player Dashboard
// ============================================================================

void Application::RenderCS2Dashboard() {
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Dashboard", &panels_.cs2_dashboard);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No process selected");
        ImGui::End();
        return;
    }

    // Refresh data if empty or using radar's data
    if (radar_players_.empty()) {
        RefreshRadarData();
    }

    // Toolbar
    ImGui::Checkbox("Show all players", &dashboard_show_all_players_);
    ImGui::SameLine();
    ImGui::Checkbox("Show bots", &dashboard_show_bots_);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        RefreshRadarData();
    }

    ImGui::Separator();

    // Split into two teams
    std::vector<const RadarPlayer*> team_t, team_ct, spectators;
    const RadarPlayer* local_player = nullptr;

    for (const auto& player : radar_players_) {
        if (player.is_local) local_player = &player;
        if (player.team == 2) team_t.push_back(&player);
        else if (player.team == 3) team_ct.push_back(&player);
        else spectators.push_back(&player);
    }

    // Local player info box
    if (local_player) {
        ImGui::BeginChild("local_player", ImVec2(0, 80), true);

        // Name and team
        ImVec4 team_color = local_player->team == 2 ?
            ImVec4(0.9f, 0.6f, 0.2f, 1.0f) : ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
        ImGui::TextColored(team_color, "%s", local_player->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(You)");

        // Health bar
        float health_pct = local_player->health / 100.0f;
        ImVec4 health_color = health_pct > 0.5f ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                              health_pct > 0.25f ? ImVec4(0.9f, 0.7f, 0.1f, 1.0f) :
                                                   ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

        ImGui::Text("Health:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, health_color);
        ImGui::ProgressBar(health_pct, ImVec2(150, 18),
                          std::to_string(local_player->health).c_str());
        ImGui::PopStyleColor();

        // Position
        ImGui::Text("Position: %.0f, %.0f, %.0f",
                   local_player->x, local_player->y, local_player->z);

        ImGui::EndChild();
    }

    // Team tables
    float half_width = ImGui::GetContentRegionAvail().x / 2 - 5;

    // Terrorists
    ImGui::BeginChild("team_t", ImVec2(half_width, 0), true);
    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "TERRORISTS (%zu)", team_t.size());
    ImGui::Separator();

    for (const auto* player : team_t) {
        if (!dashboard_show_all_players_ && !player->is_alive) continue;

        // Player row
        ImGui::PushID(player->name.c_str());

        // Alive indicator
        if (player->is_alive) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "*");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "X");
        }
        ImGui::SameLine();

        // Name (highlight if local)
        if (player->is_local) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", player->name.c_str());
        } else {
            ImGui::Text("%s", player->name.c_str());
        }

        // Health bar on same line
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        float hp = player->health / 100.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            hp > 0.5f ? ImVec4(0.2f, 0.7f, 0.2f, 1.0f) :
            hp > 0.25f ? ImVec4(0.8f, 0.6f, 0.1f, 1.0f) :
                         ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::ProgressBar(hp, ImVec2(55, 14), "");
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Counter-Terrorists
    ImGui::BeginChild("team_ct", ImVec2(half_width, 0), true);
    ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "COUNTER-TERRORISTS (%zu)", team_ct.size());
    ImGui::Separator();

    for (const auto* player : team_ct) {
        if (!dashboard_show_all_players_ && !player->is_alive) continue;

        ImGui::PushID(player->name.c_str());

        if (player->is_alive) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "*");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "X");
        }
        ImGui::SameLine();

        if (player->is_local) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", player->name.c_str());
        } else {
            ImGui::Text("%s", player->name.c_str());
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        float hp = player->health / 100.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            hp > 0.5f ? ImVec4(0.2f, 0.7f, 0.2f, 1.0f) :
            hp > 0.25f ? ImVec4(0.8f, 0.6f, 0.1f, 1.0f) :
                         ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::ProgressBar(hp, ImVec2(55, 14), "");
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::End();
}

void Application::RenderMemoryRegions() {
    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory Regions", &panels_.memory_regions);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No process selected");
        ImGui::End();
        return;
    }

    // Refresh regions if PID changed
    if (memory_regions_pid_ != selected_pid_) {
        cached_memory_regions_ = dma_->GetMemoryRegions(selected_pid_);
        memory_regions_pid_ = selected_pid_;
    }

    // Toolbar
    ImGui::Text("Process: %s (%zu regions)", selected_process_name_.c_str(), cached_memory_regions_.size());
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        cached_memory_regions_ = dma_->GetMemoryRegions(selected_pid_);
    }

    // Filter input
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##regionfilter", "Filter by protection, type, or info...",
                             memory_regions_filter_, sizeof(memory_regions_filter_));

    ImGui::Separator();

    // Table with memory regions
    if (ImGui::BeginTable("##MemoryRegionsTable", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {

        ImGui::TableSetupColumn("Base Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 150.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Protection", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                memory_regions_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                memory_regions_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Build filtered list
        std::string filter_str = memory_regions_filter_;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        std::vector<size_t> filtered_indices;
        for (size_t i = 0; i < cached_memory_regions_.size(); i++) {
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                const auto& region = cached_memory_regions_[i];
                std::string protection_lower = region.protection;
                std::string type_lower = region.type;
                std::string info_lower = region.info;
                std::transform(protection_lower.begin(), protection_lower.end(), protection_lower.begin(), ::tolower);
                std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);
                std::transform(info_lower.begin(), info_lower.end(), info_lower.begin(), ::tolower);

                if (protection_lower.find(filter_str) != std::string::npos ||
                    type_lower.find(filter_str) != std::string::npos ||
                    info_lower.find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                }
            }
        }

        // Sort filtered indices
        auto& regions = cached_memory_regions_;
        int sort_col = memory_regions_sort_column_;
        bool ascending = memory_regions_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&regions, sort_col, ascending](size_t a, size_t b) {
                const auto& ra = regions[a];
                const auto& rb = regions[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0: cmp = (ra.base_address < rb.base_address) ? -1 : (ra.base_address > rb.base_address) ? 1 : 0; break;
                    case 1: cmp = (ra.size < rb.size) ? -1 : (ra.size > rb.size) ? 1 : 0; break;
                    case 2: cmp = ra.protection.compare(rb.protection); break;
                    case 3: cmp = ra.type.compare(rb.type); break;
                    case 4: cmp = ra.info.compare(rb.info); break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Render sorted list with clipper
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& region = cached_memory_regions_[filtered_indices[row]];

                ImGui::TableNextRow();

                // Base Address column
                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)region.base_address);
                if (ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_SpanAllColumns)) {
                    NavigateToAddress(region.base_address);
                }

                // Context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    if (ImGui::MenuItem("View in Memory")) {
                        memory_address_ = region.base_address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)region.base_address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, region.base_address, 512);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem("View in Disassembly")) {
                        disasm_address_ = region.base_address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)region.base_address);
                        auto data = dma_->ReadMemory(selected_pid_, region.base_address, 4096);
                        if (!data.empty() && disassembler_) {
                            disasm_instructions_ = disassembler_->Disassemble(data, region.base_address);
                        }
                        panels_.disassembly = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Size")) {
                        char size_buf[32];
                        snprintf(size_buf, sizeof(size_buf), "0x%llX", (unsigned long long)region.size);
                        ImGui::SetClipboardText(size_buf);
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                // Size column
                ImGui::TableNextColumn();
                // Format size in a readable way
                if (region.size >= 1024 * 1024 * 1024) {
                    ImGui::Text("%.2f GB", region.size / (1024.0 * 1024.0 * 1024.0));
                } else if (region.size >= 1024 * 1024) {
                    ImGui::Text("%.2f MB", region.size / (1024.0 * 1024.0));
                } else if (region.size >= 1024) {
                    ImGui::Text("%.2f KB", region.size / 1024.0);
                } else {
                    ImGui::Text("%llu B", (unsigned long long)region.size);
                }

                // Protection column with color coding
                ImGui::TableNextColumn();
                ImVec4 prot_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);  // Default gray
                if (region.protection.find("RWX") != std::string::npos ||
                    region.protection.find("EXECUTE_READWRITE") != std::string::npos) {
                    prot_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red for RWX (suspicious)
                } else if (region.protection.find("RX") != std::string::npos ||
                           region.protection.find("EXECUTE") != std::string::npos) {
                    prot_color = ImVec4(0.3f, 0.8f, 0.3f, 1.0f);  // Green for executable
                } else if (region.protection.find("RW") != std::string::npos ||
                           region.protection.find("READWRITE") != std::string::npos) {
                    prot_color = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);  // Blue for read-write
                }
                ImGui::TextColored(prot_color, "%s", region.protection.c_str());

                // Type column
                ImGui::TableNextColumn();
                ImGui::Text("%s", region.type.c_str());

                // Info column
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", region.info.c_str());
            }
        }

        ImGui::EndTable();
    }

    // Summary statistics at the bottom
    ImGui::Separator();
    uint64_t total_size = 0;
    uint64_t executable_size = 0;
    uint64_t writable_size = 0;
    for (const auto& region : cached_memory_regions_) {
        total_size += region.size;
        if (region.protection.find("X") != std::string::npos ||
            region.protection.find("EXECUTE") != std::string::npos) {
            executable_size += region.size;
        }
        if (region.protection.find("W") != std::string::npos ||
            region.protection.find("WRITE") != std::string::npos) {
            writable_size += region.size;
        }
    }
    ImGui::Text("Total: %.2f MB | Executable: %.2f MB | Writable: %.2f MB",
                total_size / (1024.0 * 1024.0),
                executable_size / (1024.0 * 1024.0),
                writable_size / (1024.0 * 1024.0));

    ImGui::End();
}

void Application::RenderXRefFinder() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("XRef Finder", &panels_.xref_finder);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No process selected");
        ImGui::End();
        return;
    }

    // Target address input
    ImGui::Text("Find cross-references to:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##xref_target", "Target address (hex)", xref_target_input_, sizeof(xref_target_input_),
                              ImGuiInputTextFlags_CharsHexadecimal);

    ImGui::Separator();

    // Scan range selection
    ImGui::Text("Scan Range:");
    ImGui::Checkbox("Use selected module", &xref_use_module_);

    if (xref_use_module_) {
        // Module-based scanning
        if (selected_module_base_ != 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", selected_module_name_.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("@ 0x%llX (0x%X bytes)", (unsigned long long)selected_module_base_, selected_module_size_);
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No module selected");
        }
    } else {
        // Custom range
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##xref_base", "Base address (hex)", xref_base_input_, sizeof(xref_base_input_),
                                  ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputTextWithHint("##xref_size", "Size (hex)", xref_size_input_, sizeof(xref_size_input_),
                                  ImGuiInputTextFlags_CharsHexadecimal);
    }

    ImGui::Separator();

    // Scan button
    bool can_scan = false;
    if (xref_use_module_) {
        can_scan = selected_module_base_ != 0 && strlen(xref_target_input_) > 0;
    } else {
        can_scan = strlen(xref_target_input_) > 0 && strlen(xref_base_input_) > 0 && strlen(xref_size_input_) > 0;
    }

    if (!can_scan || xref_scanning_) ImGui::BeginDisabled();
    if (ImGui::Button("Find XRefs")) {
        xref_scanning_ = true;
        xref_results_.clear();

        uint64_t target = strtoull(xref_target_input_, nullptr, 16);
        uint64_t base = 0;
        uint32_t size = 0;

        if (xref_use_module_) {
            base = selected_module_base_;
            size = selected_module_size_;
        } else {
            base = strtoull(xref_base_input_, nullptr, 16);
            size = static_cast<uint32_t>(strtoull(xref_size_input_, nullptr, 16));
        }

        if (target != 0 && base != 0 && size != 0) {
            auto data = dma_->ReadMemory(selected_pid_, base, size);
            if (!data.empty()) {
                // Scan for direct 64-bit pointer references
                for (size_t i = 0; i + 8 <= data.size() && xref_results_.size() < 1000; i++) {
                    uint64_t val = *reinterpret_cast<uint64_t*>(&data[i]);
                    if (val == target) {
                        XRefResult ref;
                        ref.address = base + i;
                        ref.type = "ptr64";

                        // Build context (module+offset)
                        bool found_module = false;
                        for (const auto& mod : cached_modules_) {
                            if (ref.address >= mod.base_address && ref.address < mod.base_address + mod.size) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s+0x%llX", mod.name.c_str(),
                                         (unsigned long long)(ref.address - mod.base_address));
                                ref.context = buf;
                                found_module = true;
                                break;
                            }
                        }
                        if (!found_module) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)ref.address);
                            ref.context = buf;
                        }

                        xref_results_.push_back(ref);
                    }
                }

                // Scan for 32-bit relative offsets (RIP-relative)
                for (size_t i = 0; i + 4 <= data.size() && xref_results_.size() < 1000; i++) {
                    int32_t rel = *reinterpret_cast<int32_t*>(&data[i]);
                    uint64_t computed = base + i + 4 + rel;  // RIP + 4 + offset
                    if (computed == target) {
                        XRefResult ref;
                        ref.address = base + i;
                        ref.type = "rel32";

                        // Build context (module+offset)
                        bool found_module = false;
                        for (const auto& mod : cached_modules_) {
                            if (ref.address >= mod.base_address && ref.address < mod.base_address + mod.size) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s+0x%llX", mod.name.c_str(),
                                         (unsigned long long)(ref.address - mod.base_address));
                                ref.context = buf;
                                found_module = true;
                                break;
                            }
                        }
                        if (!found_module) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)ref.address);
                            ref.context = buf;
                        }

                        xref_results_.push_back(ref);
                    }
                }

                LOG_INFO("XRef scan found {} references to 0x{:X}", xref_results_.size(), target);
            }
        }

        xref_scanning_ = false;
    }
    if (!can_scan || xref_scanning_) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        xref_results_.clear();
    }

    // Results header
    ImGui::Separator();
    ImGui::Text("Results: %zu", xref_results_.size());

    // Results table
    if (ImGui::BeginTable("##XRefResults", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)xref_results_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& ref = xref_results_[row];

                ImGui::TableNextRow();
                ImGui::PushID(row);

                // Address column - clickable
                ImGui::TableNextColumn();
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)ref.address);
                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(ref.address);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("View in Disassembly")) {
                        disasm_address_ = ref.address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)ref.address);
                        auto code = dma_->ReadMemory(selected_pid_, ref.address, 1024);
                        if (!code.empty() && disassembler_) {
                            disasm_instructions_ = disassembler_->Disassemble(code, ref.address);
                        }
                        panels_.disassembly = true;
                    }
                    if (ImGui::MenuItem("View in Memory")) {
                        memory_address_ = ref.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)ref.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, ref.address, 256);
                        panels_.memory_viewer = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy Address")) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    if (ImGui::MenuItem("Copy Context")) {
                        ImGui::SetClipboardText(ref.context.c_str());
                    }
                    ImGui::EndPopup();
                }

                // Type column
                ImGui::TableNextColumn();
                ImVec4 type_color = ref.type == "ptr64" ?
                    ImVec4(0.5f, 0.8f, 1.0f, 1.0f) :  // Blue for ptr64
                    ImVec4(1.0f, 0.8f, 0.5f, 1.0f);   // Orange for rel32
                ImGui::TextColored(type_color, "%s", ref.type.c_str());

                // Context column
                ImGui::TableNextColumn();
                ImGui::Text("%s", ref.context.c_str());

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void Application::RenderPointerChain() {
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
    ImGui::Begin("Pointer Chain Resolver", &panels_.pointer_chain);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a process first");
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Follow pointer chains to find dynamic addresses");
    ImGui::Separator();
    ImGui::Spacing();

    // Input section
    ImGui::Text("Base Address:");
    ImGui::SameLine(120);
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##ptr_base", "e.g. 7FF600000000", pointer_base_input_, sizeof(pointer_base_input_));

    ImGui::Text("Offsets:");
    ImGui::SameLine(120);
    ImGui::SetNextItemWidth(350);
    ImGui::InputTextWithHint("##ptr_offsets", "e.g. 0x10, 0x20, 0x8 (comma-separated)", pointer_offsets_input_, sizeof(pointer_offsets_input_));

    ImGui::Spacing();

    // Resolve button
    if (ImGui::Button("Resolve Chain", ImVec2(120, 0))) {
        pointer_chain_results_.clear();
        pointer_chain_error_.clear();
        pointer_final_address_ = 0;

        // Parse base address
        uint64_t base = 0;
        try {
            std::string base_str = pointer_base_input_;
            // Remove 0x prefix if present
            if (base_str.length() > 2 && (base_str[0] == '0' && (base_str[1] == 'x' || base_str[1] == 'X'))) {
                base_str = base_str.substr(2);
            }
            base = std::stoull(base_str, nullptr, 16);
        } catch (...) {
            pointer_chain_error_ = "Invalid base address";
        }

        if (pointer_chain_error_.empty() && base == 0) {
            pointer_chain_error_ = "Base address cannot be 0";
        }

        // Parse offsets
        std::vector<int64_t> offsets;
        if (pointer_chain_error_.empty() && strlen(pointer_offsets_input_) > 0) {
            std::string offsets_str = pointer_offsets_input_;
            size_t pos = 0;
            while (pos < offsets_str.length()) {
                // Skip whitespace and commas
                while (pos < offsets_str.length() && (offsets_str[pos] == ' ' || offsets_str[pos] == ',')) {
                    pos++;
                }
                if (pos >= offsets_str.length()) break;

                // Find end of this offset
                size_t end = pos;
                while (end < offsets_str.length() && offsets_str[end] != ',' && offsets_str[end] != ' ') {
                    end++;
                }

                std::string off_str = offsets_str.substr(pos, end - pos);
                try {
                    // Handle negative offsets
                    bool negative = false;
                    if (!off_str.empty() && off_str[0] == '-') {
                        negative = true;
                        off_str = off_str.substr(1);
                    }
                    // Remove 0x prefix if present
                    if (off_str.length() > 2 && (off_str[0] == '0' && (off_str[1] == 'x' || off_str[1] == 'X'))) {
                        off_str = off_str.substr(2);
                    }
                    int64_t offset = static_cast<int64_t>(std::stoull(off_str, nullptr, 16));
                    if (negative) offset = -offset;
                    offsets.push_back(offset);
                } catch (...) {
                    pointer_chain_error_ = "Invalid offset: " + off_str;
                    break;
                }
                pos = end;
            }
        }

        // Resolve the chain
        if (pointer_chain_error_.empty()) {
            uint64_t current = base;
            pointer_chain_results_.push_back({current, 0});  // Base address (value filled later)

            for (size_t i = 0; i < offsets.size(); i++) {
                // Read pointer at current address
                auto ptr_opt = dma_->Read<uint64_t>(selected_pid_, current);
                if (!ptr_opt) {
                    pointer_chain_error_ = "Failed to read pointer at 0x" +
                        ([](uint64_t v) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%llX", (unsigned long long)v);
                            return std::string(buf);
                        })(current);
                    break;
                }

                uint64_t ptr_value = *ptr_opt;
                // Update the value for the current entry
                pointer_chain_results_.back().second = ptr_value;

                // Apply offset
                current = ptr_value + offsets[i];
                pointer_chain_results_.push_back({current, 0});
            }

            if (pointer_chain_error_.empty()) {
                pointer_final_address_ = current;
                // Read value at final address for display
                auto final_ptr = dma_->Read<uint64_t>(selected_pid_, current);
                if (final_ptr) {
                    pointer_chain_results_.back().second = *final_ptr;
                }
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(80, 0))) {
        pointer_chain_results_.clear();
        pointer_chain_error_.clear();
        pointer_final_address_ = 0;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enter a base address and comma-separated offsets.\n"
                          "The resolver will dereference at each step:\n"
                          "  [[base]+offset1]+offset2 = final address");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Error display
    if (!pointer_chain_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", pointer_chain_error_.c_str());
        ImGui::Spacing();
    }

    // Results display
    if (!pointer_chain_results_.empty()) {
        ImGui::Text("Chain Visualization:");
        ImGui::Separator();

        // Visual chain display
        ImGui::BeginChild("ChainViz", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);

        // Parse offsets again for display
        std::vector<int64_t> offsets;
        if (strlen(pointer_offsets_input_) > 0) {
            std::string offsets_str = pointer_offsets_input_;
            size_t pos = 0;
            while (pos < offsets_str.length()) {
                while (pos < offsets_str.length() && (offsets_str[pos] == ' ' || offsets_str[pos] == ',')) pos++;
                if (pos >= offsets_str.length()) break;
                size_t end = pos;
                while (end < offsets_str.length() && offsets_str[end] != ',' && offsets_str[end] != ' ') end++;
                std::string off_str = offsets_str.substr(pos, end - pos);
                try {
                    bool negative = false;
                    if (!off_str.empty() && off_str[0] == '-') {
                        negative = true;
                        off_str = off_str.substr(1);
                    }
                    if (off_str.length() > 2 && (off_str[0] == '0' && (off_str[1] == 'x' || off_str[1] == 'X'))) {
                        off_str = off_str.substr(2);
                    }
                    int64_t offset = static_cast<int64_t>(std::stoull(off_str, nullptr, 16));
                    if (negative) offset = -offset;
                    offsets.push_back(offset);
                } catch (...) {}
                pos = end;
            }
        }

        for (size_t i = 0; i < pointer_chain_results_.size(); i++) {
            const auto& [addr, value] = pointer_chain_results_[i];

            ImGui::PushID(static_cast<int>(i));

            // Step indicator
            if (i == 0) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Base");
            } else if (i == pointer_chain_results_.size() - 1) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Final");
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Step %zu", i);
            }

            ImGui::SameLine(60);

            // Address (clickable)
            char addr_buf[32];
            snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)addr);
            if (ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_None, ImVec2(140, 0))) {
                NavigateToAddress(addr);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to navigate to this address in Memory Viewer");
            }

            // Show dereferenced value (except for the last entry which shows the actual value at final address)
            if (i < pointer_chain_results_.size() - 1 && value != 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "->");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "[0x%llX]", (unsigned long long)value);

                // Show offset applied
                if (i < offsets.size()) {
                    ImGui::SameLine();
                    if (offsets[i] >= 0) {
                        ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "+ 0x%llX", (unsigned long long)offsets[i]);
                    } else {
                        ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.6f, 1.0f), "- 0x%llX", (unsigned long long)(-offsets[i]));
                    }
                }
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        // Final address section
        if (pointer_final_address_ != 0) {
            ImGui::Spacing();
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Final Address: 0x%llX", (unsigned long long)pointer_final_address_);

            // Copy button
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                char buf[32];
                snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)pointer_final_address_);
                ImGui::SetClipboardText(buf);
                status_message_ = "Address copied to clipboard";
                status_timer_ = 2.0f;
            }

            ImGui::Spacing();

            // Read final value as different types
            ImGui::Text("Read Final Value As:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            const char* type_names[] = { "Int32", "Float", "Int64", "Double" };
            ImGui::Combo("##ptr_type", &pointer_final_type_, type_names, 4);

            ImGui::SameLine();
            if (ImGui::Button("Read")) {
                switch (pointer_final_type_) {
                    case 0: {  // Int32
                        auto val = dma_->Read<int32_t>(selected_pid_, pointer_final_address_);
                        if (val) {
                            status_message_ = "Int32: " + std::to_string(*val);
                            status_timer_ = 5.0f;
                        }
                        break;
                    }
                    case 1: {  // Float
                        auto val = dma_->Read<float>(selected_pid_, pointer_final_address_);
                        if (val) {
                            status_message_ = "Float: " + std::to_string(*val);
                            status_timer_ = 5.0f;
                        }
                        break;
                    }
                    case 2: {  // Int64
                        auto val = dma_->Read<int64_t>(selected_pid_, pointer_final_address_);
                        if (val) {
                            status_message_ = "Int64: " + std::to_string(*val);
                            status_timer_ = 5.0f;
                        }
                        break;
                    }
                    case 3: {  // Double
                        auto val = dma_->Read<double>(selected_pid_, pointer_final_address_);
                        if (val) {
                            status_message_ = "Double: " + std::to_string(*val);
                            status_timer_ = 5.0f;
                        }
                        break;
                    }
                }
            }

            // Show last read value inline
            ImGui::Spacing();
            ImGui::BeginChild("FinalValueDisplay", ImVec2(0, 60), true);

            // Read and display values at final address
            auto data = dma_->ReadMemory(selected_pid_, pointer_final_address_, 8);
            if (!data.empty()) {
                if (data.size() >= 4) {
                    int32_t int32_val = *reinterpret_cast<int32_t*>(data.data());
                    float float_val = *reinterpret_cast<float*>(data.data());
                    ImGui::Text("Int32: %d  |  Float: %.6f", int32_val, float_val);
                }
                if (data.size() >= 8) {
                    int64_t int64_val = *reinterpret_cast<int64_t*>(data.data());
                    double double_val = *reinterpret_cast<double*>(data.data());
                    ImGui::Text("Int64: %lld  |  Double: %.6f", (long long)int64_val, double_val);

                    // Show as hex
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Hex: %02X %02X %02X %02X %02X %02X %02X %02X",
                        data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
                }
            }

            ImGui::EndChild();
        }
    } else if (pointer_chain_error_.empty()) {
        ImGui::TextDisabled("Enter a base address and offsets, then click 'Resolve Chain'");
    }

    ImGui::End();
}

void Application::RenderFunctionRecovery() {
    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Function Recovery", &panels_.function_recovery);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No process selected");
        ImGui::End();
        return;
    }

    // Check for async recovery completion
    if (function_recovery_running_ && function_recovery_future_.valid()) {
        auto status = function_recovery_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            auto functions = function_recovery_future_.get();
            recovered_functions_.clear();
            recovered_functions_.reserve(functions.size());
            for (auto& [addr, func] : functions) {
                recovered_functions_.push_back(std::move(func));
            }
            function_recovery_running_ = false;
            function_recovery_progress_ = 1.0f;
            function_recovery_progress_stage_ = "Complete";
            LOG_INFO("Function recovery complete: {} functions found in {}",
                     recovered_functions_.size(), function_recovery_module_name_);
        }
    }

    // Module selector dropdown
    ImGui::Text("Module:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);

    // Display current selection or prompt
    std::string combo_preview = function_recovery_module_name_.empty()
        ? "Select a module..."
        : function_recovery_module_name_;

    if (ImGui::BeginCombo("##ModuleSelector", combo_preview.c_str())) {
        for (const auto& mod : cached_modules_) {
            bool is_selected = (mod.base_address == function_recovery_module_base_);
            std::string label = mod.name + " (" + std::to_string(mod.size / 1024) + " KB)";

            if (ImGui::Selectable(label.c_str(), is_selected)) {
                function_recovery_module_base_ = mod.base_address;
                function_recovery_module_size_ = mod.size;
                function_recovery_module_name_ = mod.name;
                // Clear previous results when module changes
                recovered_functions_.clear();
                function_recovery_progress_ = 0.0f;
                function_recovery_progress_stage_.clear();
            }

            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Use selected module button
    ImGui::SameLine();
    if (selected_module_base_ != 0) {
        if (ImGui::Button("Use Selected")) {
            function_recovery_module_base_ = selected_module_base_;
            function_recovery_module_size_ = selected_module_size_;
            function_recovery_module_name_ = selected_module_name_;
            recovered_functions_.clear();
            function_recovery_progress_ = 0.0f;
            function_recovery_progress_stage_.clear();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use: %s @ 0x%llX", selected_module_name_.c_str(),
                              (unsigned long long)selected_module_base_);
        }
    }

    ImGui::Separator();

    // Recovery options
    ImGui::Text("Recovery Options:");
    ImGui::SameLine();
    ImGui::Checkbox("Prologues", &function_recovery_use_prologues_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scan for function prologues (push rbp, sub rsp, etc.)");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow calls", &function_recovery_follow_calls_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mark CALL instruction targets as function entry points");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Use .pdata", &function_recovery_use_pdata_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Parse PE exception directory for x64 function info");
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 180);

    // Recover button
    bool can_recover = function_recovery_module_base_ != 0 && !function_recovery_running_;
    if (!can_recover) ImGui::BeginDisabled();
    if (ImGui::Button("Recover Functions", ImVec2(160, 0))) {
        function_recovery_running_ = true;
        function_recovery_progress_ = 0.0f;
        function_recovery_progress_stage_ = "Starting...";
        recovered_functions_.clear();

        // Capture necessary state for the async lambda
        uint32_t pid = selected_pid_;
        uint64_t module_base = function_recovery_module_base_;
        uint32_t module_size = function_recovery_module_size_;
        bool use_prologues = function_recovery_use_prologues_;
        bool follow_calls = function_recovery_follow_calls_;
        bool use_pdata = function_recovery_use_pdata_;
        auto* dma = dma_.get();

        function_recovery_future_ = std::async(std::launch::async,
            [pid, module_base, module_size, use_prologues, follow_calls, use_pdata, dma,
             &progress_stage = function_recovery_progress_stage_,
             &progress = function_recovery_progress_]() -> std::map<uint64_t, analysis::FunctionInfo> {

            analysis::FunctionRecovery recovery(
                [dma, pid](uint64_t addr, size_t size) {
                    return dma->ReadMemory(pid, addr, size);
                },
                module_base,
                module_size,
                true  // is_64bit
            );

            analysis::FunctionRecoveryOptions opts;
            opts.use_prologues = use_prologues;
            opts.follow_calls = follow_calls;
            opts.use_exception_data = use_pdata;
            opts.max_functions = 100000;

            return recovery.RecoverFunctions(opts,
                [&progress_stage, &progress](const std::string& stage, float prog) {
                    progress_stage = stage;
                    progress = prog;
                });
        });
    }
    if (!can_recover) ImGui::EndDisabled();

    // Progress indicator
    if (function_recovery_running_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", function_recovery_progress_stage_.c_str());
        ImGui::ProgressBar(function_recovery_progress_, ImVec2(-1, 0));
    }

    ImGui::Separator();

    // Results section
    if (!recovered_functions_.empty()) {
        // Filter and stats
        ImGui::Text("Functions: %zu", recovered_functions_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##FuncFilter", "Filter by name or address...",
                                 function_filter_, sizeof(function_filter_));

        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Results")) {
            recovered_functions_.clear();
            function_recovery_progress_ = 0.0f;
            function_recovery_progress_stage_.clear();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Export CSV")) {
            std::string filename = function_recovery_module_name_ + "_functions.csv";
            std::ofstream out(filename);
            if (out.is_open()) {
                out << "Address,Size,Name,Source,Confidence,IsThunk,IsLeaf\n";
                for (const auto& func : recovered_functions_) {
                    out << "0x" << std::hex << func.entry_address << ","
                        << std::dec << func.size << ","
                        << "\"" << func.name << "\","
                        << func.GetSourceString() << ","
                        << func.confidence << ","
                        << (func.is_thunk ? "true" : "false") << ","
                        << (func.is_leaf ? "true" : "false") << "\n";
                }
                out.close();
                LOG_INFO("Exported functions to {}", filename);
            }
        }

        // Build filtered list
        std::string filter_str(function_filter_);
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        std::vector<size_t> filtered_indices;
        for (size_t i = 0; i < recovered_functions_.size(); i++) {
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                const auto& func = recovered_functions_[i];
                // Check name
                std::string name_lower = func.name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (name_lower.find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                    continue;
                }
                // Check address as hex
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "%llx", (unsigned long long)func.entry_address);
                if (std::string(addr_buf).find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                }
            }
        }

        ImGui::Text("Showing: %zu", filtered_indices.size());

        // Results table
        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        if (ImGui::BeginTable("##FunctionsTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit)) {

            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 140.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Handle sorting
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                    function_recovery_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                    function_recovery_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                    sort_specs->SpecsDirty = false;
                }
            }

            // Sort filtered indices
            auto& funcs = recovered_functions_;
            int sort_col = function_recovery_sort_column_;
            bool ascending = function_recovery_sort_ascending_;
            std::sort(filtered_indices.begin(), filtered_indices.end(),
                [&funcs, sort_col, ascending](size_t a, size_t b) {
                    const auto& fa = funcs[a];
                    const auto& fb = funcs[b];
                    int cmp = 0;
                    switch (sort_col) {
                        case 0: cmp = (fa.entry_address < fb.entry_address) ? -1 : (fa.entry_address > fb.entry_address) ? 1 : 0; break;
                        case 1: cmp = (fa.size < fb.size) ? -1 : (fa.size > fb.size) ? 1 : 0; break;
                        case 2: cmp = fa.name.compare(fb.name); break;
                        case 3: cmp = fa.GetSourceString().compare(fb.GetSourceString()); break;
                        case 4: {
                            // Sort by flags (thunk, leaf)
                            int flags_a = (fa.is_thunk ? 2 : 0) + (fa.is_leaf ? 1 : 0);
                            int flags_b = (fb.is_thunk ? 2 : 0) + (fb.is_leaf ? 1 : 0);
                            cmp = flags_a - flags_b;
                            break;
                        }
                    }
                    return ascending ? (cmp < 0) : (cmp > 0);
                });

            // Render with clipper
            ImGuiListClipper clipper;
            clipper.Begin((int)filtered_indices.size(), row_height);

            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const auto& func = recovered_functions_[filtered_indices[row]];

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);

                    // Address column
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)filtered_indices[row]);

                    char addr_buf[32];
                    snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)func.entry_address);

                    if (ImGui::Selectable(addr_buf, false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            // Double-click to decompile
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
                            decompile_address_ = func.entry_address;
                            snprintf(decompile_address_input_, sizeof(decompile_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            panels_.decompiler = true;
#else
                            // Navigate to disassembly
                            NavigateToAddress(func.entry_address);
#endif
                        }
                    }

                    // Context menu
                    if (ImGui::BeginPopupContextItem("##FuncContext")) {
                        if (ImGui::MenuItem("Copy Address")) {
                            ImGui::SetClipboardText(addr_buf);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("View in Disassembly")) {
                            disasm_address_ = func.entry_address;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            auto data = dma_->ReadMemory(selected_pid_, func.entry_address, 4096);
                            if (!data.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(data, func.entry_address);
                            }
                            panels_.disassembly = true;
                        }
                        if (ImGui::MenuItem("View in Memory")) {
                            memory_address_ = func.entry_address;
                            snprintf(address_input_, sizeof(address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            memory_data_ = dma_->ReadMemory(selected_pid_, func.entry_address, 512);
                            panels_.memory_viewer = true;
                        }
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
                        if (ImGui::MenuItem("Decompile")) {
                            decompile_address_ = func.entry_address;
                            snprintf(decompile_address_input_, sizeof(decompile_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            panels_.decompiler = true;
                        }
#endif
                        if (ImGui::MenuItem("Build CFG")) {
                            cfg_function_addr_ = func.entry_address;
                            snprintf(cfg_address_input_, sizeof(cfg_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            panels_.cfg_viewer = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Add Bookmark")) {
                            if (bookmarks_) {
                                std::string label = func.name.empty() ?
                                    "func_" + std::string(addr_buf) : func.name;
                                bookmarks_->Add(func.entry_address, label, "",
                                               "Functions", function_recovery_module_name_);
                            }
                        }
                        ImGui::EndPopup();
                    }

                    // Tooltip with details
                    if (ImGui::IsItemHovered()) {
                        uint64_t offset = func.entry_address - function_recovery_module_base_;
                        ImGui::BeginTooltip();
                        ImGui::Text("%s+0x%llX", function_recovery_module_name_.c_str(), (unsigned long long)offset);
                        if (!func.name.empty()) {
                            ImGui::Text("Name: %s", func.name.c_str());
                        }
                        ImGui::Text("Confidence: %.1f%%", func.confidence * 100.0f);
                        ImGui::Text("Instructions: %u, Blocks: %u", func.instruction_count, func.basic_block_count);
                        ImGui::TextDisabled("Double-click to decompile, Right-click for options");
                        ImGui::EndTooltip();
                    }

                    ImGui::PopID();

                    // Size column
                    ImGui::TableNextColumn();
                    if (func.size > 0) {
                        ImGui::Text("%u", func.size);
                    } else {
                        ImGui::TextDisabled("-");
                    }

                    // Name column
                    ImGui::TableNextColumn();
                    if (!func.name.empty()) {
                        ImGui::TextUnformatted(func.name.c_str());
                    } else {
                        ImGui::TextDisabled("(unnamed)");
                    }

                    // Source column with color coding
                    ImGui::TableNextColumn();
                    std::string src = func.GetSourceString();
                    ImVec4 src_color;
                    if (src == "pdata") {
                        src_color = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);  // Green - most reliable
                    } else if (src == "prologue") {
                        src_color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);  // Blue
                    } else if (src == "call_target") {
                        src_color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Yellow
                    } else if (src == "rtti") {
                        src_color = ImVec4(0.8f, 0.5f, 1.0f, 1.0f);  // Purple
                    } else {
                        src_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
                    }
                    ImGui::TextColored(src_color, "%s", src.c_str());

                    // Flags column
                    ImGui::TableNextColumn();
                    std::string flags;
                    if (func.is_thunk) flags += "T";
                    if (func.is_leaf) flags += "L";
                    if (!flags.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", flags.c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("T=Thunk (jump to another function)\nL=Leaf (no calls to other functions)");
                        }
                    } else {
                        ImGui::TextDisabled("-");
                    }
                }
            }

            ImGui::EndTable();
        }
    } else if (!function_recovery_running_ && function_recovery_module_base_ != 0) {
        ImGui::TextDisabled("Select a module and click 'Recover Functions' to discover functions");
    }

    ImGui::Separator();

    // Find Function Containing feature
    ImGui::Text("Find Function Containing Address:");
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("##ContainingAddr", function_containing_input_, sizeof(function_containing_input_),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        uint64_t target_addr = strtoull(function_containing_input_, nullptr, 16);
        if (target_addr != 0 && !recovered_functions_.empty()) {
            // Find the function containing this address
            function_containing_result_addr_ = 0;
            function_containing_result_name_.clear();

            // Sort functions by address for binary-search-like behavior
            std::vector<const analysis::FunctionInfo*> sorted_funcs;
            sorted_funcs.reserve(recovered_functions_.size());
            for (const auto& func : recovered_functions_) {
                sorted_funcs.push_back(&func);
            }
            std::sort(sorted_funcs.begin(), sorted_funcs.end(),
                [](const analysis::FunctionInfo* a, const analysis::FunctionInfo* b) {
                    return a->entry_address < b->entry_address;
                });

            // Find the largest entry_address <= target_addr
            for (auto it = sorted_funcs.rbegin(); it != sorted_funcs.rend(); ++it) {
                if ((*it)->entry_address <= target_addr) {
                    if ((*it)->size > 0 && target_addr >= (*it)->entry_address + (*it)->size) {
                        // Address is past the end of this function
                        continue;
                    }
                    function_containing_result_addr_ = (*it)->entry_address;
                    function_containing_result_name_ = (*it)->name;
                    break;
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Find")) {
        uint64_t target_addr = strtoull(function_containing_input_, nullptr, 16);
        if (target_addr != 0 && !recovered_functions_.empty()) {
            function_containing_result_addr_ = 0;
            function_containing_result_name_.clear();

            // Sort functions by address for binary-search-like behavior
            std::vector<const analysis::FunctionInfo*> sorted_funcs;
            sorted_funcs.reserve(recovered_functions_.size());
            for (const auto& func : recovered_functions_) {
                sorted_funcs.push_back(&func);
            }
            std::sort(sorted_funcs.begin(), sorted_funcs.end(),
                [](const analysis::FunctionInfo* a, const analysis::FunctionInfo* b) {
                    return a->entry_address < b->entry_address;
                });

            // Find the largest entry_address <= target_addr
            for (auto it = sorted_funcs.rbegin(); it != sorted_funcs.rend(); ++it) {
                if ((*it)->entry_address <= target_addr) {
                    if ((*it)->size > 0 && target_addr >= (*it)->entry_address + (*it)->size) {
                        // Address is past the end of this function
                        continue;
                    }
                    function_containing_result_addr_ = (*it)->entry_address;
                    function_containing_result_name_ = (*it)->name;
                    break;
                }
            }
        }
    }

    // Show result
    if (function_containing_result_addr_ != 0) {
        ImGui::SameLine();
        ImGui::Text("->");
        ImGui::SameLine();
        char result_buf[64];
        snprintf(result_buf, sizeof(result_buf), "0x%llX", (unsigned long long)function_containing_result_addr_);
        if (ImGui::SmallButton(result_buf)) {
            NavigateToAddress(function_containing_result_addr_);
        }
        if (!function_containing_result_name_.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "(%s)", function_containing_result_name_.c_str());
        }
        uint64_t target = strtoull(function_containing_input_, nullptr, 16);
        if (target > function_containing_result_addr_) {
            ImGui::SameLine();
            ImGui::TextDisabled("+0x%llX", (unsigned long long)(target - function_containing_result_addr_));
        }
    }

    ImGui::End();
}

void Application::RenderVTableReader() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("VTable Reader", &panels_.vtable_reader);

    if (!dma_ || !dma_->IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DMA not connected");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a process first");
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Read VTable entries and identify classes via RTTI");
    ImGui::Separator();
    ImGui::Spacing();

    // Input section
    ImGui::Text("VTable Address:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    bool enter_pressed = ImGui::InputTextWithHint("##vtable_addr", "e.g. 7FF600123456",
        vtable_address_input_, sizeof(vtable_address_input_),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Text("Entry Count:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("##vtable_count", &vtable_entry_count_);
    vtable_entry_count_ = std::max(1, std::min(vtable_entry_count_, 500));

    ImGui::SameLine();
    ImGui::Checkbox("Disassemble", &vtable_disasm_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show first instruction of each function");
    }

    ImGui::Spacing();

    // Read button
    if (ImGui::Button("Read VTable", ImVec2(120, 0)) || enter_pressed) {
        vtable_entries_.clear();
        vtable_class_name_.clear();
        vtable_error_.clear();

        uint64_t vtable_addr = 0;
        try {
            vtable_addr = std::stoull(vtable_address_input_, nullptr, 16);
        } catch (...) {
            vtable_error_ = "Invalid vtable address";
        }

        if (vtable_addr != 0) {
            // Read vtable entries
            size_t read_size = vtable_entry_count_ * 8;  // 8 bytes per pointer
            auto vtable_data = dma_->ReadMemory(selected_pid_, vtable_addr, read_size);

            if (vtable_data.empty()) {
                vtable_error_ = "Failed to read memory at vtable address";
            } else {
                // Parse entries
                for (int i = 0; i < vtable_entry_count_ && (size_t)((i + 1) * 8) <= vtable_data.size(); i++) {
                    VTableEntry entry;
                    entry.address = vtable_addr + i * 8;
                    entry.function = *reinterpret_cast<uint64_t*>(&vtable_data[i * 8]);

                    // Check if function pointer looks valid (non-zero, reasonable range)
                    entry.valid = (entry.function > 0x10000 && entry.function < 0x00007FFFFFFFFFFF);

                    // Build context (module+offset)
                    if (entry.valid) {
                        bool found_module = false;
                        for (const auto& mod : cached_modules_) {
                            if (entry.function >= mod.base_address && entry.function < mod.base_address + mod.size) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s+0x%llX", mod.name.c_str(),
                                         (unsigned long long)(entry.function - mod.base_address));
                                entry.context = buf;
                                found_module = true;
                                break;
                            }
                        }
                        if (!found_module) {
                            entry.context = "(outside modules)";
                        }

                        // Optionally disassemble first instruction
                        if (vtable_disasm_ && disassembler_) {
                            auto code = dma_->ReadMemory(selected_pid_, entry.function, 32);
                            if (!code.empty()) {
                                auto instrs = disassembler_->Disassemble(code, entry.function);
                                if (!instrs.empty()) {
                                    entry.first_instr = instrs[0].mnemonic + " " + instrs[0].operands;
                                }
                            }
                        }
                    }

                    vtable_entries_.push_back(entry);
                }

                // Try to identify class via RTTI (vtable[-1] points to RTTI type info)
                uint64_t rtti_ptr_addr = vtable_addr - 8;
                auto rtti_ptr_opt = dma_->Read<uint64_t>(selected_pid_, rtti_ptr_addr);
                if (rtti_ptr_opt && *rtti_ptr_opt != 0) {
                    // Parse RTTI - read type descriptor
                    uint64_t type_descriptor = *rtti_ptr_opt;

                    // In MSVC RTTI, the type name is at type_descriptor + 16 (after hash values)
                    // The name is a decorated string like ".?AVClassName@@"
                    auto name_data = dma_->ReadMemory(selected_pid_, type_descriptor + 16, 256);
                    if (!name_data.empty()) {
                        // Find null-terminated string
                        std::string decorated;
                        for (uint8_t c : name_data) {
                            if (c == 0) break;
                            decorated += static_cast<char>(c);
                        }

                        // Simple demangling: .?AVClassName@@ -> ClassName
                        if (decorated.size() > 4 && decorated.substr(0, 4) == ".?AV") {
                            size_t end = decorated.find("@@");
                            if (end != std::string::npos) {
                                vtable_class_name_ = decorated.substr(4, end - 4);
                            }
                        } else if (decorated.size() > 4 && decorated.substr(0, 4) == ".?AU") {
                            // struct
                            size_t end = decorated.find("@@");
                            if (end != std::string::npos) {
                                vtable_class_name_ = decorated.substr(4, end - 4);
                            }
                        } else if (!decorated.empty() && decorated[0] != '.') {
                            vtable_class_name_ = decorated;
                        }
                    }
                }

                LOG_INFO("VTable read: {} entries from 0x{:X}, class: {}",
                         vtable_entries_.size(), vtable_addr,
                         vtable_class_name_.empty() ? "(unknown)" : vtable_class_name_);
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        vtable_entries_.clear();
        vtable_class_name_.clear();
        vtable_error_.clear();
    }

    // Error display
    if (!vtable_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", vtable_error_.c_str());
    }

    // Class name display
    if (!vtable_class_name_.empty()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "RTTI Class: %s", vtable_class_name_.c_str());
    }

    // Results header
    ImGui::Separator();
    ImGui::Text("Entries: %zu", vtable_entries_.size());

    // Results table
    int num_cols = vtable_disasm_ ? 5 : 4;
    if (ImGui::BeginTable("##VTableEntries", num_cols,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
        if (vtable_disasm_) {
            ImGui::TableSetupColumn("First Instr", ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)vtable_entries_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& entry = vtable_entries_[row];

                ImGui::TableNextRow();
                ImGui::PushID(row);

                // Index column
                ImGui::TableNextColumn();
                ImGui::Text("%d", row);

                // Address column
                ImGui::TableNextColumn();
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)entry.address);
                ImGui::TextDisabled("%s", addr_buf);

                // Function column - clickable if valid
                ImGui::TableNextColumn();
                if (entry.valid) {
                    char func_buf[32];
                    snprintf(func_buf, sizeof(func_buf), "0x%llX", (unsigned long long)entry.function);
                    if (ImGui::Selectable(func_buf, false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            NavigateToAddress(entry.function);
                        }
                    }

                    // Context menu
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("View in Disassembly")) {
                            disasm_address_ = entry.function;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX",
                                     (unsigned long long)entry.function);
                            auto code = dma_->ReadMemory(selected_pid_, entry.function, 1024);
                            if (!code.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(code, entry.function);
                            }
                            panels_.disassembly = true;
                        }
                        if (ImGui::MenuItem("View in Memory")) {
                            memory_address_ = entry.function;
                            snprintf(address_input_, sizeof(address_input_), "0x%llX",
                                     (unsigned long long)entry.function);
                            memory_data_ = dma_->ReadMemory(selected_pid_, entry.function, 256);
                            panels_.memory_viewer = true;
                        }
                        #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
                        if (ImGui::MenuItem("Decompile")) {
                            decompile_address_ = entry.function;
                            snprintf(decompile_address_input_, sizeof(decompile_address_input_), "0x%llX",
                                     (unsigned long long)entry.function);
                            panels_.decompiler = true;
                        }
                        #endif
                        ImGui::Separator();
                        if (ImGui::MenuItem("Copy Address")) {
                            ImGui::SetClipboardText(func_buf);
                        }
                        if (ImGui::MenuItem("Add Bookmark")) {
                            std::string label = vtable_class_name_.empty() ?
                                "vtable[" + std::to_string(row) + "]" :
                                vtable_class_name_ + "::vfunc" + std::to_string(row);
                            bookmarks_->Add(entry.function, label, "", "VTable");
                        }
                        ImGui::EndPopup();
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "0x%llX (invalid)",
                                       (unsigned long long)entry.function);
                }

                // Context column
                ImGui::TableNextColumn();
                if (entry.valid) {
                    ImGui::Text("%s", entry.context.c_str());
                } else {
                    ImGui::TextDisabled("-");
                }

                // First instruction column
                if (vtable_disasm_) {
                    ImGui::TableNextColumn();
                    if (!entry.first_instr.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s", entry.first_instr.c_str());
                    } else {
                        ImGui::TextDisabled("-");
                    }
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void Application::RenderCacheManager() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Cache Manager", &panels_.cache_manager);

    // Header
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Manage cached analysis data (RTTI, Schema, Functions)");
    ImGui::Separator();
    ImGui::Spacing();

    // Filter section
    ImGui::Text("Type:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    const char* type_items[] = { "All", "RTTI", "Schema", "Functions" };
    if (ImGui::Combo("##cache_type", &cache_selected_type_, type_items, 4)) {
        cache_needs_refresh_ = true;
    }

    ImGui::SameLine();
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputTextWithHint("##cache_filter", "Search...", cache_filter_, sizeof(cache_filter_))) {
        cache_needs_refresh_ = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        cache_needs_refresh_ = true;
    }

    // Refresh cache list if needed
    if (cache_needs_refresh_) {
        cache_entries_.clear();
        cache_needs_refresh_ = false;

        auto cache_base = RuntimeManager::Instance().GetCacheDirectory();

        // Helper to scan a cache directory
        auto scan_cache_dir = [&](const std::string& subdir, const std::string& type) {
            auto dir_path = cache_base / subdir;
            if (!std::filesystem::exists(dir_path)) return;

            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                if (!entry.is_regular_file()) continue;

                std::string name = entry.path().filename().string();
                std::string filter_lower = cache_filter_;
                std::string name_lower = name;
                std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

                // Apply type filter
                if (cache_selected_type_ != 0) {
                    if ((cache_selected_type_ == 1 && type != "rtti") ||
                        (cache_selected_type_ == 2 && type != "schema") ||
                        (cache_selected_type_ == 3 && type != "functions")) {
                        continue;
                    }
                }

                // Apply text filter
                if (!filter_lower.empty() && name_lower.find(filter_lower) == std::string::npos) {
                    continue;
                }

                CacheEntry ce;
                ce.name = name;
                ce.path = entry.path().string();
                ce.size = entry.file_size();
                ce.type = type;

                // Format modification time
                auto ftime = std::filesystem::last_write_time(entry);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
                std::tm tm_val;
                #ifdef _WIN32
                localtime_s(&tm_val, &time_t_val);
                #else
                localtime_r(&time_t_val, &tm_val);
                #endif
                char time_buf[64];
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", &tm_val);
                ce.modified = time_buf;

                cache_entries_.push_back(ce);
            }
        };

        // Scan all cache directories
        scan_cache_dir("rtti", "rtti");
        scan_cache_dir("cs2_schema", "schema");
        scan_cache_dir("functions", "functions");

        // Sort by modification time (newest first)
        std::sort(cache_entries_.begin(), cache_entries_.end(),
            [](const CacheEntry& a, const CacheEntry& b) {
                return a.modified > b.modified;
            });
    }

    // Stats
    size_t total_size = 0;
    for (const auto& entry : cache_entries_) {
        total_size += entry.size;
    }
    ImGui::Text("Files: %zu | Total: %.2f MB", cache_entries_.size(), total_size / (1024.0 * 1024.0));

    ImGui::Separator();

    // Cache table
    if (ImGui::BeginTable("##CacheEntries", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)cache_entries_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& entry = cache_entries_[row];

                ImGui::TableNextRow();
                ImGui::PushID(row);

                // Name column
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.name.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.path.c_str());
                }

                // Type column
                ImGui::TableNextColumn();
                ImVec4 type_color;
                if (entry.type == "rtti") {
                    type_color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);  // Blue
                } else if (entry.type == "schema") {
                    type_color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);  // Green
                } else {
                    type_color = ImVec4(1.0f, 0.8f, 0.5f, 1.0f);  // Orange
                }
                ImGui::TextColored(type_color, "%s", entry.type.c_str());

                // Size column
                ImGui::TableNextColumn();
                if (entry.size >= 1024 * 1024) {
                    ImGui::Text("%.1f MB", entry.size / (1024.0 * 1024.0));
                } else if (entry.size >= 1024) {
                    ImGui::Text("%.1f KB", entry.size / 1024.0);
                } else {
                    ImGui::Text("%zu B", entry.size);
                }

                // Modified column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", entry.modified.c_str());

                // Actions column
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Delete")) {
                    ImGui::OpenPopup("Confirm Delete");
                }

                // Confirm delete popup
                if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete cache file '%s'?", entry.name.c_str());
                    ImGui::Spacing();

                    if (ImGui::Button("Delete", ImVec2(80, 0))) {
                        std::error_code ec;
                        std::filesystem::remove(entry.path, ec);
                        if (!ec) {
                            LOG_INFO("Deleted cache file: {}", entry.path);
                            cache_needs_refresh_ = true;
                        } else {
                            LOG_ERROR("Failed to delete: {}", ec.message());
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    // Bulk actions
    ImGui::Separator();
    if (ImGui::Button("Clear All RTTI Cache")) {
        auto dir_path = RuntimeManager::Instance().GetCacheDirectory() / "rtti";
        if (std::filesystem::exists(dir_path)) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) count++;
            }
            LOG_INFO("Cleared {} RTTI cache files", count);
            cache_needs_refresh_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All Schema Cache")) {
        auto dir_path = RuntimeManager::Instance().GetCacheDirectory() / "cs2_schema";
        if (std::filesystem::exists(dir_path)) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) count++;
            }
            LOG_INFO("Cleared {} Schema cache files", count);
            cache_needs_refresh_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All Function Cache")) {
        auto dir_path = RuntimeManager::Instance().GetCacheDirectory() / "functions";
        if (std::filesystem::exists(dir_path)) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) count++;
            }
            LOG_INFO("Cleared {} Function cache files", count);
            cache_needs_refresh_ = true;
        }
    }

    ImGui::End();
}

void Application::RenderTaskManager() {
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Task Manager", &panels_.task_manager);

    auto& tm = core::TaskManager::Instance();
    auto counts = tm.GetTaskCounts();

    // Header with counts
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Background Tasks");
    ImGui::SameLine();
    ImGui::TextDisabled("(Running: %zu, Pending: %zu, Completed: %zu, Failed: %zu)",
        counts.running, counts.pending, counts.completed, counts.failed);

    ImGui::Separator();
    ImGui::Spacing();

    // Controls
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    const char* filter_items[] = { "All", "Running", "Completed", "Failed" };
    ImGui::Combo("##task_filter", &task_filter_status_, filter_items, 4);

    ImGui::SameLine();
    ImGui::Checkbox("Auto-refresh", &task_list_auto_refresh_);

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        task_refresh_timer_ = task_refresh_interval_;  // Force immediate refresh
    }

    ImGui::SameLine();
    if (ImGui::Button("Cleanup Old")) {
        tm.CleanupTasks(std::chrono::seconds(60));  // Remove tasks older than 1 minute
        LOG_INFO("Cleaned up old tasks");
    }

    // Auto-refresh logic
    if (task_list_auto_refresh_) {
        task_refresh_timer_ += ImGui::GetIO().DeltaTime;
        if (task_refresh_timer_ >= task_refresh_interval_) {
            task_refresh_timer_ = 0.0f;
            // Refresh is automatic via ListTasks call below
        }
    }

    // Get task list with optional filter
    std::optional<core::TaskState> state_filter;
    if (task_filter_status_ == 1) state_filter = core::TaskState::Running;
    else if (task_filter_status_ == 2) state_filter = core::TaskState::Completed;
    else if (task_filter_status_ == 3) state_filter = core::TaskState::Failed;

    auto tasks = tm.ListTasks(state_filter);

    ImGui::Separator();
    ImGui::Text("Tasks: %zu", tasks.size());

    // Task table
    if (ImGui::BeginTable("##TaskTable", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < tasks.size(); i++) {
            const auto& task = tasks[i];

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // ID column
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", task.id.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Task ID: %s\nDescription: %s",
                    task.id.c_str(), task.description.c_str());
            }

            // Type column
            ImGui::TableNextColumn();
            ImGui::Text("%s", task.type.c_str());

            // Status column
            ImGui::TableNextColumn();
            ImVec4 status_color;
            switch (task.state) {
                case core::TaskState::Pending:
                    status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
                    break;
                case core::TaskState::Running:
                    status_color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);  // Blue
                    break;
                case core::TaskState::Completed:
                    status_color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // Green
                    break;
                case core::TaskState::Failed:
                    status_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
                    break;
                case core::TaskState::Cancelled:
                    status_color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Yellow
                    break;
            }
            ImGui::TextColored(status_color, "%s", core::TaskStateToString(task.state).c_str());

            // Progress column
            ImGui::TableNextColumn();
            if (task.state == core::TaskState::Running || task.state == core::TaskState::Pending) {
                ImGui::ProgressBar(task.progress, ImVec2(-1, 0),
                    task.progress > 0 ? nullptr : "...");
            } else if (task.state == core::TaskState::Completed) {
                ImGui::ProgressBar(1.0f, ImVec2(-1, 0), "Done");
            } else if (task.state == core::TaskState::Failed) {
                ImGui::ProgressBar(task.progress, ImVec2(-1, 0), "Failed");
            } else {
                ImGui::ProgressBar(task.progress, ImVec2(-1, 0), "Cancelled");
            }

            // Message column
            ImGui::TableNextColumn();
            if (!task.status_message.empty()) {
                ImGui::Text("%s", task.status_message.c_str());
            } else if (task.error) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", task.error->c_str());
            } else {
                ImGui::TextDisabled("-");
            }

            // Actions column
            ImGui::TableNextColumn();
            if (task.state == core::TaskState::Running || task.state == core::TaskState::Pending) {
                if (ImGui::SmallButton("Cancel")) {
                    if (tm.CancelTask(task.id)) {
                        LOG_INFO("Cancelled task: {}", task.id);
                    }
                }
            } else {
                // Show result preview for completed tasks
                if (task.state == core::TaskState::Completed && task.result) {
                    if (ImGui::SmallButton("View")) {
                        ImGui::OpenPopup("Task Result");
                    }

                    if (ImGui::BeginPopup("Task Result")) {
                        ImGui::Text("Task: %s (%s)", task.type.c_str(), task.id.c_str());
                        ImGui::Separator();

                        // Show result summary
                        std::string result_str = task.result->dump(2);
                        if (result_str.length() > 2000) {
                            result_str = result_str.substr(0, 2000) + "\n... (truncated)";
                        }
                        ImGui::TextWrapped("%s", result_str.c_str());

                        if (ImGui::Button("Copy")) {
                            ImGui::SetClipboardText(task.result->dump(2).c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Close")) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                } else {
                    ImGui::TextDisabled("-");
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // Bottom section - quick stats
    ImGui::Separator();
    ImGui::Text("Total: %zu | Running: %zu | Pending: %zu | Completed: %zu | Failed: %zu | Cancelled: %zu",
        counts.total, counts.running, counts.pending, counts.completed, counts.failed, counts.cancelled);

    ImGui::End();
}

} // namespace orpheus::ui
