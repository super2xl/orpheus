#include "core/runtime_manager.h"
#include "core/dma_interface.h"
#include "core/orpheus_core.h"
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

static std::atomic<bool> g_running{true};

int main(int argc, char** argv) {
    if (!orpheus::Logger::Instance().Initialize()) {
        std::cerr << "Failed to initialize logging system" << std::endl;
        return 1;
    }

    LOG_INFO("Starting Orpheus DMA Reversing Framework v{}", orpheus::version::VERSION);

    bool auto_connect = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--connect" || arg == "-c") {
            auto_connect = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Orpheus - DMA Reversing Framework\n"
                      << "\nUsage: orpheus [options]\n"
                      << "\nOptions:\n"
                      << "  -c, --connect    Auto-connect to DMA device on startup\n"
                      << "  -h, --help       Show this help message\n"
                      << std::endl;
            return 0;
        }
    }

    // Extract embedded DLLs
    LOG_INFO("Initializing runtime environment...");
    auto& runtime = orpheus::RuntimeManager::Instance();
    if (!runtime.Initialize()) {
        LOG_ERROR("Failed to initialize runtime (DLL extraction failed)");
        return 1;
    }
    LOG_INFO("Runtime initialized: {}", runtime.GetAppDataDirectory().string());

    orpheus::Telemetry::Instance().LoadFromConfig();
    orpheus::Telemetry::Instance().SendStartupPing();

    std::atexit([]() {
        LOG_INFO("Cleaning up...");
        orpheus::Telemetry::Instance().SendShutdownPing();
        orpheus::RuntimeManager::Instance().Cleanup();
    });

#ifdef PLATFORM_WINDOWS
    SetConsoleCtrlHandler([](DWORD type) -> BOOL {
        if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
            LOG_INFO("Received shutdown signal");
            g_running.store(false);
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#endif

    // Initialize OrpheusCore
    auto core = std::make_unique<orpheus::OrpheusCore>();
    if (!core->Initialize()) {
        LOG_ERROR("Failed to initialize OrpheusCore");
        return 1;
    }

    if (auto_connect) {
        LOG_INFO("Auto-connecting to DMA device...");
        if (core->GetDMA()->Initialize("fpga")) {
            LOG_INFO("DMA connection established");
        } else {
            LOG_WARN("Failed to auto-connect to DMA device");
        }
    }

    // Start MCP server
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

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("Shutting down...");
    mcp_server->Stop();
    mcp_server.reset();
    core->Shutdown();
    core.reset();

    LOG_INFO("Shutdown complete");
    return 0;
}
