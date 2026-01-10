#pragma once

#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;
struct ImFont;

namespace orpheus::installer {

/**
 * MCP client configuration information
 */
struct MCPClientInfo {
    std::string name;           // Display name (e.g., "Claude Desktop")
    std::string config_dir;     // Config directory path
    std::string config_file;    // Config filename
    bool detected;              // Whether the client is installed
    bool selected;              // Whether to install to this client
    std::string json_key;       // Top-level key ("mcpServers" or special)
    std::string nested_key;     // Nested key (for VS Code style)
    bool use_cli;               // Use CLI command instead of JSON file (Claude Code)
};

/**
 * InstallerApp - Small GUI for configuring MCP bridge on client machines
 *
 * This is a standalone tool to configure MCP clients (Claude, Cursor, etc.)
 * to connect to an Orpheus server running on a different machine.
 */
class InstallerApp {
public:
    InstallerApp();
    ~InstallerApp();

    bool Initialize();
    int Run();
    void Shutdown();

private:
    // Initialization
    bool InitializeGLFW();
    bool InitializeImGui();
    void SetupStyle();
    void SetWindowIcon();

    // MCP client detection
    void DetectMCPClients();
    bool IsClientInstalled(const std::string& config_dir);

    // Installation
    bool InstallToClient(MCPClientInfo& client);
    std::string GenerateMCPConfig();

    // GUI rendering
    void RenderFrame();
    void RenderTitleBar();
    void RenderMainContent();
    void RenderClientList();
    void RenderStatusBar();

    // Window dragging (for borderless window)
    void HandleWindowDrag();

    GLFWwindow* window_ = nullptr;
    ImFont* font_regular_ = nullptr;
    ImFont* font_bold_ = nullptr;

    // Configuration
    char orpheus_url_[256] = "http://localhost:8765";
    char api_key_[256] = "";

    // MCP clients
    std::vector<MCPClientInfo> clients_;

    // State
    bool should_close_ = false;
    bool is_dragging_ = false;
    double drag_start_x_ = 0;
    double drag_start_y_ = 0;
    int window_start_x_ = 0;
    int window_start_y_ = 0;

    // Status
    std::string status_message_;
    bool status_is_error_ = false;
    int install_count_ = 0;
};

} // namespace orpheus::installer
