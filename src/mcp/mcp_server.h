#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <map>

namespace orpheus {

class DMAInterface;
struct ModuleInfo;

namespace emulation {
    class Emulator;
}

namespace ui {
    class Application;
}

namespace mcp {

/**
 * MCPConfig - MCP server configuration
 */
struct MCPConfig {
    bool enabled = false;
    uint16_t port = 8765;
    std::string api_key;
    bool require_auth = true;

    // Feature flags
    bool allow_read = true;
    bool allow_write = false;  // Disabled by default for safety
    bool allow_scan = true;
    bool allow_dump = true;
    bool allow_disasm = true;
    bool allow_emu = true;        // Emulation
    bool allow_rtti = true;       // RTTI analysis
    bool allow_cs2_schema = true; // CS2 schema dumper
};

/**
 * MCPServer - Embedded HTTP server for Model Context Protocol
 *
 * Provides REST API endpoints for Claude/LLM to interact with Orpheus:
 * - Read/write memory via DMA
 * - Scan for patterns/strings
 * - Dump modules
 * - Disassemble code
 * - Manage bookmarks
 */
class MCPServer {
public:
    explicit MCPServer(ui::Application* app);
    ~MCPServer();

    // Disable copy/move
    MCPServer(const MCPServer&) = delete;
    MCPServer& operator=(const MCPServer&) = delete;

    /**
     * Start the MCP server
     */
    bool Start(const MCPConfig& config);

    /**
     * Stop the MCP server
     */
    void Stop();

    /**
     * Check if server is running
     */
    bool IsRunning() const { return running_.load(); }

    /**
     * Get current configuration
     */
    const MCPConfig& GetConfig() const { return config_; }

    /**
     * Update configuration (requires restart)
     */
    void SetConfig(const MCPConfig& config);

    /**
     * Generate a new API key
     */
    static std::string GenerateApiKey();

    /**
     * Save config to file
     */
    static bool SaveConfig(const MCPConfig& config, const std::string& filepath = "mcp_config.json");

    /**
     * Load config from file
     */
    static bool LoadConfig(MCPConfig& config, const std::string& filepath = "mcp_config.json");

private:
    // Server thread
    void ServerThread();

    // Authentication
    bool ValidateAuth(const std::string& provided_key) const;

    // Endpoint handlers
    void SetupRoutes();

    // Tool endpoints
    std::string HandleReadMemory(const std::string& body);
    std::string HandleWriteMemory(const std::string& body);
    std::string HandleScanPattern(const std::string& body);
    std::string HandleScanStrings(const std::string& body);
    std::string HandleDumpModule(const std::string& body);
    std::string HandleDisassemble(const std::string& body);
    std::string HandleGetProcesses(const std::string& body);
    std::string HandleGetModules(const std::string& body);
    std::string HandleFindXrefs(const std::string& body);
    std::string HandleResolvePointerChain(const std::string& body);
    std::string HandleGetMemoryRegions(const std::string& body);

    // RTTI analysis endpoints
    std::string HandleRTTIParseVTable(const std::string& body);
    std::string HandleRTTIScan(const std::string& body);
    std::string HandleRTTIScanModule(const std::string& body);

    // RTTI cache endpoints
    std::string HandleRTTICacheList(const std::string& body);
    std::string HandleRTTICacheQuery(const std::string& body);
    std::string HandleRTTICacheGet(const std::string& body);
    std::string HandleRTTICacheClear(const std::string& body);

    // Emulation tool endpoints
    std::string HandleEmuCreate(const std::string& body);
    std::string HandleEmuDestroy(const std::string& body);
    std::string HandleEmuMapModule(const std::string& body);
    std::string HandleEmuMapRegion(const std::string& body);
    std::string HandleEmuSetRegisters(const std::string& body);
    std::string HandleEmuGetRegisters(const std::string& body);
    std::string HandleEmuRun(const std::string& body);
    std::string HandleEmuRunInstructions(const std::string& body);
    std::string HandleEmuReset(const std::string& body);

    // Bookmark endpoints
    std::string HandleBookmarkList(const std::string& body);
    std::string HandleBookmarkAdd(const std::string& body);
    std::string HandleBookmarkRemove(const std::string& body);
    std::string HandleBookmarkUpdate(const std::string& body);

