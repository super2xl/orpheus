#include "application.h"
#include "core/dma_interface.h"
#include "utils/logger.h"
#include "utils/bookmarks.h"
#include "analysis/disassembler.h"
#include "analysis/pattern_scanner.h"
#include "analysis/string_scanner.h"
#include "analysis/memory_watcher.h"
#include "analysis/pe_dumper.h"
#include "analysis/rtti_parser.h"
#include "mcp/mcp_server.h"
#include "emulation/emulator.h"
#include "dumper/cs2_schema.h"
#include "embedded_resources.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <GLFW/glfw3.h>

// stb_image for icon loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

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

    io.IniFilename = "orpheus_layout.ini";

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
            if (dma_ && !dma_->IsConnected()) {
                if (dma_->Initialize("fpga")) {
                    LOG_INFO("DMA connected");
                    RefreshProcesses();
                }
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
    if (panels_.pattern_scanner) RenderPatternScanner();
    if (panels_.string_scanner) RenderStringScanner();
    if (panels_.memory_watcher) RenderMemoryWatcher();
    if (panels_.rtti_scanner) RenderRTTIScanner();
    if (panels_.bookmarks) RenderBookmarks();
    if (panels_.emulator) RenderEmulatorPanel();
    if (panels_.cs2_schema) RenderCS2Schema();
    if (panels_.cs2_entity_inspector) RenderCS2EntityInspector();
    if (panels_.console) RenderConsole();

    // Dialogs
    if (show_command_palette_) RenderCommandPalette();
    if (show_about_) RenderAboutDialog();
    if (show_goto_dialog_) RenderGotoDialog();
    if (show_dump_dialog_) RenderDumpDialog();
    if (show_settings_) RenderSettingsDialog();
    if (show_demo_) ImGui::ShowDemoWindow(&show_demo_);

    RenderStatusBar();
}

void Application::RenderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Connect DMA", "Ctrl+D", false, dma_ && !dma_->IsConnected())) {
                if (dma_->Initialize("fpga")) {
                    LOG_INFO("DMA connected successfully");
                    RefreshProcesses();
                }
            }
            if (ImGui::MenuItem("Disconnect", nullptr, false, dma_ && dma_->IsConnected())) {
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
            ImGui::Separator();
            ImGui::MenuItem("Pattern Scanner", "Ctrl+5", &panels_.pattern_scanner);
            ImGui::MenuItem("String Scanner", "Ctrl+6", &panels_.string_scanner);
            ImGui::MenuItem("Memory Watcher", "Ctrl+7", &panels_.memory_watcher);
            ImGui::MenuItem("RTTI Scanner", "Ctrl+9", &panels_.rtti_scanner);
            ImGui::MenuItem("Bookmarks", "Ctrl+B", &panels_.bookmarks);
            ImGui::MenuItem("Emulator", "Ctrl+8", &panels_.emulator);
            ImGui::Separator();
            if (ImGui::BeginMenu("Game Tools")) {
                ImGui::MenuItem("CS2 Schema Dumper", "Ctrl+Shift+C", &panels_.cs2_schema);
                ImGui::MenuItem("CS2 Entity Inspector", "Ctrl+Shift+E", &panels_.cs2_entity_inspector);
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
                ImGui::SetTooltip("MCP Server running on port %d\nClick to open settings",
                    mcp_config_ ? mcp_config_->port : 8765);
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
                    RefreshModules();
                    LOG_INFO("Selected process: {} (PID: {})", proc.name, proc.pid);
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
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
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

                    // Build entire line in a buffer for faster rendering
                    char line_buf[256];
                    int pos = 0;

                    // Hex bytes
                    for (int col = 0; col < bytes_per_row_; col++) {
                        size_t idx = offset + col;
                        if (idx < memory_data_.size()) {
                            pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%02X ", memory_data_[idx]);
                        } else {
                            pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "   ");
                        }
                        if (col == 7) {
                            line_buf[pos++] = ' ';
                        }
                    }

                    // ASCII section
                    char ascii_buf[32] = {0};
                    if (show_ascii_) {
                        ascii_buf[0] = '|';
                        for (int col = 0; col < bytes_per_row_; col++) {
                            size_t idx = offset + col;
                            if (idx < memory_data_.size()) {
                                char c = static_cast<char>(memory_data_[idx]);
                                ascii_buf[1 + col] = (c >= 32 && c < 127) ? c : '.';
                            } else {
                                ascii_buf[1 + col] = ' ';
                            }
                        }
                        ascii_buf[1 + bytes_per_row_] = '|';
                        ascii_buf[2 + bytes_per_row_] = '\0';
                    }

                    // Render entire row
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%016llX  ", (unsigned long long)addr);
                    ImGui::SameLine();
                    ImGui::Text("%s", line_buf);
                    if (show_ascii_) {
                        ImGui::SameLine();
                        ImGui::Text("%s", ascii_buf);
                    }
                }
            }

            ImGui::EndChild();
            if (font_mono_) ImGui::PopFont();
            ImGui::PopStyleVar();
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
            }
        }

        ImGui::EndChild();
        if (font_mono_) ImGui::PopFont();
    } else {
        ImGui::TextDisabled("Select a process to disassemble");
    }

    ImGui::End();
}

void Application::RenderPatternScanner() {
    ImGui::Begin("Pattern Scanner", &panels_.pattern_scanner);

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
            pattern_scanning_ = true;
            pattern_results_.clear();

            auto pattern = analysis::PatternScanner::Compile(pattern_input_);
            if (pattern) {
                auto data = dma_->ReadMemory(selected_pid_, selected_module_base_, selected_module_size_);
                if (!data.empty()) {
                    pattern_results_ = analysis::PatternScanner::Scan(data, *pattern, selected_module_base_);
                    LOG_INFO("Pattern scan found {} results", pattern_results_.size());
                }
            }
            pattern_scanning_ = false;
        }
        if (!can_scan) ImGui::EndDisabled();

        ImGui::Separator();

        // Results header
        ImGui::Text("Results: %zu", pattern_results_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            pattern_results_.clear();
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
                cs2_schema_ = std::make_unique<dumper::CS2SchemaDumper>(dma_.get(), selected_pid_);
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

        const dumper::SchemaClass* selected_cls = nullptr;
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
                ImGui::TextDisabled("(all interfaces, default: 8765)");

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

} // namespace orpheus::ui
