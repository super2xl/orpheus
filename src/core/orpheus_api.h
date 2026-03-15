#pragma once

#ifdef _WIN32
  #ifdef ORPHEUS_EXPORTS
    #define ORPHEUS_API __declspec(dllexport)
  #else
    #define ORPHEUS_API __declspec(dllimport)
  #endif
#else
  #define ORPHEUS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the core (logger, runtime manager, telemetry, OrpheusCore)
// Returns 0 on success, non-zero on error
ORPHEUS_API int orpheus_init(void);

// Start the MCP server on the given port
// api_key can be NULL for no auth
// Returns 0 on success
ORPHEUS_API int orpheus_start_server(int port, const char* api_key);

// Stop the MCP server
ORPHEUS_API void orpheus_stop_server(void);

// Connect to DMA device
// device_type: "fpga" or file path
// Returns 0 on success
ORPHEUS_API int orpheus_connect_dma(const char* device_type);

// Check if DMA is connected
// Returns 1 if connected, 0 if not
ORPHEUS_API int orpheus_is_connected(void);

// Get server port (returns 0 if not running)
ORPHEUS_API int orpheus_get_port(void);

// Full shutdown and cleanup
ORPHEUS_API void orpheus_shutdown(void);

#ifdef __cplusplus
}
#endif
