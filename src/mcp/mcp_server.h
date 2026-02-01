#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "../utils/cache_manager.h"

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
    std::string bind_address = "127.0.0.1";  // Default to localhost for security

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
     * Get the default config file path (in AppData/config)
     */
    static std::string GetDefaultConfigPath();

    /**
     * Save config to file (defaults to AppData/config/mcp_config.json)
     */
    static bool SaveConfig(const MCPConfig& config, const std::string& filepath = "");

    /**
     * Load config from file (defaults to AppData/config/mcp_config.json)
     */
    static bool LoadConfig(MCPConfig& config, const std::string& filepath = "");

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
    std::string HandleScanPatternAsync(const std::string& body);
    std::string HandleScanStrings(const std::string& body);
    std::string HandleScanStringsAsync(const std::string& body);
    std::string HandleDumpModule(const std::string& body);
    std::string HandleDisassemble(const std::string& body);
    std::string HandleDecompile(const std::string& body);
    std::string HandleGenerateSignature(const std::string& body);

    // Memory diff tools
    std::string HandleMemorySnapshot(const std::string& body);
    std::string HandleMemorySnapshotList(const std::string& body);
    std::string HandleMemorySnapshotDelete(const std::string& body);
    std::string HandleMemoryDiff(const std::string& body);

    // Memory cache tools
    std::string HandleCacheStats(const std::string& body);
    std::string HandleCacheConfig(const std::string& body);
    std::string HandleCacheClear(const std::string& body);

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

    // VTable helper tool
    std::string HandleReadVTable(const std::string& body);

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

    // CS2 Schema query endpoints (use cs2_init for initialization)
    std::string HandleCS2SchemaGetOffset(const std::string& body);
    std::string HandleCS2SchemaFindClass(const std::string& body);

    // CS2 Schema cache endpoints
    std::string HandleCS2SchemaCacheList(const std::string& body);
    std::string HandleCS2SchemaCacheQuery(const std::string& body);
    std::string HandleCS2SchemaCacheGet(const std::string& body);
    std::string HandleCS2SchemaCacheClear(const std::string& body);

    // CS2 Consolidated init (one-shot)
    std::string HandleCS2Init(const std::string& body);

    // CS2 Entity tools (RTTI + Schema bridge) - use cs2_init for initialization
    std::string HandleCS2Identify(const std::string& body);
    std::string HandleCS2ReadField(const std::string& body);
    std::string HandleCS2Inspect(const std::string& body);
    std::string HandleCS2GetLocalPlayer(const std::string& body);
    std::string HandleCS2GetEntity(const std::string& body);
    std::string HandleCS2ListPlayers(const std::string& body);
    std::string HandleCS2GetGameState(const std::string& body);

    // Function recovery endpoints
    std::string HandleRecoverFunctions(const std::string& body);
    std::string HandleGetFunctionAt(const std::string& body);
    std::string HandleGetFunctionContaining(const std::string& body);
    std::string HandleFindFunctionBounds(const std::string& body);

    // CFG analysis endpoints
    std::string HandleBuildCFG(const std::string& body);
    std::string HandleGetCFGNode(const std::string& body);

    // Expression evaluation
    std::string HandleEvaluateExpression(const std::string& body);

    // Task management endpoints
    std::string HandleTaskStatus(const std::string& body);
    std::string HandleTaskCancel(const std::string& body);
    std::string HandleTaskList(const std::string& body);
    std::string HandleTaskCleanup(const std::string& body);

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
    // Protected by modules_mutex_ for thread-safe access from HTTP handlers
    mutable std::mutex modules_mutex_;
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

    // Cache managers (unified cache system)
    // Protected by cache_mutex_ for thread-safe access from HTTP handlers
    mutable std::mutex cache_mutex_;
    utils::CacheManager rtti_cache_{"rtti", "RTTI"};
    utils::CacheManager cs2_schema_cache_{"cs2_schema", "CS2 schema"};
    utils::CacheManager function_cache_{"functions", "Function recovery"};

    // CS2 Schema instance (lazy-initialized)
    void* cs2_schema_ = nullptr;  // dumper::CS2SchemaDumper* (opaque)
    uint32_t cs2_schema_pid_ = 0;

    // CS2 scope size lookup helper
    uint32_t GetModuleSizeForScope(const std::string& scope_name);

    // CS2 Entity system cache (discovered via pattern scanning)
    struct CS2EntityCache {
        uint64_t entity_system = 0;         // CGameEntitySystem*
        uint64_t local_player_controller = 0; // LocalPlayerController array base
        uint64_t client_base = 0;           // client.dll base
        uint32_t client_size = 0;           // client.dll size
        bool initialized = false;
    };
    CS2EntityCache cs2_entity_cache_;

    // In-memory schema cache for fast field lookups (loaded from disk cache on cs2_init)
    struct SchemaFieldInfo {
        std::string name;
        std::string type;
        uint32_t offset;
    };
    struct SchemaClassInfo {
        std::string name;
        std::string scope;  // e.g., "client.dll"
        std::string parent;
        std::vector<SchemaFieldInfo> fields;
    };
    mutable std::mutex schema_mem_cache_mutex_;
    std::unordered_map<std::string, SchemaClassInfo> schema_mem_cache_;  // key: lowercase class name
    bool schema_mem_cache_loaded_ = false;

    // Helper to load schema into memory cache
    void LoadSchemaIntoMemory();
    // Helper to find class in memory cache (case-insensitive)
    const SchemaClassInfo* FindSchemaClass(const std::string& class_name) const;

    // RTTI+Schema helper: identify class from pointer
    std::string IdentifyClassFromPointer(uint32_t pid, uint64_t ptr, uint64_t module_base = 0);
    // RTTI+Schema helper: strip "class " or "struct " prefix
    static std::string StripTypePrefix(const std::string& name);

    // Memory snapshot storage for diff engine
    struct MemorySnapshot {
        std::string name;
        uint32_t pid;
        uint64_t address;
        std::vector<uint8_t> data;
        std::chrono::system_clock::time_point timestamp;
    };
    mutable std::mutex snapshot_mutex_;
    std::map<std::string, MemorySnapshot> snapshots_;
};

} // namespace mcp
} // namespace orpheus
