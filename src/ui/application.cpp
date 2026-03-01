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
#include "icons.h"
#include "panel_helpers.h"
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
#include <unordered_set>

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

    // Merge icon font into regular font (if available)
    icons_loaded_ = false;
    if constexpr (orpheus::embedded::has_icon_font) {
        if (orpheus::embedded::fa_solid_900_ttf_size > 1) {
            ImFontConfig icons_config;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            icons_config.GlyphMinAdvanceX = scaled_font_size;
            icons_config.GlyphOffset = ImVec2(0, 1);  // Slight vertical offset for alignment
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };

            void* icon_data = IM_ALLOC(orpheus::embedded::fa_solid_900_ttf_size);
            memcpy(icon_data, orpheus::embedded::fa_solid_900_ttf, orpheus::embedded::fa_solid_900_ttf_size);
            ImFont* merged = io.Fonts->AddFontFromMemoryTTF(
                icon_data, (int)orpheus::embedded::fa_solid_900_ttf_size,
                scaled_font_size * 0.85f, &icons_config, icon_ranges);
            if (merged) {
                icons_loaded_ = true;
            }
        }
    }

    font_bold_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_bold_ttf,
        orpheus::embedded::jetbrainsmono_bold_ttf_size,
        scaled_font_size
    );

    // Merge icons into bold font too
    if constexpr (orpheus::embedded::has_icon_font) {
        if (icons_loaded_ && orpheus::embedded::fa_solid_900_ttf_size > 1) {
            ImFontConfig icons_config;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            icons_config.GlyphMinAdvanceX = scaled_font_size;
            icons_config.GlyphOffset = ImVec2(0, 1);
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };

            void* icon_data = IM_ALLOC(orpheus::embedded::fa_solid_900_ttf_size);
            memcpy(icon_data, orpheus::embedded::fa_solid_900_ttf, orpheus::embedded::fa_solid_900_ttf_size);
            io.Fonts->AddFontFromMemoryTTF(
                icon_data, (int)orpheus::embedded::fa_solid_900_ttf_size,
                scaled_font_size * 0.85f, &icons_config, icon_ranges);
        }
    }

    font_mono_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_medium_ttf,
        orpheus::embedded::jetbrainsmono_medium_ttf_size,
        scaled_mono_size
    );

    if (font_regular_ && font_bold_ && font_mono_) {
        LOG_INFO("Loaded fonts at size {:.0f}px (DPI scale: {:.2f}){}",
                 scaled_font_size, dpi_scale_, icons_loaded_ ? " with icons" : "");
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

    // Update semantic colors for theme type
    colors::ApplyThemeColors(theme == Theme::Light);

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
        colors[ImGuiCol_TabHovered]             = ImVec4(0.28f, 0.30f, 0.35f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.45f, 0.72f, 0.96f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
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

    // Render toolbar first — it adjusts viewport WorkPos/WorkSize
    // so the dockspace below is offset to avoid overlap
    RenderToolbar();

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Reserve space for the status bar at the bottom
    constexpr float status_bar_height = 24.0f;
    viewport->WorkSize.y -= status_bar_height;

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
                ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Connecting...", "Connecting..."), nullptr, false, false);
            } else if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_PLUG " Connect DMA", "Connect DMA"), "Ctrl+D", false, dma_ && !dma_->IsConnected())) {
                dma_connecting_ = true;
                LOG_INFO("Connecting to DMA device...");
                dma_connect_future_ = std::async(std::launch::async, [this]() {
                    return dma_->Initialize("fpga");
                });
            }
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CIRCLE_XMARK " Disconnect", "Disconnect"), nullptr, false, dma_ && dma_->IsConnected() && !dma_connecting_)) {
                dma_->Close();
                cached_processes_.clear();
                cached_modules_.clear();
                LOG_INFO("DMA disconnected");
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_EXPORT " Dump Module...", "Dump Module..."), nullptr, false, selected_module_base_ != 0)) {
                show_dump_dialog_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                RequestExit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            // Core panels
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_MICROCHIP " Processes", "Processes"), "Ctrl+1", &panels_.process_list);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_PUZZLE_PIECE " Modules", "Modules"), "Ctrl+2", &panels_.module_list);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " Memory", "Memory"), "Ctrl+3", &panels_.memory_viewer);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " Disassembly", "Disassembly"), "Ctrl+4", &panels_.disassembly);
            #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_CODE " Decompiler", "Decompiler"), "Ctrl+Shift+D", &panels_.decompiler);
