/**
 * MCP Server Implementation - Core Infrastructure
 *
 * This file contains the core MCP server infrastructure:
 * - Constructor/destructor
 * - Configuration management
 * - Route setup
 * - Helper functions
 *
 * Handler implementations are in separate files:
 * - mcp_handlers_introspection.cpp
 * - mcp_handlers_bookmarks.cpp
 * - mcp_handlers_memory.cpp
 * - mcp_handlers_scan.cpp
 * - mcp_handlers_analysis.cpp
 * - mcp_handlers_emulation.cpp
 * - mcp_handlers_rtti.cpp
 * - mcp_handlers_cs2_schema.cpp
 * - mcp_handlers_cs2_entity.cpp
 *
 * IMPORTANT: When adding new routes/endpoints, remember to also update:
 *   orpheus\mcp_bridge.js
 * The bridge file defines the MCP tool schemas that Claude uses.
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "core/runtime_manager.h"
#include "emulation/emulator.h"  // Required for unique_ptr destructor
#include "dumper/cs2_schema.h"   // Required for destructor cleanup
#include "utils/logger.h"
#include "version.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>

using json = nlohmann::json;

namespace orpheus::mcp {

// ============================================================================
// Constructor / Destructor
// ============================================================================

MCPServer::MCPServer(ui::Application* app)
    : app_(app) {
}

MCPServer::~MCPServer() {
    Stop();

    // Clean up CS2 schema dumper if allocated
    if (cs2_schema_) {
        delete static_cast<orpheus::dumper::CS2SchemaDumper*>(cs2_schema_);
        cs2_schema_ = nullptr;
    }
}

// ============================================================================
// Configuration Management
// ============================================================================

std::string MCPServer::GenerateApiKey() {
    // Use std::random_device directly for cryptographic randomness
    // On Linux, this uses /dev/urandom. On Windows, it uses CryptGenRandom.
    std::random_device rd;

    std::stringstream ss;
    ss << "oph_";

    // Generate 32 random bytes (256 bits) using random_device directly
    // random_device returns 32-bit values, so we need 8 calls for 256 bits
    for (int i = 0; i < 8; i++) {
        ss << std::hex << std::setw(8) << std::setfill('0') << rd();
    }

    return ss.str();
}

std::string MCPServer::GetDefaultConfigPath() {
    auto config_dir = RuntimeManager::Instance().GetConfigDirectory();
    return (config_dir / "mcp_config.json").string();
}

bool MCPServer::SaveConfig(const MCPConfig& config, const std::string& filepath) {
    std::string actual_path = filepath.empty() ? GetDefaultConfigPath() : filepath;
    try {
        json j;
        j["enabled"] = config.enabled;
        j["port"] = config.port;
        j["api_key"] = config.api_key;
        j["require_auth"] = config.require_auth;
        j["allow_read"] = config.allow_read;
        j["allow_write"] = config.allow_write;
        j["allow_scan"] = config.allow_scan;
        j["allow_dump"] = config.allow_dump;
        j["allow_disasm"] = config.allow_disasm;
        j["allow_emu"] = config.allow_emu;
        j["allow_rtti"] = config.allow_rtti;
        j["allow_cs2_schema"] = config.allow_cs2_schema;

        std::ofstream file(actual_path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file for writing: {}", actual_path);
            return false;
        }

        file << j.dump(4);
        file.close();

        LOG_INFO("MCP config saved to {}", actual_path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save MCP config: {}", e.what());
        return false;
    }
}

bool MCPServer::LoadConfig(MCPConfig& config, const std::string& filepath) {
    std::string actual_path = filepath.empty() ? GetDefaultConfigPath() : filepath;
    try {
        std::ifstream file(actual_path);
        if (!file.is_open()) {
            LOG_WARN("MCP config file not found: {}", actual_path);
            return false;
        }

        json j;
        file >> j;
        file.close();

        config.enabled = j.value("enabled", false);
        config.port = j.value("port", 8765);
        config.api_key = j.value("api_key", "");
        config.require_auth = j.value("require_auth", true);
        config.allow_read = j.value("allow_read", true);
        config.allow_write = j.value("allow_write", false);
        config.allow_scan = j.value("allow_scan", true);
        config.allow_dump = j.value("allow_dump", true);
        config.allow_disasm = j.value("allow_disasm", true);
        config.allow_emu = j.value("allow_emu", true);
        config.allow_rtti = j.value("allow_rtti", true);
        config.allow_cs2_schema = j.value("allow_cs2_schema", true);

        LOG_INFO("MCP config loaded from {}", filepath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load MCP config: {}", e.what());
        return false;
    }
}

bool MCPServer::ValidateAuth(const std::string& provided_key) const {
    if (!config_.require_auth) {
        return true;
    }

    return provided_key == config_.api_key;
}

void MCPServer::SetConfig(const MCPConfig& config) {
    config_ = config;
}

// ============================================================================
// Response Helpers
// ============================================================================

std::string MCPServer::CreateErrorResponse(const std::string& error) {
    json response;
    response["success"] = false;
    response["error"] = error;
    return response.dump();
}

std::string MCPServer::CreateSuccessResponse(const std::string& data) {
    json response;
    response["success"] = true;
    response["data"] = json::parse(data);
    return response.dump();
}

// ============================================================================
// Address Formatting Helpers
// ============================================================================

MCPServer::AddressContext MCPServer::ResolveAddressContext(uint32_t pid, uint64_t address) {
    AddressContext ctx;

    std::lock_guard<std::mutex> lock(modules_mutex_);

    // Refresh cache if needed
    if (cached_modules_pid_ != pid) {
        auto* dma = app_->GetDMA();
        if (dma && dma->IsConnected()) {
            cached_modules_ = dma->GetModuleList(pid);
            cached_modules_pid_ = pid;
        }
    }

    // Find containing module
    for (const auto& mod : cached_modules_) {
        if (address >= mod.base_address && address < mod.base_address + mod.size) {
            ctx.module_name = mod.name;
            ctx.module_base = mod.base_address;
            ctx.offset = address - mod.base_address;
            ctx.resolved = true;
            break;
        }
    }

    return ctx;
}

std::string MCPServer::FormatAddress(uint64_t address) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << address;
    return ss.str();
}

std::string MCPServer::FormatAddressWithContext(uint32_t pid, uint64_t address) {
    auto ctx = ResolveAddressContext(pid, address);
    if (ctx.resolved) {
        std::stringstream ss;
        ss << ctx.module_name << "+0x" << std::hex << ctx.offset;
        return ss.str();
    }
    return FormatAddress(address);
}

// ============================================================================
// Route Setup
// ============================================================================

void MCPServer::SetupRoutes() {
    auto* server = static_cast<httplib::Server*>(http_server_);

    // Middleware for authentication
    server->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (config_.require_auth) {
            auto auth = req.get_header_value("Authorization");
            if (auth.empty() || !auth.starts_with("Bearer ")) {
                res.status = 401;
                res.set_content(CreateErrorResponse("Missing or invalid Authorization header"), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }

            std::string provided_key = auth.substr(7);  // Remove "Bearer "
            if (!ValidateAuth(provided_key)) {
                res.status = 403;
                res.set_content(CreateErrorResponse("Invalid API key"), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Helper macros for registering routes with permission checks
    // Eliminates repetitive boilerplate for each endpoint
    #define ROUTE_POST(path, handler) \
        server->Post(path, [this](const httplib::Request& req, httplib::Response& res) { \
            res.set_content(handler(req.body), "application/json"); \
        })

    #define ROUTE_POST_PERM(path, perm, perm_name, handler) \
        server->Post(path, [this](const httplib::Request& req, httplib::Response& res) { \
            if (!config_.perm) { \
                res.status = 403; \
                res.set_content(CreateErrorResponse(perm_name " operations disabled"), "application/json"); \
                return; \
            } \
            res.set_content(handler(req.body), "application/json"); \
        })

    #define ROUTE_GET(path, handler) \
        server->Get(path, [this](const httplib::Request& req, httplib::Response& res) { \
            res.set_content(handler(req.body), "application/json"); \
        })

    // Health check with version info
    server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json response;
        response["status"] = "ok";
        response["service"] = "Orpheus MCP Server";
        response["version"] = orpheus::version::VERSION;
        response["version_full"] = orpheus::version::VERSION_FULL;
        response["git_hash"] = orpheus::version::GIT_HASH_SHORT;
        response["build_date"] = orpheus::version::BUILD_DATE;
        response["platform"] = orpheus::version::PLATFORM;
        res.set_content(response.dump(), "application/json");
    });

    // Dedicated version endpoint with full details
    server->Get("/version", [](const httplib::Request&, httplib::Response& res) {
        json response;
        response["version"] = orpheus::version::VERSION;
        response["version_full"] = orpheus::version::VERSION_FULL;
        response["version_major"] = orpheus::version::VERSION_MAJOR;
        response["version_minor"] = orpheus::version::VERSION_MINOR;
        response["version_patch"] = orpheus::version::VERSION_PATCH;
        response["git_hash"] = orpheus::version::GIT_HASH;
        response["git_hash_short"] = orpheus::version::GIT_HASH_SHORT;
        response["git_branch"] = orpheus::version::GIT_BRANCH;
        response["git_dirty"] = orpheus::version::GIT_DIRTY;
        response["build_date"] = orpheus::version::BUILD_DATE;
        response["build_timestamp"] = orpheus::version::BUILD_TIMESTAMP;
        response["platform"] = orpheus::version::PLATFORM;
        response["build_info"] = orpheus::version::GetBuildInfo();
        res.set_content(response.dump(), "application/json");
    });

    // Core tool endpoints (no permission check)
    ROUTE_GET("/tools/processes", HandleGetProcesses);
    ROUTE_POST("/tools/modules", HandleGetModules);
    ROUTE_POST("/tools/memory_regions", HandleGetMemoryRegions);

    // Memory operations
    ROUTE_POST_PERM("/tools/read_memory", allow_read, "Read", HandleReadMemory);
    ROUTE_POST_PERM("/tools/write_memory", allow_write, "Write", HandleWriteMemory);
    ROUTE_POST_PERM("/tools/resolve_pointer", allow_read, "Read", HandleResolvePointerChain);

    // Scan operations (sync and async variants)
    ROUTE_POST_PERM("/tools/scan_pattern", allow_scan, "Scan", HandleScanPattern);
    ROUTE_POST_PERM("/tools/scan_pattern_async", allow_scan, "Scan", HandleScanPatternAsync);
    ROUTE_POST_PERM("/tools/scan_strings", allow_scan, "Scan", HandleScanStrings);
    ROUTE_POST_PERM("/tools/scan_strings_async", allow_scan, "Scan", HandleScanStringsAsync);
    ROUTE_POST_PERM("/tools/find_xrefs", allow_scan, "Scan", HandleFindXrefs);

    // Disassembly, decompile & dump
    ROUTE_POST_PERM("/tools/disassemble", allow_disasm, "Disassembly", HandleDisassemble);
    ROUTE_POST_PERM("/tools/decompile", allow_disasm, "Decompile", HandleDecompile);
    ROUTE_POST_PERM("/tools/dump_module", allow_dump, "Dump", HandleDumpModule);
    ROUTE_POST_PERM("/tools/generate_signature", allow_disasm, "Signature", HandleGenerateSignature);

    // Memory diff engine
    ROUTE_POST_PERM("/tools/memory_snapshot", allow_read, "Memory", HandleMemorySnapshot);
    ROUTE_POST("/tools/memory_snapshot_list", HandleMemorySnapshotList);
    ROUTE_POST("/tools/memory_snapshot_delete", HandleMemorySnapshotDelete);
    ROUTE_POST_PERM("/tools/memory_diff", allow_read, "Memory", HandleMemoryDiff);

    // RTTI analysis
    ROUTE_POST_PERM("/tools/rtti_parse_vtable", allow_rtti, "RTTI", HandleRTTIParseVTable);
    ROUTE_POST_PERM("/tools/rtti_scan", allow_rtti, "RTTI", HandleRTTIScan);
    ROUTE_POST_PERM("/tools/rtti_scan_module", allow_rtti, "RTTI", HandleRTTIScanModule);
    ROUTE_POST_PERM("/tools/rtti_cache_list", allow_rtti, "RTTI", HandleRTTICacheList);
    ROUTE_POST_PERM("/tools/rtti_cache_query", allow_rtti, "RTTI", HandleRTTICacheQuery);
    ROUTE_POST_PERM("/tools/rtti_cache_get", allow_rtti, "RTTI", HandleRTTICacheGet);
    ROUTE_POST_PERM("/tools/rtti_cache_clear", allow_rtti, "RTTI", HandleRTTICacheClear);
    ROUTE_POST_PERM("/tools/read_vtable", allow_rtti, "RTTI", HandleReadVTable);

    // Emulation
    ROUTE_POST_PERM("/tools/emu_create", allow_emu, "Emulation", HandleEmuCreate);
    ROUTE_POST_PERM("/tools/emu_destroy", allow_emu, "Emulation", HandleEmuDestroy);
    ROUTE_POST_PERM("/tools/emu_map_module", allow_emu, "Emulation", HandleEmuMapModule);
    ROUTE_POST_PERM("/tools/emu_map_region", allow_emu, "Emulation", HandleEmuMapRegion);
    ROUTE_POST_PERM("/tools/emu_set_registers", allow_emu, "Emulation", HandleEmuSetRegisters);
    ROUTE_POST_PERM("/tools/emu_get_registers", allow_emu, "Emulation", HandleEmuGetRegisters);
    ROUTE_POST_PERM("/tools/emu_run", allow_emu, "Emulation", HandleEmuRun);
    ROUTE_POST_PERM("/tools/emu_run_instructions", allow_emu, "Emulation", HandleEmuRunInstructions);
    ROUTE_POST_PERM("/tools/emu_reset", allow_emu, "Emulation", HandleEmuReset);

    // Bookmarks (no permission check - always available)
    ROUTE_GET("/tools/bookmarks", HandleBookmarkList);
    ROUTE_POST("/tools/bookmarks/add", HandleBookmarkAdd);
    ROUTE_POST("/tools/bookmarks/remove", HandleBookmarkRemove);
    ROUTE_POST("/tools/bookmarks/update", HandleBookmarkUpdate);

    // CS2 Schema endpoints
    // cs2_schema_init and cs2_schema_dump removed - use cs2_init one-shot instead
    ROUTE_POST_PERM("/tools/cs2_schema_get_offset", allow_cs2_schema, "CS2Schema", HandleCS2SchemaGetOffset);
    ROUTE_POST_PERM("/tools/cs2_schema_find_class", allow_cs2_schema, "CS2Schema", HandleCS2SchemaFindClass);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_list", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheList);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_query", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheQuery);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_get", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheGet);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_clear", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheClear);

    // CS2 Consolidated init (one-shot initialization)
    ROUTE_POST_PERM("/tools/cs2_init", allow_cs2_schema, "CS2", HandleCS2Init);

    // CS2 Entity tools (RTTI + Schema bridge)
    // cs2_entity_init removed - use cs2_init one-shot instead
    ROUTE_POST_PERM("/tools/cs2_identify", allow_cs2_schema, "CS2Entity", HandleCS2Identify);
    ROUTE_POST_PERM("/tools/cs2_read_field", allow_cs2_schema, "CS2Entity", HandleCS2ReadField);
    ROUTE_POST_PERM("/tools/cs2_inspect", allow_cs2_schema, "CS2Entity", HandleCS2Inspect);
    ROUTE_POST_PERM("/tools/cs2_get_local_player", allow_cs2_schema, "CS2Entity", HandleCS2GetLocalPlayer);
    ROUTE_POST_PERM("/tools/cs2_get_entity", allow_cs2_schema, "CS2Entity", HandleCS2GetEntity);
    ROUTE_POST_PERM("/tools/cs2_list_players", allow_cs2_schema, "CS2Entity", HandleCS2ListPlayers);
    ROUTE_POST_PERM("/tools/cs2_get_game_state", allow_cs2_schema, "CS2Entity", HandleCS2GetGameState);

    // Function recovery
    ROUTE_POST_PERM("/tools/recover_functions", allow_disasm, "Analysis", HandleRecoverFunctions);
    ROUTE_POST_PERM("/tools/get_function_at", allow_disasm, "Analysis", HandleGetFunctionAt);
    ROUTE_POST_PERM("/tools/get_function_containing", allow_disasm, "Analysis", HandleGetFunctionContaining);
    ROUTE_POST_PERM("/tools/find_function_bounds", allow_read, "Analysis", HandleFindFunctionBounds);

    // CFG analysis
    ROUTE_POST_PERM("/tools/build_cfg", allow_disasm, "Analysis", HandleBuildCFG);
    ROUTE_POST_PERM("/tools/get_cfg_node", allow_disasm, "Analysis", HandleGetCFGNode);

    // Expression evaluation
    ROUTE_POST_PERM("/tools/evaluate_expression", allow_read, "Utility", HandleEvaluateExpression);

    // Task management (always allowed - no special permissions needed)
    ROUTE_POST("/tools/task_status", HandleTaskStatus);
    ROUTE_POST("/tools/task_cancel", HandleTaskCancel);
    ROUTE_POST("/tools/task_list", HandleTaskList);
    ROUTE_POST("/tools/task_cleanup", HandleTaskCleanup);

    #undef ROUTE_POST
    #undef ROUTE_POST_PERM
    #undef ROUTE_GET

    LOG_INFO("MCP server routes configured");
}

// ============================================================================
// Server Lifecycle
// ============================================================================

void MCPServer::ServerThread() {
    auto* server = static_cast<httplib::Server*>(http_server_);

    LOG_INFO("MCP server listening on port {}", config_.port);

    // Bind to 0.0.0.0 to allow remote connections (e.g., from laptop)
    if (!server->listen("0.0.0.0", config_.port)) {
        LOG_ERROR("Failed to start MCP server on port {}", config_.port);
        running_.store(false);
    }
}

bool MCPServer::Start(const MCPConfig& config) {
    if (running_.load()) {
        LOG_WARN("MCP server already running");
        return false;
    }

    config_ = config;

    if (!config_.enabled) {
        LOG_INFO("MCP server disabled in configuration");
        return false;
    }

    // Create HTTP server
    http_server_ = new httplib::Server();

    SetupRoutes();

    running_.store(true);
    server_thread_ = std::thread(&MCPServer::ServerThread, this);

    LOG_INFO("MCP server started on port {} (auth: {})", config_.port, config_.require_auth ? "enabled" : "disabled");
    return true;
}

void MCPServer::Stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (http_server_) {
        static_cast<httplib::Server*>(http_server_)->stop();
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    if (http_server_) {
        delete static_cast<httplib::Server*>(http_server_);
        http_server_ = nullptr;
    }

    LOG_INFO("MCP server stopped");
}

} // namespace orpheus::mcp