    // CS2 Schema endpoints
    std::string HandleCS2SchemaInit(const std::string& body);
    std::string HandleCS2SchemaDump(const std::string& body);
    std::string HandleCS2SchemaGetOffset(const std::string& body);
    std::string HandleCS2SchemaFindClass(const std::string& body);

    // CS2 Schema cache endpoints
    std::string HandleCS2SchemaCacheList(const std::string& body);
    std::string HandleCS2SchemaCacheQuery(const std::string& body);
    std::string HandleCS2SchemaCacheGet(const std::string& body);
    std::string HandleCS2SchemaCacheClear(const std::string& body);

    // CS2 Entity tools (RTTI + Schema bridge)
    std::string HandleCS2EntityInit(const std::string& body);
    std::string HandleCS2Identify(const std::string& body);
    std::string HandleCS2ReadField(const std::string& body);
    std::string HandleCS2Inspect(const std::string& body);
    std::string HandleCS2GetLocalPlayer(const std::string& body);
    std::string HandleCS2GetEntity(const std::string& body);

    // Utility
    std::string CreateErrorResponse(const std::string& error);
    std::string CreateSuccessResponse(const std::string& data);

    // Address context helper - resolves address to module+offset
    struct AddressContext {
        std::string module_name;
        uint64_t module_base = 0;
        uint64_t offset = 0;
        bool resolved = false;
    };
    AddressContext ResolveAddressContext(uint32_t pid, uint64_t address);
    std::string FormatAddress(uint64_t address);
    std::string FormatAddressWithContext(uint32_t pid, uint64_t address);

    // Cached modules for context resolution
    std::vector<orpheus::ModuleInfo> cached_modules_;
    uint32_t cached_modules_pid_ = 0;

    ui::Application* app_;
    MCPConfig config_;

    std::atomic<bool> running_{false};
    std::thread server_thread_;

    void* http_server_ = nullptr;  // httplib::Server pointer (opaque)

    // Emulator instance (one per MCP server)
    std::unique_ptr<emulation::Emulator> emulator_;
    uint32_t emulator_pid_ = 0;  // PID the emulator is attached to

    // RTTI cache helpers
    std::string GetCacheDirectory();
    std::string GetCacheFilePath(const std::string& module_name, uint32_t module_size);
    bool SaveRTTICache(const std::string& module_name, uint32_t module_size, const std::string& json_data);
    std::string LoadRTTICache(const std::string& module_name, uint32_t module_size);
    bool CacheExists(const std::string& module_name, uint32_t module_size);

    // CS2 Schema instance (lazy-initialized)
    void* cs2_schema_ = nullptr;  // dumper::CS2SchemaDumper* (opaque)
    uint32_t cs2_schema_pid_ = 0;

    // CS2 Schema cache helpers
    std::string GetCS2SchemaCacheDirectory();
    std::string GetCS2SchemaCacheFilePath(const std::string& scope_name, uint32_t module_size);
    bool SaveCS2SchemaCache(const std::string& scope_name, uint32_t module_size, const std::string& json_data);
    std::string LoadCS2SchemaCache(const std::string& scope_name, uint32_t module_size);
    bool CS2SchemaCacheExists(const std::string& scope_name, uint32_t module_size);
    uint32_t GetModuleSizeForScope(const std::string& scope_name);  // Get size of DLL corresponding to scope

    // CS2 Entity system cache (discovered via pattern scanning)
    struct CS2EntityCache {
        uint64_t entity_system = 0;         // CGameEntitySystem*
        uint64_t local_player_controller = 0; // LocalPlayerController array base
        uint64_t client_base = 0;           // client.dll base
        uint32_t client_size = 0;           // client.dll size
        bool initialized = false;
    };
    CS2EntityCache cs2_entity_cache_;

    // RTTI+Schema helper: identify class from pointer
    std::string IdentifyClassFromPointer(uint32_t pid, uint64_t ptr, uint64_t module_base = 0);
    // RTTI+Schema helper: strip "class " or "struct " prefix
    static std::string StripTypePrefix(const std::string& name);
};

} // namespace mcp
} // namespace orpheus