#endif
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_DIAGRAM_PROJECT " CFG Viewer", "CFG Viewer"), "Ctrl+Shift+G", &panels_.cfg_viewer);
            ImGui::Separator();

            // Analysis tools
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS " Pattern Scanner", "Pattern Scanner"), "Ctrl+5", &panels_.pattern_scanner);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_FONT " String Scanner", "String Scanner"), "Ctrl+6", &panels_.string_scanner);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_EYE " Memory Watcher", "Memory Watcher"), "Ctrl+7", &panels_.memory_watcher);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_SITEMAP " RTTI Scanner", "RTTI Scanner"), "Ctrl+9", &panels_.rtti_scanner);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_BOOKMARK " Bookmarks", "Bookmarks"), "Ctrl+B", &panels_.bookmarks);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_WAND_MAGIC_SPARKLES " Emulator", "Emulator"), "Ctrl+8", &panels_.emulator);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_LINK " Pointer Chain", "Pointer Chain"), nullptr, &panels_.pointer_chain);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_LAYER_GROUP " Memory Regions", "Memory Regions"), nullptr, &panels_.memory_regions);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS " Function Recovery", "Function Recovery"), "Ctrl+Shift+F", &panels_.function_recovery);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_LINK " XRef Finder", "XRef Finder"), "Ctrl+X", &panels_.xref_finder);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_SITEMAP " VTable Reader", "VTable Reader"), "Ctrl+V", &panels_.vtable_reader);
            ImGui::Separator();

            // Utility panels
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_DATABASE " Cache Manager", "Cache Manager"), nullptr, &panels_.cache_manager);
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_LIST_CHECK " Task Manager", "Task Manager"), nullptr, &panels_.task_manager);
            ImGui::Separator();
            if (ImGui::BeginMenu(ICON_OR_TEXT(icons_loaded_, ICON_FA_CROSSHAIRS " Game Tools", "Game Tools"))) {
                ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_DATABASE " CS2 Schema Dumper", "CS2 Schema Dumper"), "Ctrl+Shift+C", &panels_.cs2_schema);
                ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS " CS2 Entity Inspector", "CS2 Entity Inspector"), "Ctrl+Shift+E", &panels_.cs2_entity_inspector);
                ImGui::Separator();
                ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CROSSHAIRS " CS2 Radar", "CS2 Radar"), "Ctrl+Shift+R", &panels_.cs2_radar);
                ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_GAUGE_HIGH " CS2 Dashboard", "CS2 Dashboard"), "Ctrl+Shift+P", &panels_.cs2_dashboard);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TERMINAL " Console", "Console"), "Ctrl+`", &panels_.console);
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
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_ARROW_LEFT " Back", "Back"), "Alt+Left", false, CanNavigateBack())) {
                NavigateBack();
            }
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_ARROW_RIGHT " Forward", "Forward"), "Alt+Right", false, CanNavigateForward())) {
                NavigateForward();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS " Goto Address...", "Goto Address..."), "Ctrl+G")) {
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

            ImGui::TextColored(colors::Muted, "MCP Server");
            ImGui::Separator();

            if (mcp_running) {
                ImGui::PushStyleColor(ImGuiCol_Text, colors::Success);
                ImGui::Text("Status: Running");
                ImGui::PopStyleColor();

                if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_STOP " Stop MCP Server", "Stop MCP Server"))) {
                    if (mcp_server_) {
                        mcp_server_->Stop();
                        LOG_INFO("MCP server stopped from menu");
                    }
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, colors::Muted);
                ImGui::Text("Status: Stopped");
                ImGui::PopStyleColor();

                if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_PLAY " Start MCP Server", "Start MCP Server"))) {
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

            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_GEAR " MCP Settings...", "MCP Settings..."))) {
                show_settings_ = true;
            }

            ImGui::Separator();

            // Copy API key (if available)
            if (mcp_config_ && !mcp_config_->api_key.empty()) {
                if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy API Key", "Copy API Key"))) {
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
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_GEAR " Settings", "Settings"), "Ctrl+,")) {
                show_settings_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TERMINAL " Command Palette", "Command Palette"), "Ctrl+Shift+P")) {
                show_command_palette_ = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_);
            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CIRCLE_INFO " About Orpheus", "About Orpheus"))) {
                show_about_ = true;
            }
            ImGui::EndMenu();
        }

        // Right-aligned status
        float status_width = 200.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - status_width);

        if (dma_ && dma_->IsConnected()) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::Success);
            std::string device = dma_->GetDeviceType();
            if (device == "fpga") {
                device = "FPGA";
            }
            if (icons_loaded_) ImGui::Text(ICON_FA_PLUG " %s", device.c_str());
            else ImGui::Text("DMA: %s", device.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::Error);
            ImGui::Text("%s", ICON_OR_TEXT(icons_loaded_, ICON_FA_CIRCLE_XMARK " Disconnected", "DMA: Disconnected"));
            ImGui::PopStyleColor();
        }

        ImGui::EndMenuBar();
    }
}

