#include "core/runtime_manager.h"
#include "core/dma_interface.h"
#include "ui/application.h"
#include "utils/logger.h"
#include "utils/telemetry.h"
#include "version.h"

#include <iostream>
#include <cstdlib>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

int main(int argc, char** argv) {
    // CRITICAL: Initialize logging first
    if (!orpheus::Logger::Instance().Initialize()) {
        std::cerr << "Failed to initialize logging system" << std::endl;
        return 1;
    }

    LOG_INFO("Starting Orpheus DMA Reversing Framework v{}", orpheus::version::VERSION);

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
            orpheus::Telemetry::Instance().SendShutdownPing();
            orpheus::RuntimeManager::Instance().Cleanup();
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#endif

    // Create and initialize the application
    orpheus::ui::Application app;

    std::string window_title = "Orpheus v" + std::string(orpheus::version::VERSION);
    if (!app.Initialize(window_title)) {
        LOG_ERROR("Failed to initialize application");
        return 1;
    }

    LOG_INFO("Application initialized successfully");

    // Check command line for auto-connect
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--connect" || arg == "-c") {
            LOG_INFO("Auto-connecting to DMA device...");
            if (app.GetDMA()->Initialize("fpga")) {
                LOG_INFO("DMA connection established");
            } else {
                LOG_WARN("Failed to auto-connect to DMA device");
            }
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

    // Run the main application loop
    LOG_INFO("Starting main loop");
    int result = app.Run();

    LOG_INFO("Application exiting with code {}", result);
    return result;
}
