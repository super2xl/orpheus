#include "installer_app.h"
#include "embedded_resources.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstring>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace orpheus::installer {

using json = nlohmann::json;

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

InstallerApp::InstallerApp() = default;

InstallerApp::~InstallerApp() {
    Shutdown();
}

bool InstallerApp::Initialize() {
    if (!InitializeGLFW()) {
        return false;
    }

    // Create borderless window for custom title bar
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    window_ = glfwCreateWindow(360, 480, "MCPinstaller", nullptr, nullptr);
    if (window_ == nullptr) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Center window on screen
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int x = (mode->width - 360) / 2;
    int y = (mode->height - 480) / 2;
    glfwSetWindowPos(window_, x, y);

    SetWindowIcon();

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if (!InitializeImGui()) {
        glfwDestroyWindow(window_);
        glfwTerminate();
        return false;
    }

    SetupStyle();
    DetectMCPClients();

    return true;
}

bool InstallerApp::InitializeGLFW() {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    return true;
}

bool InstallerApp::InitializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // No layout saving for installer

    // Load embedded fonts
    ImFontConfig font_config;
    font_config.OversampleH = 2;
    font_config.OversampleV = 1;
    font_config.PixelSnapH = true;

    auto LoadEmbeddedFont = [&io, &font_config](const unsigned char* data, size_t size, float fontSize) -> ImFont* {
        void* font_data = IM_ALLOC(size);
        memcpy(font_data, data, size);
        return io.Fonts->AddFontFromMemoryTTF(font_data, (int)size, fontSize, &font_config);
    };

    font_regular_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_regular_ttf,
        orpheus::embedded::jetbrainsmono_regular_ttf_size,
        15.0f
    );

    font_bold_ = LoadEmbeddedFont(
        orpheus::embedded::jetbrainsmono_bold_ttf,
        orpheus::embedded::jetbrainsmono_bold_ttf_size,
        15.0f
    );

    if (!font_regular_ || !font_bold_) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void InstallerApp::SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Rounded corners
    style.WindowRounding = 10.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;

    // Borders
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    // Padding
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 12.0f;

    // Dark theme colors (matching Orpheus)
    colors[ImGuiCol_Text]                   = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.13f, 0.13f, 0.13f, 0.98f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.15f, 0.15f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.45f, 0.72f, 0.96f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
}

void InstallerApp::SetWindowIcon() {
    if constexpr (orpheus::embedded::has_icon) {
        int width, height, channels;
        unsigned char* pixels = stbi_load_from_memory(
            orpheus::embedded::orpheus_png,
            static_cast<int>(orpheus::embedded::orpheus_png_size),
            &width, &height, &channels, 4
        );

        if (pixels) {
            GLFWimage icon;
            icon.width = width;
            icon.height = height;
            icon.pixels = pixels;
            glfwSetWindowIcon(window_, 1, &icon);
            stbi_image_free(pixels);
        }
    }
}

void InstallerApp::DetectMCPClients() {
    clients_.clear();

#ifdef PLATFORM_WINDOWS
    char* appdata = nullptr;
    size_t len = 0;
    std::string appdata_path;
    if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
        appdata_path = appdata;
        free(appdata);
    }

    std::string home = std::filesystem::path(getenv("USERPROFILE")).string();

    // Define all supported MCP clients
    std::vector<MCPClientInfo> all_clients = {
        {"Claude Desktop", appdata_path + "\\Claude", "claude_desktop_config.json", false, true, "mcpServers", ""},
        {"Cursor", home + "\\.cursor", "mcp.json", false, true, "mcpServers", ""},
        {"Claude Code", home, ".claude.json", false, true, "mcpServers", ""},
        {"Windsurf", home + "\\.codeium\\windsurf", "mcp_config.json", false, true, "mcpServers", ""},
        {"VS Code", appdata_path + "\\Code\\User", "settings.json", false, true, "mcp", "servers"},
        {"Cline", appdata_path + "\\Code\\User\\globalStorage\\saoudrizwan.claude-dev\\settings", "cline_mcp_settings.json", false, true, "mcpServers", ""},
        {"Roo Code", appdata_path + "\\Code\\User\\globalStorage\\rooveterinaryinc.roo-cline\\settings", "mcp_settings.json", false, true, "mcpServers", ""},
        {"LM Studio", home + "\\.lmstudio", "mcp.json", false, true, "mcpServers", ""},
        {"Zed", appdata_path + "\\Zed", "settings.json", false, true, "mcpServers", ""},
        {"Amazon Q", home + "\\.aws\\amazonq", "mcp_config.json", false, true, "mcpServers", ""},
        {"Warp", home + "\\.warp", "mcp_config.json", false, true, "mcpServers", ""},
    };