void Application::RenderStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = 24.0f;

    // Position at the very bottom of the viewport (below dockspace work area)
    float bar_y = viewport->Pos.y + viewport->Size.y - height;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, bar_y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoDocking |
                              ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));

    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        // Left side - DMA status indicator
        bool connected = dma_ && dma_->IsConnected();
        if (dma_connecting_) {
            ImGui::TextColored(colors::Warning,
                ICON_OR_TEXT(icons_loaded_, ICON_FA_CIRCLE, "*"));
            ImGui::SameLine(0, 4.0f);
            ImGui::TextColored(colors::Muted, "Connecting");
        } else if (connected) {
            ImGui::TextColored(colors::Success,
                ICON_OR_TEXT(icons_loaded_, ICON_FA_CIRCLE, "*"));
            ImGui::SameLine(0, 4.0f);
            ImGui::TextColored(colors::Muted, "DMA");
        } else {
            ImGui::TextColored(colors::Error,
                ICON_OR_TEXT(icons_loaded_, ICON_FA_CIRCLE, "*"));
            ImGui::SameLine(0, 4.0f);
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "Offline");
        }

        // Process/module info
        if (selected_pid_ != 0) {
            ImGui::SameLine(0, 16.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "|");
            ImGui::SameLine(0, 8.0f);
            ImGui::Text("%s", selected_process_name_.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("PID: %u", selected_pid_);
            }
        }
        if (!selected_module_name_.empty()) {
            ImGui::SameLine(0, 8.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), ">");
            ImGui::SameLine(0, 4.0f);
            ImGui::TextColored(ImVec4(0.6f, 0.75f, 0.95f, 1.0f), "%s", selected_module_name_.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Base: 0x%llX  Size: 0x%X",
                    (unsigned long long)selected_module_base_, selected_module_size_);
            }
        }

        // Status message (center-ish area)
        if (!status_message_.empty()) {
            ImGui::SameLine(0, 20.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "|");
            ImGui::SameLine(0, 8.0f);
            ImGui::Text("%s", status_message_.c_str());
        }

        // Right side section
        float right_x = ImGui::GetWindowWidth() - 200.0f;
        ImGui::SameLine(right_x);

        // MCP server status indicator
        bool mcp_running = mcp_server_ && mcp_server_->IsRunning();
        if (mcp_running) {
            ImGui::TextColored(colors::Success,
                ICON_OR_TEXT(icons_loaded_, ICON_FA_SERVER, "[MCP]"));
            if (ImGui::IsItemHovered()) {
                std::string bind_addr = mcp_config_ ? mcp_config_->bind_address : "127.0.0.1";
                uint16_t port = mcp_config_ ? mcp_config_->port : 8765;
                ImGui::SetTooltip("MCP Server: %s:%d\nClick to open settings",
                    bind_addr.c_str(), port);
            }
            if (ImGui::IsItemClicked()) {
                show_settings_ = true;
            }
        } else {
            ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.35f, 1.0f),
                ICON_OR_TEXT(icons_loaded_, ICON_FA_SERVER, "[MCP]"));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("MCP Server stopped\nClick to open settings");
            }
            if (ImGui::IsItemClicked()) {
                show_settings_ = true;
            }
        }

        // CS2 indicator (when attached to CS2)
        if (IsCS2Process()) {
            ImGui::SameLine(0, 10.0f);
            if (cs2_auto_init_success_) {
                ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "CS2");
            } else if (cs2_auto_init_attempted_) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "CS2");
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.35f, 1.0f), "CS2");
            }
            if (ImGui::IsItemHovered()) {
                const char* state = cs2_auto_init_success_ ? "Initialized" :
                                    cs2_auto_init_attempted_ ? "Partial" : "Not initialized";
                ImGui::SetTooltip("CS2 Integration: %s", state);
            }
        }

        // Version info on far right
        ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "v%s", orpheus::version::VERSION);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", orpheus::version::GetBuildInfo());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}


// Panel render methods have been moved to src/ui/panels/
// See: panel_*.cpp and dialog_*.cpp for individual panel implementations

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
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

    // Controller/pawn offsets (from schema - updated 2026-02-15)
    constexpr uint32_t OFFSET_PLAYER_NAME = 0x6F8;
    constexpr uint32_t OFFSET_TEAM_NUM = 0x3F3;
    constexpr uint32_t OFFSET_PAWN_HANDLE = 0x90C;
    constexpr uint32_t OFFSET_PAWN_IS_ALIVE = 0x914;
    constexpr uint32_t OFFSET_PAWN_HEALTH = 0x918;
    constexpr uint32_t OFFSET_CONNECTED = 0x6F4;
    constexpr uint32_t OFFSET_IS_LOCAL = 0x788;
    constexpr uint32_t OFFSET_SCENE_NODE = 0x338;
    constexpr uint32_t OFFSET_ABS_ORIGIN = 0xD0;
    constexpr uint32_t OFFSET_SPOTTED_STATE = 0x26E0;
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

} // namespace orpheus::ui
