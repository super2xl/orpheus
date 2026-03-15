#include "core/runtime_manager.h"
#include "core/dma_interface.h"
#include "core/orpheus_core.h"
#include "ui/application.h"
#include "mcp/mcp_server.h"
#include "utils/logger.h"
#include "utils/telemetry.h"
#include "version.h"

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

// Global flag for headless shutdown
static std::atomic<bool> g_headless_running{true};

int main(int argc, char** argv) {
    // CRITICAL: Initialize logging first
    if (!orpheus::Logger::Instance().Initialize()) {
        std::cerr << "Failed to initialize logging system" << std::endl;
        return 1;
    }

    LOG_INFO("Starting Orpheus DMA Reversing Framework v{}", orpheus::version::VERSION);

    // Parse command line early to detect headless mode
    bool headless = false;
    bool auto_connect = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--connect" || arg == "-c") {
            auto_connect = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Orpheus - DMA Reversing Framework\n"
                      << "\nUsage: orpheus [options]\n"
                      << "\nOptions:\n"
                      << "  --headless       Run in headless mode (MCP server only, no GUI)\n"
                      << "  -c, --connect    Auto-connect to DMA device on startup\n"
                      << "  -h, --help       Show this help message\n"
                      << std::endl;
            return 0;
        }
    }

    // CRITICAL: Extract embedded DLLs before anything else
    LOG_INFO("Initializing runtime environment...");
    auto& runtime = orpheus::RuntimeManager::Instance();

    if (!runtime.Initialize()) {
        LOG_ERROR("Failed to initialize runtime (DLL extraction failed)");
        LOG_ERROR("Check that you have write permissions to the temp directory");
        return 1;
    }

    LOG_INFO("Runtime initialized: {}", runtime.GetAppDataDirectory().string());

    // Load telemetry settings and send startup ping (async, non-blocking)
    orpheus::Telemetry::Instance().LoadFromConfig();
    orpheus::Telemetry::Instance().SendStartupPing();

    // Register cleanup on exit
    std::atexit([]() {
        LOG_INFO("Cleaning up...");
        // Send shutdown ping with session duration (blocking)
        orpheus::Telemetry::Instance().SendShutdownPing();
        orpheus::RuntimeManager::Instance().Cleanup();
    });

    // Handle signals for clean shutdown
#ifdef PLATFORM_WINDOWS
    SetConsoleCtrlHandler([](DWORD type) -> BOOL {
        if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
            LOG_INFO("Received shutdown signal");
            g_headless_running.store(false);
            orpheus::Telemetry::Instance().SendShutdownPing();
            orpheus::RuntimeManager::Instance().Cleanup();
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#endif

    // =========================================================================
    // Headless mode: OrpheusCore + MCP server, no GUI
    // =========================================================================
    if (headless) {
        LOG_INFO("Running in headless mode (no GUI)");

        // Create and initialize headless core
        auto core = std::make_unique<orpheus::OrpheusCore>();
        if (!core->Initialize()) {
            LOG_ERROR("Failed to initialize OrpheusCore");
            return 1;
        }

        // Auto-connect DMA if requested
        if (auto_connect) {
            LOG_INFO("Auto-connecting to DMA device...");
            if (core->GetDMA()->Initialize("fpga")) {
                LOG_INFO("DMA connection established");
            } else {
                LOG_WARN("Failed to auto-connect to DMA device");
            }
        }

        // Load MCP config and start server
        auto mcp_config = std::make_unique<orpheus::mcp::MCPConfig>();
        orpheus::mcp::MCPServer::LoadConfig(*mcp_config);
        mcp_config->enabled = true;

        if (mcp_config->require_auth && mcp_config->api_key.empty()) {
            mcp_config->api_key = orpheus::mcp::MCPServer::GenerateApiKey();
            LOG_INFO("Generated new API key (save config to persist)");
        }

        auto mcp_server = std::make_unique<orpheus::mcp::MCPServer>(core.get());
        if (!mcp_server->Start(*mcp_config)) {
            LOG_ERROR("Failed to start MCP server");
            return 1;
        }

        LOG_INFO("MCP server running on {}:{}", mcp_config->bind_address, mcp_config->port);
        LOG_INFO("Press Ctrl+C to stop");

        // Block until interrupted
        while (g_headless_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_INFO("Shutting down headless mode...");
        mcp_server->Stop();
        mcp_server.reset();
        core->Shutdown();
        core.reset();

        LOG_INFO("Headless shutdown complete");
        return 0;
    }

    // =========================================================================
    // GUI mode: full Application with ImGui
    // =========================================================================

    // Create and initialize the application
    orpheus::ui::Application app;

    std::string window_title = "Orpheus v" + std::string(orpheus::version::VERSION);
    if (!app.Initialize(window_title)) {
        LOG_ERROR("Failed to initialize application");
        return 1;
    }

    LOG_INFO("Application initialized successfully");

    // Auto-connect DMA if requested
    if (auto_connect) {
        LOG_INFO("Auto-connecting to DMA device...");
        if (app.GetDMA()->Initialize("fpga")) {
            LOG_INFO("DMA connection established");
        } else {
            LOG_WARN("Failed to auto-connect to DMA device");
        }
    }

    // Run the main application loop
    LOG_INFO("Starting main loop");
    int result = app.Run();

    LOG_INFO("Application exiting with code {}", result);
    return result;
}