#else
    std::string home = std::filesystem::path(getenv("HOME")).string();

    std::vector<MCPClientInfo> all_clients = {
        {"Claude Desktop", home + "/Library/Application Support/Claude", "claude_desktop_config.json", false, true, "mcpServers", ""},
        {"Cursor", home + "/.cursor", "mcp.json", false, true, "mcpServers", ""},
        {"Claude Code", home, ".claude.json", false, true, "mcpServers", ""},
        {"Windsurf", home + "/.codeium/windsurf", "mcp_config.json", false, true, "mcpServers", ""},
    };
#endif

    // Check which clients are installed
    for (auto& client : all_clients) {
        client.detected = IsClientInstalled(client.config_dir);
        if (client.detected) {
            clients_.push_back(client);
        }
    }
}

bool InstallerApp::IsClientInstalled(const std::string& config_dir) {
    return std::filesystem::exists(config_dir);
}

// Extract embedded mcp_bridge.js to AppData and return the path
static std::filesystem::path ExtractMCPBridge() {
    std::filesystem::path bridge_path;

#ifdef PLATFORM_WINDOWS
    char* appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
        bridge_path = std::filesystem::path(appdata) / "Orpheus" / "mcp_bridge.js";
        free(appdata);
    }
#else
    bridge_path = std::filesystem::path(getenv("HOME")) / ".orpheus" / "mcp_bridge.js";
#endif

    if (bridge_path.empty()) {
        return {};
    }

    // Check if embedded bridge is available
    if constexpr (!orpheus::embedded::has_mcp_bridge) {
        return {};
    }

    // Create directory if needed
    std::filesystem::create_directories(bridge_path.parent_path());

    // Write embedded bridge to file (always overwrite to ensure latest version)
    try {
        std::ofstream out(bridge_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(orpheus::embedded::mcp_bridge_js),
                  orpheus::embedded::mcp_bridge_js_size);
        out.close();
    } catch (const std::exception&) {
        return {};
    }

    return bridge_path;
}

std::string InstallerApp::GenerateMCPConfig() {
    // Extract embedded bridge and generate config
    std::filesystem::path bridge_path = ExtractMCPBridge();
    if (bridge_path.empty()) {
        return "{}";
    }

    json config;
    config["command"] = "node";
    config["args"] = json::array({bridge_path.string()});
    config["env"] = {
        {"ORPHEUS_MCP_URL", orpheus_url_},
        {"ORPHEUS_API_KEY", api_key_}
    };

    return config.dump(2);
}

