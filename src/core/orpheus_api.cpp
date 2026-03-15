#include "orpheus_api.h"
#include "orpheus_core.h"
#include "dma_interface.h"
#include "runtime_manager.h"
#include "../mcp/mcp_server.h"
#include "../utils/logger.h"
#include "../utils/telemetry.h"

#include <memory>
#include <mutex>
#include <string>

// Global state — protected by mutex for thread safety
static std::mutex g_mutex;
static std::unique_ptr<orpheus::OrpheusCore> g_core;
static std::unique_ptr<orpheus::mcp::MCPServer> g_mcp;
static int g_port = 0;
static bool g_initialized = false;

int orpheus_init(void) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_initialized) {
        return 0; // Already initialized
    }

    // 1. Initialize logger
    if (!orpheus::Logger::Instance().Initialize()) {
        return 1;
    }

    LOG_INFO("orpheus_init: Starting initialization (DLL mode)");

    // 2. Initialize runtime manager (DLL extraction, AppData setup)
    auto& runtime = orpheus::RuntimeManager::Instance();
    if (!runtime.Initialize()) {
        LOG_ERROR("orpheus_init: Failed to initialize runtime");
        return 2;
    }
    LOG_INFO("orpheus_init: Runtime initialized: {}", runtime.GetAppDataDirectory().string());

    // 3. Telemetry
    orpheus::Telemetry::Instance().LoadFromConfig();
    orpheus::Telemetry::Instance().SendStartupPing();

    // 4. Initialize OrpheusCore
    g_core = std::make_unique<orpheus::OrpheusCore>();
    if (!g_core->Initialize()) {
        LOG_ERROR("orpheus_init: Failed to initialize OrpheusCore");
        g_core.reset();
        return 3;
    }

    g_initialized = true;
    LOG_INFO("orpheus_init: Initialization complete");
    return 0;
}

int orpheus_start_server(int port, const char* api_key) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized || !g_core) {
        return 1; // Not initialized
    }

    if (g_mcp) {
        return 0; // Already running
    }

    // Load existing config or create default
    auto config = orpheus::mcp::MCPConfig{};
    orpheus::mcp::MCPServer::LoadConfig(config);

    config.enabled = true;
    config.port = static_cast<uint16_t>(port);

    if (api_key && api_key[0] != '\0') {
        config.api_key = api_key;
        config.require_auth = true;
    } else {
        // If config has auth enabled but no key provided, generate one
        if (config.require_auth && config.api_key.empty()) {
            config.api_key = orpheus::mcp::MCPServer::GenerateApiKey();
            LOG_INFO("orpheus_start_server: Generated new API key");
        }
    }

    g_mcp = std::make_unique<orpheus::mcp::MCPServer>(g_core.get());
    if (!g_mcp->Start(config)) {
        LOG_ERROR("orpheus_start_server: Failed to start MCP server on port {}", port);
        g_mcp.reset();
        return 2;
    }

    g_port = port;
    LOG_INFO("orpheus_start_server: MCP server running on port {}", port);
    return 0;
}

void orpheus_stop_server(void) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_mcp) {
        g_mcp->Stop();
        g_mcp.reset();
        g_port = 0;
        LOG_INFO("orpheus_stop_server: MCP server stopped");
    }
}

int orpheus_connect_dma(const char* device_type) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized || !g_core) {
        return 1;
    }

    auto* dma = g_core->GetDMA();
    if (!dma) {
        return 2;
    }

    std::string dev = (device_type && device_type[0] != '\0') ? device_type : "fpga";

    if (dma->Initialize(dev)) {
        LOG_INFO("orpheus_connect_dma: Connected to DMA device ({})", dev);
        return 0;
    } else {
        LOG_WARN("orpheus_connect_dma: Failed to connect to DMA device ({})", dev);
        return 3;
    }
}

int orpheus_is_connected(void) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized || !g_core) {
        return 0;
    }

    auto* dma = g_core->GetDMA();
    return (dma && dma->IsConnected()) ? 1 : 0;
}

int orpheus_get_port(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_port;
}

void orpheus_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized) {
        return;
    }

    LOG_INFO("orpheus_shutdown: Starting shutdown");

    // Stop MCP server first
    if (g_mcp) {
        g_mcp->Stop();
        g_mcp.reset();
        g_port = 0;
    }

    // Shutdown core
    if (g_core) {
        g_core->Shutdown();
        g_core.reset();
    }

    // Telemetry shutdown ping
    orpheus::Telemetry::Instance().SendShutdownPing();

    // Cleanup runtime
    orpheus::RuntimeManager::Instance().Cleanup();

    g_initialized = false;
    LOG_INFO("orpheus_shutdown: Shutdown complete");
}