bool InstallerApp::InstallToClient(MCPClientInfo& client) {
    std::filesystem::path config_path = std::filesystem::path(client.config_dir) / client.config_file;

    // Extract embedded mcp_bridge.js to AppData
    std::filesystem::path bridge_path = ExtractMCPBridge();
    if (bridge_path.empty()) {
        status_message_ = "Failed to extract mcp_bridge.js";
        status_is_error_ = true;
        return false;
    }

    json config;

    // Read existing config if it exists
    if (std::filesystem::exists(config_path)) {
        try {
            std::ifstream in(config_path);
            std::string content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
            if (!content.empty()) {
                config = json::parse(content);
            }
        } catch (const std::exception&) {
            // Invalid JSON, start fresh
            config = json::object();
        }
    }

    // Build the MCP server config
    json mcp_config;
    mcp_config["command"] = "node";
    mcp_config["args"] = json::array({bridge_path.string()});
    mcp_config["env"] = {
        {"ORPHEUS_MCP_URL", std::string(orpheus_url_)},
        {"ORPHEUS_API_KEY", std::string(api_key_)}
    };

    // Handle different config structures
    if (!client.nested_key.empty()) {
        // VS Code style: mcp.servers
        if (!config.contains(client.json_key)) {
            config[client.json_key] = json::object();
        }
        if (!config[client.json_key].contains(client.nested_key)) {
            config[client.json_key][client.nested_key] = json::object();
        }
        config[client.json_key][client.nested_key]["orpheus"] = mcp_config;
    } else {
        // Standard style: mcpServers at root
        if (!config.contains(client.json_key)) {
            config[client.json_key] = json::object();
        }
        config[client.json_key]["orpheus"] = mcp_config;
    }

    // Create parent directory if needed
    std::filesystem::create_directories(client.config_dir);

    // Write config
    try {
        std::ofstream out(config_path);
        out << config.dump(2);
        out.close();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int InstallerApp::Run() {
    while (!glfwWindowShouldClose(window_) && !should_close_) {
        glfwPollEvents();

        HandleWindowDrag();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderFrame();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0, 0, 0, 0);  // Transparent background
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    return 0;
}

void InstallerApp::HandleWindowDrag() {
    // Handle window dragging for borderless window
    double mouse_x, mouse_y;
    glfwGetCursorPos(window_, &mouse_x, &mouse_y);

    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (!is_dragging_ && mouse_y < 40) {  // Only drag from title bar area
            is_dragging_ = true;
            drag_start_x_ = mouse_x;
            drag_start_y_ = mouse_y;
            glfwGetWindowPos(window_, &window_start_x_, &window_start_y_);
        }

        if (is_dragging_) {
            int new_x = window_start_x_ + static_cast<int>(mouse_x - drag_start_x_);
            int new_y = window_start_y_ + static_cast<int>(mouse_y - drag_start_y_);
            glfwSetWindowPos(window_, new_x, new_y);
        }
    } else {
        is_dragging_ = false;
    }
}

void InstallerApp::RenderFrame() {
    ImGuiIO& io = ImGui::GetIO();

    // Full window ImGui content
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##MainWindow", nullptr, flags);

    RenderTitleBar();
    ImGui::Separator();
    RenderMainContent();
    RenderStatusBar();

    ImGui::End();
}

void InstallerApp::RenderTitleBar() {
    // Custom title bar
    if (font_bold_) ImGui::PushFont(font_bold_);
    ImGui::Text("MCPinstaller");
    if (font_bold_) ImGui::PopFont();

    // Close button
    ImGui::SameLine(ImGui::GetWindowWidth() - 35);
    if (ImGui::Button("X", ImVec2(25, 25))) {
        should_close_ = true;
    }
}

void InstallerApp::RenderMainContent() {
    ImGui::Spacing();

    // Server configuration section
    if (font_bold_) ImGui::PushFont(font_bold_);
    ImGui::Text("Server Configuration");
    if (font_bold_) ImGui::PopFont();

    ImGui::Spacing();

    ImGui::Text("Orpheus URL:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##url", orpheus_url_, sizeof(orpheus_url_));

    ImGui::Spacing();

    ImGui::Text("API Key:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##apikey", api_key_, sizeof(api_key_), ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Detected clients section
    if (font_bold_) ImGui::PushFont(font_bold_);
    ImGui::Text("Detected MCP Clients");
    if (font_bold_) ImGui::PopFont();

    ImGui::Spacing();

    RenderClientList();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Install button
    bool can_install = false;
    for (const auto& client : clients_) {
        if (client.selected) {
            can_install = true;
            break;
        }
    }

    if (!can_install) {
        ImGui::BeginDisabled();
    }

    float button_width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.80f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.45f, 0.75f, 1.0f));

    if (ImGui::Button("Install to Selected Clients", ImVec2(button_width, 35))) {
        install_count_ = 0;
        status_is_error_ = false;

        for (auto& client : clients_) {
            if (client.selected) {
                if (InstallToClient(client)) {
                    install_count_++;
                } else {
                    status_is_error_ = true;
                    status_message_ = "Failed to install to " + client.name;
                }
            }
        }

        if (!status_is_error_ && install_count_ > 0) {
            status_message_ = "Installed to " + std::to_string(install_count_) +
                             " client(s). Restart them to apply.";
        }
    }

    ImGui::PopStyleColor(3);

    if (!can_install) {
        ImGui::EndDisabled();
    }
}

void InstallerApp::RenderClientList() {
    if (clients_.empty()) {
        ImGui::TextDisabled("No MCP clients detected on this system.");
        ImGui::TextDisabled("Install Claude Desktop, Cursor, or another");
        ImGui::TextDisabled("MCP-compatible client first.");
        return;
    }

    // Client checkboxes in a scrollable area
    ImGui::BeginChild("##clients", ImVec2(0, 150), true);

    for (auto& client : clients_) {
        ImGui::Checkbox(client.name.c_str(), &client.selected);

        // Show config path as tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Config: %s/%s", client.config_dir.c_str(), client.config_file.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();

    // Select all / none buttons
    if (ImGui::SmallButton("Select All")) {
        for (auto& client : clients_) {
            client.selected = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Select None")) {
        for (auto& client : clients_) {
            client.selected = false;
        }
    }
}

void InstallerApp::RenderStatusBar() {
    if (!status_message_.empty()) {
        ImGui::Spacing();

        if (status_is_error_) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
        }

        ImGui::TextWrapped("%s", status_message_.c_str());
        ImGui::PopStyleColor();
    }
}

void InstallerApp::Shutdown() {
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }
}

} // namespace orpheus::installer
