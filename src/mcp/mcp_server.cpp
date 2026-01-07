/**
 * MCP Server Implementation
 *
 * IMPORTANT: When adding new routes/endpoints, remember to also update:
 *   orpheus\mcp_bridge.js
 * The bridge file defines the MCP tool schemas that Claude uses.
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "analysis/pattern_scanner.h"
#include "analysis/string_scanner.h"
#include "analysis/disassembler.h"
#include "analysis/rtti_parser.h"
#include "emulation/emulator.h"
#include "dumper/cs2_schema.h"
#include "utils/logger.h"
#include "utils/bookmarks.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <chrono>

using json = nlohmann::json;

namespace orpheus::mcp {

MCPServer::MCPServer(ui::Application* app)
    : app_(app) {
}

MCPServer::~MCPServer() {
    Stop();
}

std::string MCPServer::GenerateApiKey() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    std::stringstream ss;
    ss << "oph_";
    for (int i = 0; i < 4; i++) {
        ss << std::hex << std::setw(16) << std::setfill('0') << dist(gen);
    }

    return ss.str();
}

bool MCPServer::SaveConfig(const MCPConfig& config, const std::string& filepath) {
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

        std::ofstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file for writing: {}", filepath);
            return false;
        }

        file << j.dump(4);
        file.close();

        LOG_INFO("MCP config saved to {}", filepath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save MCP config: {}", e.what());
        return false;
    }
}

bool MCPServer::LoadConfig(MCPConfig& config, const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_WARN("MCP config file not found: {}", filepath);
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

    // Health check
    server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json response;
        response["status"] = "ok";
        response["service"] = "Orpheus MCP Server";
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

    // Scan operations
    ROUTE_POST_PERM("/tools/scan_pattern", allow_scan, "Scan", HandleScanPattern);
    ROUTE_POST_PERM("/tools/scan_strings", allow_scan, "Scan", HandleScanStrings);
    ROUTE_POST_PERM("/tools/find_xrefs", allow_scan, "Scan", HandleFindXrefs);

    // Disassembly & dump
    ROUTE_POST_PERM("/tools/disassemble", allow_disasm, "Disassembly", HandleDisassemble);
    ROUTE_POST_PERM("/tools/dump_module", allow_dump, "Dump", HandleDumpModule);

    // RTTI analysis
    ROUTE_POST_PERM("/tools/rtti_parse_vtable", allow_rtti, "RTTI", HandleRTTIParseVTable);
    ROUTE_POST_PERM("/tools/rtti_scan", allow_rtti, "RTTI", HandleRTTIScan);
    ROUTE_POST_PERM("/tools/rtti_scan_module", allow_rtti, "RTTI", HandleRTTIScanModule);
    ROUTE_POST_PERM("/tools/rtti_cache_list", allow_rtti, "RTTI", HandleRTTICacheList);
    ROUTE_POST_PERM("/tools/rtti_cache_query", allow_rtti, "RTTI", HandleRTTICacheQuery);
    ROUTE_POST_PERM("/tools/rtti_cache_get", allow_rtti, "RTTI", HandleRTTICacheGet);
    ROUTE_POST_PERM("/tools/rtti_cache_clear", allow_rtti, "RTTI", HandleRTTICacheClear);

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
    ROUTE_POST_PERM("/tools/cs2_schema_init", allow_cs2_schema, "CS2Schema", HandleCS2SchemaInit);
    ROUTE_POST_PERM("/tools/cs2_schema_dump", allow_cs2_schema, "CS2Schema", HandleCS2SchemaDump);
    ROUTE_POST_PERM("/tools/cs2_schema_get_offset", allow_cs2_schema, "CS2Schema", HandleCS2SchemaGetOffset);
    ROUTE_POST_PERM("/tools/cs2_schema_find_class", allow_cs2_schema, "CS2Schema", HandleCS2SchemaFindClass);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_list", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheList);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_query", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheQuery);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_get", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheGet);
    ROUTE_POST_PERM("/tools/cs2_schema_cache_clear", allow_cs2_schema, "CS2Schema", HandleCS2SchemaCacheClear);

    // CS2 Entity tools (RTTI + Schema bridge)
    ROUTE_POST_PERM("/tools/cs2_entity_init", allow_cs2_schema, "CS2Entity", HandleCS2EntityInit);
    ROUTE_POST_PERM("/tools/cs2_identify", allow_cs2_schema, "CS2Entity", HandleCS2Identify);
    ROUTE_POST_PERM("/tools/cs2_read_field", allow_cs2_schema, "CS2Entity", HandleCS2ReadField);
    ROUTE_POST_PERM("/tools/cs2_inspect", allow_cs2_schema, "CS2Entity", HandleCS2Inspect);
    ROUTE_POST_PERM("/tools/cs2_get_local_player", allow_cs2_schema, "CS2Entity", HandleCS2GetLocalPlayer);
    ROUTE_POST_PERM("/tools/cs2_get_entity", allow_cs2_schema, "CS2Entity", HandleCS2GetEntity);

    #undef ROUTE_POST
    #undef ROUTE_POST_PERM
    #undef ROUTE_GET

    LOG_INFO("MCP server routes configured");
}

std::string MCPServer::HandleReadMemory(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        size_t size = req["size"];
        std::string format = req.value("format", "auto");  // auto, hex, bytes, hexdump

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer (0x0)");
        }
        if (size == 0) {
            return CreateErrorResponse("Invalid size: cannot read 0 bytes");
        }
        if (size > 16 * 1024 * 1024) {  // 16MB limit
            return CreateErrorResponse("Size too large: maximum read is 16MB");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        auto data = dma->ReadMemory(pid, address, size);
        if (data.empty()) {
            std::stringstream err;
            err << "Failed to read memory at " << FormatAddress(address)
                << " (size: " << size << " bytes) in process " << proc_info->name
                << " - address may be invalid, unmapped, or protected";
            return CreateErrorResponse(err.str());
        }

        json result;
        result["address"] = FormatAddress(address);
        result["context"] = FormatAddressWithContext(pid, address);
        result["size"] = data.size();

        // Smart format selection based on size and request
        // "auto" = hex for small reads (<=64 bytes), hexdump for larger
        // "hex" = compact hex string only
        // "bytes" = JSON byte array
        // "hexdump" = IDA-style formatted dump
        bool use_hex = (format == "hex") || (format == "auto" && data.size() <= 64);
        bool use_bytes = (format == "bytes");
        bool use_hexdump = (format == "hexdump") || (format == "auto" && data.size() > 64);

        if (use_hex || use_hexdump) {
            // Compact hex string
            std::stringstream hex_compact;
            for (uint8_t b : data) {
                hex_compact << std::hex << std::setw(2) << std::setfill('0') << (int)b;
            }
            result["hex"] = hex_compact.str();
        }

        if (use_bytes) {
            // JSON byte array (for programmatic processing)
            json bytes = json::array();
            for (uint8_t b : data) {
                bytes.push_back(b);
            }
            result["bytes"] = bytes;
        }

        if (use_hexdump) {
            // IDA-style hexdump (for larger reads)
            std::stringstream hexdump;
            const size_t bytes_per_line = 16;

            for (size_t i = 0; i < data.size(); i += bytes_per_line) {
                hexdump << std::hex << std::setw(16) << std::setfill('0')
                       << (address + i) << "  ";

                for (size_t j = 0; j < bytes_per_line; j++) {
                    if (i + j < data.size()) {
                        hexdump << std::hex << std::setw(2) << std::setfill('0')
                               << static_cast<int>(data[i + j]) << " ";
                    } else {
                        hexdump << "   ";
                    }
                    if (j == 7) hexdump << " ";
                }

                hexdump << " |";
                for (size_t j = 0; j < bytes_per_line && i + j < data.size(); j++) {
                    uint8_t byte = data[i + j];
                    hexdump << (byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '.');
                }
                hexdump << "|\n";
            }
            result["hexdump"] = hexdump.str();
        }

        // Type interpretations only for small reads (<=16 bytes) where they're meaningful
        if (data.size() <= 16) {
            if (data.size() >= 4) {
                result["as_int32"] = *reinterpret_cast<int32_t*>(data.data());
                result["as_float"] = *reinterpret_cast<float*>(data.data());
            }
            if (data.size() >= 8) {
                result["as_int64"] = *reinterpret_cast<int64_t*>(data.data());
                result["as_ptr"] = FormatAddress(*reinterpret_cast<uint64_t*>(data.data()));
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleWriteMemory(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        std::string hex_data = req["data"];

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer (0x0)");
        }
        if (hex_data.empty()) {
            return CreateErrorResponse("Invalid data: no bytes to write");
        }
        if (hex_data.length() % 2 != 0) {
            return CreateErrorResponse("Invalid hex data: odd number of characters (must be pairs)");
        }

        // Convert hex string to bytes with validation
        std::vector<uint8_t> data;
        for (size_t i = 0; i < hex_data.length(); i += 2) {
            std::string byte_str = hex_data.substr(i, 2);
            try {
                data.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
            } catch (...) {
                return CreateErrorResponse("Invalid hex data at position " + std::to_string(i) + ": '" + byte_str + "'");
            }
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        bool success = dma->WriteMemory(pid, address, data);
        if (!success) {
            std::stringstream err;
            err << "Failed to write " << data.size() << " bytes at " << FormatAddress(address)
                << " in process " << proc_info->name
                << " - address may be invalid or memory is protected";
            return CreateErrorResponse(err.str());
        }

        json result;
        result["address"] = req["address"];
        result["bytes_written"] = data.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleScanPattern(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);
        uint32_t size = req["size"];
        std::string pattern = req["pattern"];

        // Validate parameters
        if (pattern.empty()) {
            return CreateErrorResponse("Invalid pattern: pattern string is empty");
        }
        if (base == 0) {
            return CreateErrorResponse("Invalid base address: cannot scan from NULL (0x0)");
        }
        if (size == 0) {
            return CreateErrorResponse("Invalid size: cannot scan 0 bytes");
        }
        if (size > 512 * 1024 * 1024) {  // 512MB limit
            return CreateErrorResponse("Size too large: maximum scan region is 512MB");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        auto compiled = analysis::PatternScanner::Compile(pattern);
        if (!compiled) {
            return CreateErrorResponse("Invalid pattern syntax: '" + pattern + "' - use IDA-style format like '48 8B ?? 74 ?? ?? ?? ??' where ?? are wildcards");
        }

        auto data = dma->ReadMemory(pid, base, size);
        if (data.empty()) {
            std::stringstream err;
            err << "Failed to read scan region at " << FormatAddress(base)
                << " (size: " << size << " bytes) - region may be unmapped or protected";
            return CreateErrorResponse(err.str());
        }

        auto results = analysis::PatternScanner::Scan(data, *compiled, base, 100);

        json result;
        result["pattern"] = pattern;
        result["base"] = req["base"];
        result["count"] = results.size();

        json addresses = json::array();
        for (uint64_t addr : results) {
            std::stringstream ss;
            ss << "0x" << std::hex << addr;
            addresses.push_back(ss.str());
        }
        result["addresses"] = addresses;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleScanStrings(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);
        uint32_t size = req["size"];
        int min_length = req.value("min_length", 4);

        // Validate parameters
        if (base == 0) {
            return CreateErrorResponse("Invalid base address: cannot scan from NULL (0x0)");
        }
        if (size == 0) {
            return CreateErrorResponse("Invalid size: cannot scan 0 bytes");
        }
        if (size > 512 * 1024 * 1024) {  // 512MB limit
            return CreateErrorResponse("Size too large: maximum scan region is 512MB");
        }
        if (min_length < 1 || min_length > 256) {
            return CreateErrorResponse("Invalid min_length: must be between 1 and 256");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        auto data = dma->ReadMemory(pid, base, size);
        if (data.empty()) {
            std::stringstream err;
            err << "Failed to read scan region at " << FormatAddress(base)
                << " (size: " << size << " bytes) - region may be unmapped or protected";
            return CreateErrorResponse(err.str());
        }

        analysis::StringScanOptions opts;
        opts.min_length = min_length;

        auto results = analysis::StringScanner::Scan(data, opts, base);

        json result;
        result["base"] = req["base"];
        result["count"] = results.size();

        json strings = json::array();
        for (const auto& str : results) {
            json s;
            std::stringstream ss;
            ss << "0x" << std::hex << str.address;
            s["address"] = ss.str();
            s["value"] = str.value;
            s["type"] = str.type == analysis::StringType::ASCII ? "ASCII" : "UTF16";
            strings.push_back(s);
        }
        result["strings"] = strings;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTIParseVTable(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t vtable_address = std::stoull(req["vtable_address"].get<std::string>(), nullptr, 16);
        uint64_t module_base = 0;
        if (req.contains("module_base")) {
            module_base = std::stoull(req["module_base"].get<std::string>(), nullptr, 16);
        }

        // Validate parameters
        if (vtable_address == 0) {
            return CreateErrorResponse("Invalid vtable_address: cannot parse NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // If module_base not provided, try to find the containing module
        if (module_base == 0) {
            auto modules = dma->GetModuleList(pid);
            for (const auto& mod : modules) {
                if (vtable_address >= mod.base_address && vtable_address < mod.base_address + mod.size) {
                    module_base = mod.base_address;
                    break;
                }
            }
            if (module_base == 0) {
                return CreateErrorResponse("Could not determine module base for vtable address. Please provide module_base parameter.");
            }
        }

        // Create RTTI parser with DMA read function
        analysis::RTTIParser parser(
            [dma, pid](uint64_t addr, size_t size) {
                return dma->ReadMemory(pid, addr, size);
            },
            module_base
        );

        auto info = parser.ParseVTable(vtable_address);
        if (!info) {
            return CreateErrorResponse("No valid RTTI found at vtable address " + FormatAddress(vtable_address) +
                                        " - address may not point to a vtable with RTTI");
        }

        json result;
        result["vtable_address"] = FormatAddress(info->vtable_address);
        result["col_address"] = FormatAddress(info->col_address);
        result["mangled_name"] = info->mangled_name;
        result["demangled_name"] = info->demangled_name;
        result["vftable_offset"] = info->vftable_offset;
        result["has_virtual_base"] = info->has_virtual_base;
        result["is_multiple_inheritance"] = info->is_multiple_inheritance;
        result["base_classes"] = info->base_classes;

        // ClassInformer-style enhanced output
        result["method_count"] = info->method_count;
        result["flags"] = info->GetFlags();           // "M", "V", "MV", or ""
        result["hierarchy"] = info->GetHierarchyString();  // "ClassName: Base1, Base2, ..."

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTIScan(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);
        size_t size = req["size"];
        size_t max_results = req.value("max_results", 100);

        // Validate parameters
        if (base == 0) {
            return CreateErrorResponse("Invalid base address: cannot scan from NULL (0x0)");
        }
        if (size == 0) {
            return CreateErrorResponse("Invalid size: cannot scan 0 bytes");
        }
        if (size > 256 * 1024 * 1024) {  // 256MB limit for RTTI scan
            return CreateErrorResponse("Size too large: maximum RTTI scan region is 256MB");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // Create RTTI parser - pass 0 to auto-detect module base from COL self_rva
        analysis::RTTIParser parser(
            [dma, pid](uint64_t addr, size_t sz) {
                return dma->ReadMemory(pid, addr, sz);
            },
            0  // Auto-detect from COL's self_rva
        );

        // Scan for vtables
        std::vector<analysis::RTTIClassInfo> found_classes;
        parser.ScanForVTables(base, size, [&](const analysis::RTTIClassInfo& info) {
            if (found_classes.size() < max_results) {
                found_classes.push_back(info);
            }
        });

        json result;
        result["base"] = FormatAddress(base);
        result["size"] = size;
        result["count"] = found_classes.size();

        json classes_array = json::array();
        for (const auto& info : found_classes) {
            json cls;
            cls["vtable_address"] = FormatAddress(info.vtable_address);
            cls["method_count"] = info.method_count;
            cls["flags"] = info.GetFlags();
            cls["demangled_name"] = info.demangled_name;
            cls["hierarchy"] = info.GetHierarchyString();
            classes_array.push_back(cls);
        }
        result["classes"] = classes_array;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTIScanModule(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t module_base = std::stoull(req["module_base"].get<std::string>(), nullptr, 16);
        bool force_rescan = req.value("force_rescan", false);

        // Validate parameters
        if (module_base == 0) {
            return CreateErrorResponse("Invalid module_base: cannot scan from NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // Find module info for name and size
        auto modules = dma->GetModuleList(pid);
        std::string module_name;
        uint32_t module_size = 0;
        for (const auto& mod : modules) {
            if (mod.base_address == module_base) {
                module_name = mod.name;
                module_size = mod.size;
                break;
            }
        }
        if (module_name.empty()) {
            module_name = "unknown";
            module_size = 0;
        }

        // Check cache first (unless force_rescan)
        if (!force_rescan && module_size > 0 && CacheExists(module_name, module_size)) {
            std::string cached = LoadRTTICache(module_name, module_size);
            if (!cached.empty()) {
                json cache_data = json::parse(cached);

                // Return summary only
                json result;
                result["status"] = "cached";
                result["module"] = module_name;
                result["module_base"] = FormatAddress(module_base);
                result["module_size"] = module_size;
                result["cache_file"] = GetCacheFilePath(module_name, module_size);
                result["summary"] = cache_data.value("summary", json::object());
                result["count"] = cache_data.contains("classes") ? cache_data["classes"].size() : 0;
                result["hint"] = "Use rtti_cache_query to search classes by name";

                LOG_INFO("RTTI cache hit for {} ({} classes)", module_name,
                    cache_data.contains("classes") ? cache_data["classes"].size() : 0);

                return CreateSuccessResponse(result.dump());
            }
        }

        // Cache miss or force_rescan - perform scan
        LOG_INFO("RTTI scanning {} at 0x{:X}...", module_name, module_base);

        analysis::RTTIParser parser(
            [dma, pid](uint64_t addr, size_t sz) {
                return dma->ReadMemory(pid, addr, sz);
            },
            0  // Auto-detect from COL's self_rva
        );

        // Get PE sections for info
        auto sections = parser.GetPESections(module_base);

        // Scan entire module (no limit for caching)
        std::vector<analysis::RTTIClassInfo> found_classes;
        parser.ScanModule(module_base, [&](const analysis::RTTIClassInfo& info) {
            found_classes.push_back(info);
        });

        // Build summary stats
        int multiple_inheritance = 0, virtual_bases = 0;
        uint32_t max_methods = 0, total_methods = 0;
        std::string largest_class;
        for (const auto& info : found_classes) {
            if (info.is_multiple_inheritance) multiple_inheritance++;
            if (info.has_virtual_base) virtual_bases++;
            total_methods += info.method_count;
            if (info.method_count > max_methods) {
                max_methods = info.method_count;
                largest_class = info.demangled_name;
            }
        }

        json summary = {
            {"total_classes", found_classes.size()},
            {"multiple_inheritance", multiple_inheritance},
            {"virtual_bases", virtual_bases},
            {"largest_vtable", max_methods},
            {"largest_class", largest_class},
            {"avg_methods", found_classes.empty() ? 0 : total_methods / found_classes.size()}
        };

        // Build cache data (full class list)
        json cache_data;
        cache_data["module"] = module_name;
        cache_data["module_base"] = FormatAddress(module_base);
        cache_data["module_size"] = module_size;
        cache_data["summary"] = summary;

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream time_ss;
        time_ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        cache_data["cached_at"] = time_ss.str();

        json sections_array = json::array();
        for (const auto& section : sections) {
            if (section.name == ".rdata" || section.name == ".data") {
                json sec;
                sec["name"] = section.name;
                sec["address"] = FormatAddress(section.virtual_address);
                sec["size"] = section.virtual_size;
                sections_array.push_back(sec);
            }
        }
        cache_data["sections_scanned"] = sections_array;

        // Store scan_base so we can compute RVAs and convert back later
        cache_data["scan_base"] = FormatAddress(module_base);

        json classes_array = json::array();
        for (const auto& info : found_classes) {
            json cls;
            // Store RVA (relative to module base) instead of absolute address
            // This allows cache to work across game restarts with different ASLR bases
            uint64_t rva = info.vtable_address - module_base;
            cls["vtable_rva"] = rva;
            cls["methods"] = info.method_count;
            cls["flags"] = info.GetFlags();
            cls["type"] = info.demangled_name;
            cls["hierarchy"] = info.GetHierarchyString();
            classes_array.push_back(cls);
        }
        cache_data["classes"] = classes_array;

        // Save to cache
        if (module_size > 0) {
            SaveRTTICache(module_name, module_size, cache_data.dump(2));
        }

        // Return summary only (not full class list)
        json result;
        result["status"] = "scanned";
        result["module"] = module_name;
        result["module_base"] = FormatAddress(module_base);
        result["module_size"] = module_size;
        result["count"] = found_classes.size();
        result["summary"] = summary;
        if (module_size > 0) {
            result["cache_file"] = GetCacheFilePath(module_name, module_size);
        }
        result["hint"] = "Use rtti_cache_query to search classes by name";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleDisassemble(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        int count = req.value("count", 20);

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: cannot disassemble NULL (0x0)");
        }
        if (count <= 0) {
            return CreateErrorResponse("Invalid count: must be at least 1");
        }
        if (count > 1000) {
            return CreateErrorResponse("Count too large: maximum is 1000 instructions");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        auto data = dma->ReadMemory(pid, address, count * 16);
        if (data.empty()) {
            std::stringstream err;
            err << "Failed to read code at " << FormatAddress(address)
                << " in process " << proc_info->name
                << " - address may point to invalid, unmapped, or non-executable memory";
            return CreateErrorResponse(err.str());
        }

        analysis::Disassembler disasm(true);  // x64
        auto instructions = disasm.Disassemble(data, address);

        json result;
        result["address"] = FormatAddress(address);
        result["context"] = FormatAddressWithContext(pid, address);
        result["count"] = std::min((int)instructions.size(), count);

        json instrs = json::array();
        for (size_t i = 0; i < std::min((size_t)count, instructions.size()); i++) {
            const auto& instr = instructions[i];
            json inst;

            // Compact format: addr, bytes, text, and optional target
            inst["addr"] = FormatAddress(instr.address);
            inst["bytes"] = analysis::disasm::FormatBytes(instr.bytes);
            inst["text"] = instr.mnemonic + (instr.operands.empty() ? "" : " " + instr.operands);

            // Only add type marker for control flow instructions
            if (instr.is_call) inst["type"] = "call";
            else if (instr.is_ret) inst["type"] = "ret";
            else if (instr.is_jump) inst["type"] = instr.is_conditional ? "jcc" : "jmp";

            // Resolve call/jump targets with context
            if ((instr.is_call || instr.is_jump) && instr.branch_target.has_value()) {
                inst["target"] = FormatAddressWithContext(pid, *instr.branch_target);
            }

            instrs.push_back(inst);
        }
        result["instructions"] = instrs;

        // Summary for agent convenience
        int calls = 0, jumps = 0, rets = 0;
        for (const auto& instr : instructions) {
            if (instr.is_call) calls++;
            if (instr.is_jump) jumps++;
            if (instr.is_ret) rets++;
        }
        result["summary"] = {
            {"total", result["count"]},
            {"calls", calls},
            {"jumps", jumps},
            {"returns", rets}
        };

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetProcesses(const std::string&) {
    try {
        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto processes = dma->GetProcessList();

        json result;
        result["count"] = processes.size();

        json procs = json::array();
        for (const auto& proc : processes) {
            json p;
            p["pid"] = proc.pid;
            p["name"] = proc.name;
            p["is_64bit"] = proc.is_64bit;

            std::stringstream ss;
            ss << "0x" << std::hex << proc.base_address;
            p["base"] = ss.str();

            procs.push_back(p);
        }
        result["processes"] = procs;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetModules(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto modules = dma->GetModuleList(pid);

        json result;
        result["pid"] = pid;
        result["count"] = modules.size();

        json mods = json::array();
        for (const auto& mod : modules) {
            json m;
            m["name"] = mod.name;

            std::stringstream ss_base, ss_entry;
            ss_base << "0x" << std::hex << mod.base_address;
            ss_entry << "0x" << std::hex << mod.entry_point;

            m["base"] = ss_base.str();
            m["size"] = mod.size;
            m["entry"] = ss_entry.str();

            mods.push_back(m);
        }
        result["modules"] = mods;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleFindXrefs(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t target = std::stoull(req["target"].get<std::string>(), nullptr, 16);
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);
        uint32_t size = req["size"];
        int max_results = req.value("max_results", 100);

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto data = dma->ReadMemory(pid, base, size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read memory");
        }

        json result;
        result["target"] = req["target"];
        result["base"] = req["base"];

        json xrefs = json::array();

        // Scan for direct references (little-endian 64-bit pointers)
        for (size_t i = 0; i + 8 <= data.size() && (int)xrefs.size() < max_results; i++) {
            uint64_t val = *reinterpret_cast<uint64_t*>(&data[i]);
            if (val == target) {
                json ref;
                uint64_t ref_addr = base + i;
                ref["address"] = FormatAddress(ref_addr);
                ref["type"] = "ptr64";
                ref["context"] = FormatAddressWithContext(pid, ref_addr);
                xrefs.push_back(ref);
            }
        }

        // Scan for 32-bit relative offsets (common in x64 RIP-relative addressing)
        for (size_t i = 0; i + 4 <= data.size() && (int)xrefs.size() < max_results; i++) {
            int32_t rel = *reinterpret_cast<int32_t*>(&data[i]);
            uint64_t computed = base + i + 4 + rel;  // RIP + 4 + offset
            if (computed == target) {
                json ref;
                uint64_t ref_addr = base + i;
                ref["address"] = FormatAddress(ref_addr);
                ref["type"] = "rel32";
                ref["context"] = FormatAddressWithContext(pid, ref_addr);
                xrefs.push_back(ref);
            }
        }

        result["count"] = xrefs.size();
        result["xrefs"] = xrefs;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleResolvePointerChain(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);

        // Offsets can be array of ints or hex strings
        std::vector<int64_t> offsets;
        if (req.contains("offsets")) {
            for (const auto& off : req["offsets"]) {
                if (off.is_string()) {
                    offsets.push_back(std::stoll(off.get<std::string>(), nullptr, 16));
                } else {
                    offsets.push_back(off.get<int64_t>());
                }
            }
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        json result;
        result["base"] = req["base"];

        json chain = json::array();

        uint64_t current = base;
        chain.push_back({
            {"step", 0},
            {"address", FormatAddress(current)},
            {"context", FormatAddressWithContext(pid, current)},
            {"operation", "base"}
        });

        for (size_t i = 0; i < offsets.size(); i++) {
            // Read pointer at current address
            auto ptr_opt = dma->Read<uint64_t>(pid, current);
            if (!ptr_opt) {
                result["error"] = "Failed to read pointer at step " + std::to_string(i);
                result["failed_at"] = FormatAddress(current);
                result["chain"] = chain;
                return CreateSuccessResponse(result.dump());
            }

            uint64_t ptr_value = *ptr_opt;
            chain.push_back({
                {"step", i + 1},
                {"address", FormatAddress(current)},
                {"value", FormatAddress(ptr_value)},
                {"operation", "deref"}
            });

            // Apply offset
            current = ptr_value + offsets[i];
            chain.push_back({
                {"step", i + 1},
                {"address", FormatAddress(current)},
                {"context", FormatAddressWithContext(pid, current)},
                {"operation", "offset"},
                {"offset", offsets[i]}
            });
        }

        result["final_address"] = FormatAddress(current);
        result["final_context"] = FormatAddressWithContext(pid, current);
        result["chain"] = chain;

        // Optionally read value at final address
        if (req.value("read_final", false)) {
            int read_size = req.value("read_size", 8);
            auto final_data = dma->ReadMemory(pid, current, read_size);
            if (!final_data.empty()) {
                std::stringstream hex;
                for (uint8_t b : final_data) {
                    hex << std::hex << std::setw(2) << std::setfill('0') << (int)b;
                }
                result["final_value"] = hex.str();

                if (read_size == 4 && final_data.size() == 4) {
                    result["final_as_int32"] = *reinterpret_cast<int32_t*>(final_data.data());
                    result["final_as_float"] = *reinterpret_cast<float*>(final_data.data());
                } else if (read_size == 8 && final_data.size() == 8) {
                    result["final_as_int64"] = *reinterpret_cast<int64_t*>(final_data.data());
                    result["final_as_double"] = *reinterpret_cast<double*>(final_data.data());
                }
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetMemoryRegions(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto regions = dma->GetMemoryRegions(pid);

        json result;
        result["pid"] = pid;
        result["count"] = regions.size();

        json regs = json::array();
        for (const auto& reg : regions) {
            json r;
            r["base"] = FormatAddress(reg.base_address);
            r["size"] = reg.size;
            r["size_hex"] = FormatAddress(reg.size);
            r["protection"] = reg.protection;
            r["type"] = reg.type;
            r["info"] = reg.info;
            regs.push_back(r);
        }
        result["regions"] = regs;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleDumpModule(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        std::string module_name = req["module"];
        std::string output_path = req.value("output", module_name + ".dump");

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto mod_opt = dma->GetModuleByName(pid, module_name);
        if (!mod_opt) {
            return CreateErrorResponse("Module not found: " + module_name);
        }

        auto data = dma->ReadMemory(pid, mod_opt->base_address, mod_opt->size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read module memory");
        }

        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) {
            return CreateErrorResponse("Failed to open output file: " + output_path);
        }

        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        file.close();

        json result;
        result["module"] = module_name;
        result["base"] = FormatAddress(mod_opt->base_address);
        result["size"] = mod_opt->size;
        result["output"] = output_path;
        result["bytes_written"] = data.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

// ============================================================================
// Address Context Helpers
// ============================================================================

MCPServer::AddressContext MCPServer::ResolveAddressContext(uint32_t pid, uint64_t address) {
    AddressContext ctx;

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

void MCPServer::SetConfig(const MCPConfig& config) {
    config_ = config;
}

// ============================================================================
// Emulation Tool Handlers
// ============================================================================

std::string MCPServer::HandleEmuCreate(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];

        // Optional config
        emulation::EmulatorConfig config;
        if (req.contains("stack_base")) {
            config.stack_base = std::stoull(req["stack_base"].get<std::string>(), nullptr, 16);
        }
        if (req.contains("stack_size")) {
            config.stack_size = req["stack_size"];
        }
        if (req.contains("max_instructions")) {
            config.max_instructions = req["max_instructions"];
        }
        if (req.contains("timeout_us")) {
            config.timeout_us = req["timeout_us"];
        }
        if (req.contains("lazy_mapping")) {
            config.lazy_mapping = req["lazy_mapping"];
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Destroy existing emulator if any
        emulator_.reset();

        // Create new emulator
        emulator_ = std::make_unique<emulation::Emulator>();
        if (!emulator_->Initialize(dma, pid, config)) {
            std::string error = emulator_->GetLastError();
            emulator_.reset();
            return CreateErrorResponse("Failed to initialize emulator: " + error);
        }

        emulator_pid_ = pid;

        json result;
        result["pid"] = pid;
        result["stack_base"] = FormatAddress(config.stack_base);
        result["stack_size"] = config.stack_size;
        result["max_instructions"] = config.max_instructions;
        result["timeout_us"] = config.timeout_us;
        result["lazy_mapping"] = config.lazy_mapping;
        result["status"] = "initialized";

        LOG_INFO("Emulator created for PID {}", pid);
        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuDestroy(const std::string&) {
    try {
        if (!emulator_) {
            return CreateErrorResponse("No emulator active");
        }

        uint32_t pid = emulator_pid_;
        emulator_.reset();
        emulator_pid_ = 0;

        json result;
        result["pid"] = pid;
        result["status"] = "destroyed";

        LOG_INFO("Emulator destroyed for PID {}", pid);
        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuMapModule(const std::string& body) {
    try {
        auto req = json::parse(body);
        std::string module_name = req["module"];

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        if (!emulator_->MapModule(module_name)) {
            return CreateErrorResponse("Failed to map module: " + emulator_->GetLastError());
        }

        json result;
        result["module"] = module_name;
        result["status"] = "mapped";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuMapRegion(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        size_t size = req["size"];

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        if (!emulator_->MapRegion(address, size)) {
            return CreateErrorResponse("Failed to map region: " + emulator_->GetLastError());
        }

        json result;
        result["address"] = FormatAddress(address);
        result["size"] = size;
        result["status"] = "mapped";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuSetRegisters(const std::string& body) {
    try {
        auto req = json::parse(body);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        json set_regs = json::object();

        // Process register values from request
        if (req.contains("registers")) {
            for (auto& [key, value] : req["registers"].items()) {
                uint64_t reg_value;
                if (value.is_string()) {
                    reg_value = std::stoull(value.get<std::string>(), nullptr, 16);
                } else {
                    reg_value = value.get<uint64_t>();
                }

                auto reg = emulation::ParseRegister(key);
                if (!reg) {
                    return CreateErrorResponse("Unknown register: " + key);
                }

                if (!emulator_->SetRegister(*reg, reg_value)) {
                    return CreateErrorResponse("Failed to set " + key + ": " + emulator_->GetLastError());
                }

                set_regs[key] = FormatAddress(reg_value);
            }
        }

        json result;
        result["registers_set"] = set_regs;
        result["status"] = "ok";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuGetRegisters(const std::string& body) {
    try {
        auto req = json::parse(body);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        json result;
        json regs = json::object();

        // If specific registers requested, get only those
        if (req.contains("registers") && req["registers"].is_array()) {
            for (const auto& name : req["registers"]) {
                std::string reg_name = name.get<std::string>();
                auto reg = emulation::ParseRegister(reg_name);
                if (reg) {
                    auto value = emulator_->GetRegister(*reg);
                    if (value) {
                        regs[reg_name] = FormatAddress(*value);
                    }
                }
            }
        } else {
            // Get all GP registers
            const std::vector<std::string> gp_regs = {
                "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
                "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
                "rip", "rflags"
            };

            for (const auto& name : gp_regs) {
                auto reg = emulation::ParseRegister(name);
                if (reg) {
                    auto value = emulator_->GetRegister(*reg);
                    if (value) {
                        regs[name] = FormatAddress(*value);
                    }
                }
            }
        }

        result["registers"] = regs;
        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuRun(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint64_t start_address = std::stoull(req["start_address"].get<std::string>(), nullptr, 16);
        uint64_t end_address = std::stoull(req["end_address"].get<std::string>(), nullptr, 16);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        auto emu_result = emulator_->Run(start_address, end_address);

        json result;
        result["success"] = emu_result.success;
        result["start_address"] = FormatAddress(start_address);
        result["end_address"] = FormatAddress(end_address);
        result["final_rip"] = FormatAddress(emu_result.final_rip);
        result["instructions_executed"] = emu_result.instructions_executed;

        if (!emu_result.success) {
            result["error"] = emu_result.error;
        }

        // Include register state
        json regs = json::object();
        for (const auto& [name, value] : emu_result.registers) {
            regs[name] = FormatAddress(value);
        }
        result["registers"] = regs;

        // Include accessed pages info
        auto& accessed = emulator_->GetAccessedPages();
        result["pages_accessed"] = accessed.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuRunInstructions(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint64_t start_address = std::stoull(req["start_address"].get<std::string>(), nullptr, 16);
        size_t count = req["count"];

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        auto emu_result = emulator_->RunInstructions(start_address, count);

        json result;
        result["success"] = emu_result.success;
        result["start_address"] = FormatAddress(start_address);
        result["requested_count"] = count;
        result["final_rip"] = FormatAddress(emu_result.final_rip);
        result["instructions_executed"] = emu_result.instructions_executed;

        if (!emu_result.success) {
            result["error"] = emu_result.error;
        }

        // Include register state
        json regs = json::object();
        for (const auto& [name, value] : emu_result.registers) {
            regs[name] = FormatAddress(value);
        }
        result["registers"] = regs;

        // Include accessed pages info
        auto& accessed = emulator_->GetAccessedPages();
        result["pages_accessed"] = accessed.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuReset(const std::string& body) {
    try {
        auto req = json::parse(body);
        bool full_reset = req.value("full", false);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        if (full_reset) {
            emulator_->Reset();
        } else {
            emulator_->ResetCPU();
        }

        json result;
        result["reset_type"] = full_reset ? "full" : "cpu_only";
        result["status"] = "ok";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

// ============================================================================
// RTTI Cache System
// ============================================================================

std::string MCPServer::GetCacheDirectory() {
    namespace fs = std::filesystem;
    fs::path cache_dir = fs::current_path() / "cache";
    if (!fs::exists(cache_dir)) {
        fs::create_directories(cache_dir);
    }
    return cache_dir.string();
}

std::string MCPServer::GetCacheFilePath(const std::string& module_name, uint32_t module_size) {
    namespace fs = std::filesystem;
    std::stringstream ss;
    ss << module_name << "_" << module_size << ".json";
    return (fs::path(GetCacheDirectory()) / ss.str()).string();
}

bool MCPServer::SaveRTTICache(const std::string& module_name, uint32_t module_size, const std::string& json_data) {
    std::string filepath = GetCacheFilePath(module_name, module_size);
    std::ofstream out(filepath);
    if (!out.is_open()) return false;
    out << json_data;
    out.close();
    LOG_INFO("RTTI cache saved: {}", filepath);
    return true;
}

std::string MCPServer::LoadRTTICache(const std::string& module_name, uint32_t module_size) {
    std::string filepath = GetCacheFilePath(module_name, module_size);
    std::ifstream in(filepath);
    if (!in.is_open()) return "";
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool MCPServer::CacheExists(const std::string& module_name, uint32_t module_size) {
    return std::filesystem::exists(GetCacheFilePath(module_name, module_size));
}

std::string MCPServer::HandleRTTICacheList(const std::string& body) {
    try {
        namespace fs = std::filesystem;
        std::string cache_dir = GetCacheDirectory();

        json result;
        json modules = json::array();

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() == ".json") {
                std::string filename = entry.path().filename().string();
                // Parse module name and size from filename (format: name_size.json)
                size_t last_underscore = filename.rfind('_');
                if (last_underscore != std::string::npos) {
                    std::string module_name = filename.substr(0, last_underscore);
                    std::string size_str = filename.substr(last_underscore + 1);
                    size_str = size_str.substr(0, size_str.length() - 5); // remove .json

                    // Read cache file to get class count
                    std::ifstream in(entry.path());
                    if (in.is_open()) {
                        json cache_data = json::parse(in);
                        json mod;
                        mod["module"] = module_name;
                        mod["size"] = std::stoul(size_str);
                        mod["classes"] = cache_data.contains("classes") ? cache_data["classes"].size() : 0;
                        mod["cache_file"] = entry.path().string();
                        mod["cached_at"] = cache_data.value("cached_at", "unknown");
                        modules.push_back(mod);
                    }
                }
            }
        }

        result["count"] = modules.size();
        result["modules"] = modules;
        result["cache_directory"] = cache_dir;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheQuery(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string query = req.value("query", "");
        std::string module_filter = req.value("module", "");
        int max_results = req.value("max_results", 100);
        uint32_t pid = req.value("pid", 0);  // Optional PID to resolve RVAs to absolute addresses

        if (query.empty()) {
            return CreateErrorResponse("Missing required parameter: query");
        }

        // Build module base lookup map if PID provided
        std::map<std::string, uint64_t> current_bases;
        if (pid > 0) {
            auto* dma = app_->GetDMA();
            if (dma && dma->IsConnected()) {
                auto modules = dma->GetModuleList(pid);
                for (const auto& mod : modules) {
                    std::string mod_lower = mod.name;
                    std::transform(mod_lower.begin(), mod_lower.end(), mod_lower.begin(), ::tolower);
                    current_bases[mod_lower] = mod.base_address;
                }
            }
        }

        // Convert query to lowercase for case-insensitive search
        std::string query_lower = query;
        std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

        namespace fs = std::filesystem;
        std::string cache_dir = GetCacheDirectory();

        json result;
        json matches = json::array();
        int total_searched = 0;

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().filename().string();
            size_t last_underscore = filename.rfind('_');
            if (last_underscore == std::string::npos) continue;

            std::string module_name = filename.substr(0, last_underscore);

            // Filter by module if specified
            if (!module_filter.empty()) {
                std::string filter_lower = module_filter;
                std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);
                std::string mod_lower = module_name;
                std::transform(mod_lower.begin(), mod_lower.end(), mod_lower.begin(), ::tolower);
                if (mod_lower.find(filter_lower) == std::string::npos) continue;
            }

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            // Get current base for this module (if PID provided)
            std::string mod_lower = module_name;
            std::transform(mod_lower.begin(), mod_lower.end(), mod_lower.begin(), ::tolower);
            uint64_t current_base = 0;
            if (current_bases.count(mod_lower)) {
                current_base = current_bases[mod_lower];
            }

            for (const auto& cls : cache_data["classes"]) {
                total_searched++;
                std::string type = cls.value("type", "");
                std::string type_lower = type;
                std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

                if (type_lower.find(query_lower) != std::string::npos) {
                    json match;
                    match["module"] = module_name;

                    // Convert RVA to absolute address if we have current base
                    // Support both new format (vtable_rva) and old format (vtable)
                    if (cls.contains("vtable_rva")) {
                        uint64_t rva = cls["vtable_rva"].get<uint64_t>();
                        if (current_base > 0) {
                            match["vtable"] = FormatAddress(current_base + rva);
                        } else {
                            // No PID provided - return RVA with prefix
                            std::stringstream ss;
                            ss << "RVA:0x" << std::hex << std::uppercase << rva;
                            match["vtable"] = ss.str();
                        }
                        match["vtable_rva"] = rva;
                    } else {
                        // Old cache format - return as-is
                        match["vtable"] = cls.value("vtable", "");
                    }

                    match["methods"] = cls.value("methods", 0);
                    match["flags"] = cls.value("flags", "");
                    match["type"] = type;
                    match["hierarchy"] = cls.value("hierarchy", "");
                    matches.push_back(match);

                    if ((int)matches.size() >= max_results) break;
                }
            }

            if ((int)matches.size() >= max_results) break;
        }

        result["query"] = query;
        result["count"] = matches.size();
        result["total_searched"] = total_searched;
        result["matches"] = matches;
        if (pid > 0) {
            result["pid"] = pid;
            result["addresses_resolved"] = !current_bases.empty();
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheGet(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string module_name = req.value("module", "");
        int max_results = req.value("max_results", 1000);
        uint32_t pid = req.value("pid", 0);  // Optional PID to resolve RVAs

        if (module_name.empty()) {
            return CreateErrorResponse("Missing required parameter: module");
        }

        // Get current module base if PID provided
        uint64_t current_base = 0;
        if (pid > 0) {
            auto* dma = app_->GetDMA();
            if (dma && dma->IsConnected()) {
                auto modules = dma->GetModuleList(pid);
                std::string mod_name_lower = module_name;
                std::transform(mod_name_lower.begin(), mod_name_lower.end(), mod_name_lower.begin(), ::tolower);
                for (const auto& mod : modules) {
                    std::string m_lower = mod.name;
                    std::transform(m_lower.begin(), m_lower.end(), m_lower.begin(), ::tolower);
                    if (m_lower == mod_name_lower) {
                        current_base = mod.base_address;
                        break;
                    }
                }
            }
        }

        namespace fs = std::filesystem;
        std::string cache_dir = GetCacheDirectory();

        // Find matching cache file
        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().filename().string();
            size_t last_underscore = filename.rfind('_');
            if (last_underscore == std::string::npos) continue;

            std::string cached_module = filename.substr(0, last_underscore);

            // Case-insensitive match
            std::string mod_lower = cached_module;
            std::transform(mod_lower.begin(), mod_lower.end(), mod_lower.begin(), ::tolower);
            std::string query_lower = module_name;
            std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

            if (mod_lower == query_lower) {
                std::ifstream in(entry.path());
                if (!in.is_open()) continue;

                json cache_data = json::parse(in);

                // Convert RVAs to absolute addresses if PID provided and we have current base
                if (current_base > 0 && cache_data.contains("classes")) {
                    cache_data["current_base"] = FormatAddress(current_base);
                    cache_data["addresses_resolved"] = true;
                    for (auto& cls : cache_data["classes"]) {
                        if (cls.contains("vtable_rva")) {
                            uint64_t rva = cls["vtable_rva"].get<uint64_t>();
                            cls["vtable"] = FormatAddress(current_base + rva);
                        }
                    }
                }

                // Limit results if needed
                if (cache_data.contains("classes") && (int)cache_data["classes"].size() > max_results) {
                    json limited = cache_data;
                    json limited_classes = json::array();
                    for (int i = 0; i < max_results && i < (int)cache_data["classes"].size(); i++) {
                        limited_classes.push_back(cache_data["classes"][i]);
                    }
                    limited["classes"] = limited_classes;
                    limited["truncated"] = true;
                    limited["total_classes"] = cache_data["classes"].size();
                    return CreateSuccessResponse(limited.dump());
                }

                return CreateSuccessResponse(cache_data.dump());
            }
        }

        return CreateErrorResponse("Cache not found for module: " + module_name);

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheClear(const std::string& body) {
    try {
        json req = json::parse(body);
        std::string module_filter = req.value("module", "");

        namespace fs = std::filesystem;
        std::string cache_dir = GetCacheDirectory();

        int cleared = 0;
        std::vector<std::string> cleared_files;

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().filename().string();

            if (module_filter.empty()) {
                // Clear all
                fs::remove(entry.path());
                cleared++;
                cleared_files.push_back(filename);
            } else {
                // Clear specific module
                size_t last_underscore = filename.rfind('_');
                if (last_underscore != std::string::npos) {
                    std::string cached_module = filename.substr(0, last_underscore);
                    std::string mod_lower = cached_module;
                    std::transform(mod_lower.begin(), mod_lower.end(), mod_lower.begin(), ::tolower);
                    std::string filter_lower = module_filter;
                    std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

                    if (mod_lower == filter_lower) {
                        fs::remove(entry.path());
                        cleared++;
                        cleared_files.push_back(filename);
                    }
                }
            }
        }

        json result;
        result["cleared"] = cleared;
        result["files"] = cleared_files;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

// ============================================================================
// Bookmark Handlers
// ============================================================================

std::string MCPServer::HandleBookmarkList(const std::string&) {
    try {
        auto* bookmarks = app_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        const auto& all = bookmarks->GetAll();

        json result;
        result["count"] = all.size();

        json bookmarks_array = json::array();
        for (size_t i = 0; i < all.size(); i++) {
            const auto& bm = all[i];
            json entry;
            entry["index"] = i;
            entry["address"] = FormatAddress(bm.address);
            entry["label"] = bm.label;
            entry["notes"] = bm.notes;
            entry["category"] = bm.category;
            entry["module"] = bm.module;
            entry["created_at"] = bm.created_at;
            bookmarks_array.push_back(entry);
        }
        result["bookmarks"] = bookmarks_array;

        // Include categories for convenience
        json categories = json::array();
        for (const auto& cat : bookmarks->GetCategories()) {
            categories.push_back(cat);
        }
        result["categories"] = categories;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleBookmarkAdd(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* bookmarks = app_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        // Parse address (required)
        if (!req.contains("address")) {
            return CreateErrorResponse("Missing required parameter: address");
        }
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);

        // Parse optional fields
        std::string label = req.value("label", "");
        std::string notes = req.value("notes", "");
        std::string category = req.value("category", "");
        std::string module = req.value("module", "");

        // Check if already bookmarked
        if (bookmarks->IsBookmarked(address)) {
            return CreateErrorResponse("Address already bookmarked: " + FormatAddress(address));
        }

        size_t index = bookmarks->Add(address, label, notes, category, module);

        json result;
        result["index"] = index;
        result["address"] = FormatAddress(address);
        result["label"] = label;
        result["total_bookmarks"] = bookmarks->Count();

        LOG_INFO("MCP: Added bookmark '{}' at {}", label, FormatAddress(address));

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleBookmarkRemove(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* bookmarks = app_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        bool removed = false;
        std::string removed_info;

        // Can remove by index or by address
        if (req.contains("index")) {
            size_t index = req["index"];
            if (index >= bookmarks->Count()) {
                return CreateErrorResponse("Invalid bookmark index: " + std::to_string(index));
            }
            const auto& bm = bookmarks->GetAll()[index];
            removed_info = FormatAddress(bm.address) + " (" + bm.label + ")";
            removed = bookmarks->Remove(index);
        } else if (req.contains("address")) {
            uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
            const auto* bm = bookmarks->FindByAddress(address);
            if (bm) {
                removed_info = FormatAddress(address) + " (" + bm->label + ")";
            }
            removed = bookmarks->RemoveByAddress(address);
        } else {
            return CreateErrorResponse("Missing parameter: provide 'index' or 'address'");
        }

        if (!removed) {
            return CreateErrorResponse("Bookmark not found");
        }

        json result;
        result["removed"] = removed_info;
        result["remaining"] = bookmarks->Count();

        LOG_INFO("MCP: Removed bookmark {}", removed_info);

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleBookmarkUpdate(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* bookmarks = app_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        // Require index to update
        if (!req.contains("index")) {
            return CreateErrorResponse("Missing required parameter: index");
        }

        size_t index = req["index"];
        if (index >= bookmarks->Count()) {
            return CreateErrorResponse("Invalid bookmark index: " + std::to_string(index));
        }

        // Get existing bookmark
        Bookmark bm = bookmarks->GetAll()[index];

        // Update fields if provided
        if (req.contains("address")) {
            bm.address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        }
        if (req.contains("label")) {
            bm.label = req["label"].get<std::string>();
        }
        if (req.contains("notes")) {
            bm.notes = req["notes"].get<std::string>();
        }
        if (req.contains("category")) {
            bm.category = req["category"].get<std::string>();
        }
        if (req.contains("module")) {
            bm.module = req["module"].get<std::string>();
        }

        if (!bookmarks->Update(index, bm)) {
            return CreateErrorResponse("Failed to update bookmark");
        }

        json result;
        result["index"] = index;
        result["address"] = FormatAddress(bm.address);
        result["label"] = bm.label;
        result["notes"] = bm.notes;
        result["category"] = bm.category;
        result["module"] = bm.module;

        LOG_INFO("MCP: Updated bookmark {} at {}", bm.label, FormatAddress(bm.address));

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

// ============================================================================
// CS2 Schema Cache System
// ============================================================================

std::string MCPServer::GetCS2SchemaCacheDirectory() {
    namespace fs = std::filesystem;
    fs::path cache_dir = fs::current_path() / "cache" / "cs2_schema";
    if (!fs::exists(cache_dir)) {
        fs::create_directories(cache_dir);
    }
    return cache_dir.string();
}

std::string MCPServer::GetCS2SchemaCacheFilePath(const std::string& scope_name, uint32_t module_size) {
    namespace fs = std::filesystem;
    // Sanitize scope name for filesystem
    std::string safe_name = scope_name;
    for (char& c : safe_name) {
        if (c == '!' || c == ':' || c == '\\' || c == '/' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    // Include module size in filename for ASLR/version resistance (like RTTI cache)
    std::stringstream ss;
    ss << safe_name << "_" << module_size << ".json";
    return (fs::path(GetCS2SchemaCacheDirectory()) / ss.str()).string();
}

bool MCPServer::SaveCS2SchemaCache(const std::string& scope_name, uint32_t module_size, const std::string& json_data) {
    std::string filepath = GetCS2SchemaCacheFilePath(scope_name, module_size);
    std::ofstream out(filepath);
    if (!out.is_open()) return false;
    out << json_data;
    out.close();
    LOG_INFO("CS2 schema cache saved: {}", filepath);
    return true;
}

std::string MCPServer::LoadCS2SchemaCache(const std::string& scope_name, uint32_t module_size) {
    std::string filepath = GetCS2SchemaCacheFilePath(scope_name, module_size);
    std::ifstream in(filepath);
    if (!in.is_open()) return "";
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool MCPServer::CS2SchemaCacheExists(const std::string& scope_name, uint32_t module_size) {
    return std::filesystem::exists(GetCS2SchemaCacheFilePath(scope_name, module_size));
}

uint32_t MCPServer::GetModuleSizeForScope(const std::string& scope_name) {
    // Scope names are typically like "client.dll", "server.dll", etc.
    // We need to get the module size from DMA
    if (!cs2_schema_ || cs2_schema_pid_ == 0) return 0;

    auto* dma = app_->GetDMA();
    if (!dma || !dma->IsConnected()) return 0;

    // GlobalTypeScope is not a real module - use schemasystem.dll size as proxy
    // This ensures cache invalidates when schemasystem.dll updates
    std::string module_name = scope_name;
    if (scope_name == "GlobalTypeScope") {
        module_name = "schemasystem.dll";
    }

    auto mod = dma->GetModuleByName(cs2_schema_pid_, module_name);
    if (mod) {
        return mod->size;
    }
    return 0;
}

// ============================================================================
// CS2 Schema Endpoints
// ============================================================================

std::string MCPServer::HandleCS2SchemaInit(const std::string& body) {
    try {
        json req = json::parse(body);

        uint32_t pid = req.value("pid", 0);
        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Find schemasystem.dll
        auto mod_opt = dma->GetModuleByName(pid, "schemasystem.dll");
        if (!mod_opt) {
            return CreateErrorResponse("schemasystem.dll not found - is this Counter-Strike 2?");
        }

        // Create or recreate dumper if PID changed
        if (cs2_schema_ && cs2_schema_pid_ != pid) {
            delete static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
            cs2_schema_ = nullptr;
        }

        if (!cs2_schema_) {
            cs2_schema_ = new dumper::CS2SchemaDumper(dma, pid);
            cs2_schema_pid_ = pid;
        }

        auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);

        if (!dumper->Initialize(mod_opt->base_address)) {
            return CreateErrorResponse("Failed to initialize CS2 Schema: " + dumper->GetLastError());
        }

        json result;
        result["schema_system"] = FormatAddress(dumper->GetSchemaSystemAddress());
        result["scope_count"] = dumper->GetScopes().size();

        json scopes = json::array();
        for (const auto& scope : dumper->GetScopes()) {
            json s;
            s["name"] = scope.name;
            s["address"] = FormatAddress(scope.address);
            s["class_count"] = scope.class_count;
            scopes.push_back(s);
        }
        result["scopes"] = scopes;

        LOG_INFO("CS2 Schema initialized: {} scopes", dumper->GetScopes().size());

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaDump(const std::string& body) {
    try {
        json req = json::parse(body);

        if (!cs2_schema_) {
            return CreateErrorResponse("CS2 Schema not initialized - call cs2_schema_init first");
        }

        auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
        if (!dumper->IsInitialized()) {
            return CreateErrorResponse("CS2 Schema not initialized");
        }

        std::string scope_name = req.value("scope", "");
        bool force_refresh = req.value("force_refresh", false);
        bool deduplicate = req.value("deduplicate", true);  // Default to deduplicate for comprehensive dump

        // Get module size for cache key (ASLR/version resistance)
        uint32_t module_size = 0;
        if (!scope_name.empty()) {
            module_size = GetModuleSizeForScope(scope_name);
        } else if (deduplicate) {
            // For deduplicated "all" dump, use schemasystem.dll size as version key
            module_size = GetModuleSizeForScope("schemasystem.dll");
            scope_name = "all_deduplicated";
        }

        // Check cache first (unless force_refresh)
        if (!force_refresh && !scope_name.empty() && module_size > 0 && CS2SchemaCacheExists(scope_name, module_size)) {
            std::string cached = LoadCS2SchemaCache(scope_name, module_size);
            if (!cached.empty()) {
                json cache_data = json::parse(cached);
                json result;
                result["scope"] = scope_name;
                result["module_size"] = module_size;
                result["cached"] = true;
                result["class_count"] = cache_data.contains("classes") ? cache_data["classes"].size() : 0;

                // Count fields
                size_t field_count = 0;
                if (cache_data.contains("classes")) {
                    for (const auto& cls : cache_data["classes"]) {
                        if (cls.contains("fields")) {
                            field_count += cls["fields"].size();
                        }
                    }
                }
                result["field_count"] = field_count;
                result["hint"] = "Use cs2_schema_cache_query to search classes by name";
                LOG_INFO("CS2 schema cache hit for {} (size={})", scope_name, module_size);
                return CreateSuccessResponse(result.dump());
            }
        }

        json result;
        std::vector<dumper::SchemaClass> classes;
        size_t total_fields = 0;
        std::string original_scope = req.value("scope", "");

        if (!original_scope.empty()) {
            // Dump specific scope
            uint64_t scope_addr = 0;
            for (const auto& scope : dumper->GetScopes()) {
                if (scope.name == original_scope) {
                    scope_addr = scope.address;
                    break;
                }
            }

            if (scope_addr == 0) {
                return CreateErrorResponse("Scope not found: " + original_scope);
            }

            classes = dumper->DumpScope(scope_addr);
            result["scope"] = original_scope;
            result["module_size"] = module_size;
        } else if (deduplicate) {
            // Dump all scopes with deduplication (like Andromeda)
            classes = dumper->DumpAllDeduplicated();
            result["scope"] = "all_deduplicated";
            result["module_size"] = module_size;
            result["deduplicated"] = true;
            result["scopes_processed"] = dumper->GetScopes().size();
        } else {
            // Dump all scopes without deduplication
            auto all_schemas = dumper->DumpAll();
            for (const auto& [name, scope_classes] : all_schemas) {
                for (const auto& cls : scope_classes) {
                    classes.push_back(cls);
                }
            }
            result["scope"] = "all";
            result["deduplicated"] = false;
        }

        // Count fields
        for (const auto& cls : classes) {
            total_fields += cls.fields.size();
        }

        // Build cache data
        json cache_data;
        cache_data["classes"] = json::array();

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        cache_data["cached_at"] = ss.str();
        cache_data["scope"] = scope_name.empty() ? "all" : scope_name;
        cache_data["module_size"] = module_size;

        for (const auto& cls : classes) {
            json c;
            c["name"] = cls.name;
            c["module"] = cls.module;
            c["size"] = cls.size;
            c["base_class"] = cls.base_class;

            json fields = json::array();
            for (const auto& field : cls.fields) {
                json f;
                f["name"] = field.name;
                f["type"] = field.type_name;
                f["offset"] = field.offset;
                f["size"] = field.size;
                fields.push_back(f);
            }
            c["fields"] = fields;
            cache_data["classes"].push_back(c);
        }

        // Save to cache (only if we have a valid module size)
        std::string cache_key = scope_name.empty() ? "all" : scope_name;
        if (module_size > 0) {
            SaveCS2SchemaCache(cache_key, module_size, cache_data.dump(2));
            result["cache_file"] = GetCS2SchemaCacheFilePath(cache_key, module_size);
        }

        result["cached"] = false;
        result["class_count"] = classes.size();
        result["field_count"] = total_fields;
        result["hint"] = "Use cs2_schema_cache_query to search classes by name";

        LOG_INFO("CS2 schema dumped: {} classes, {} fields", classes.size(), total_fields);

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaGetOffset(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string class_name = req.value("class_name", "");
        std::string field_name = req.value("field_name", "");

        if (class_name.empty() || field_name.empty()) {
            return CreateErrorResponse("Missing required parameters: class_name, field_name");
        }

        // Try live dumper first
        if (cs2_schema_) {
            auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
            if (dumper->IsInitialized()) {
                uint32_t offset = dumper->GetOffset(class_name, field_name);
                if (offset > 0) {
                    json result;
                    result["class"] = class_name;
                    result["field"] = field_name;
                    result["offset"] = offset;
                    std::stringstream ss;
                    ss << "0x" << std::hex << std::uppercase << offset;
                    result["offset_hex"] = ss.str();
                    return CreateSuccessResponse(result.dump());
                }
            }
        }

        // Search cache
        namespace fs = std::filesystem;
        std::string cache_dir = GetCS2SchemaCacheDirectory();

        std::string class_lower = class_name;
        std::string field_lower = field_name;
        std::transform(class_lower.begin(), class_lower.end(), class_lower.begin(), ::tolower);
        std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            for (const auto& cls : cache_data["classes"]) {
                std::string cls_name = cls.value("name", "");
                std::string cls_name_lower = cls_name;
                std::transform(cls_name_lower.begin(), cls_name_lower.end(), cls_name_lower.begin(), ::tolower);

                if (cls_name_lower != class_lower) continue;

                if (cls.contains("fields")) {
                    for (const auto& field : cls["fields"]) {
                        std::string fld_name = field.value("name", "");
                        std::string fld_name_lower = fld_name;
                        std::transform(fld_name_lower.begin(), fld_name_lower.end(), fld_name_lower.begin(), ::tolower);

                        if (fld_name_lower == field_lower) {
                            json result;
                            result["class"] = cls_name;
                            result["field"] = fld_name;
                            result["offset"] = field.value("offset", 0);
                            result["type"] = field.value("type", "");
                            result["size"] = field.value("size", 0);
                            std::stringstream ss;
                            ss << "0x" << std::hex << std::uppercase << field.value("offset", 0);
                            result["offset_hex"] = ss.str();
                            result["from_cache"] = true;
                            return CreateSuccessResponse(result.dump());
                        }
                    }
                }
            }
        }

        return CreateErrorResponse("Offset not found for " + class_name + "::" + field_name);

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaFindClass(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string class_name = req.value("class_name", "");
        if (class_name.empty()) {
            return CreateErrorResponse("Missing required parameter: class_name");
        }

        // Try live dumper first
        if (cs2_schema_) {
            auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
            if (dumper->IsInitialized()) {
                const dumper::SchemaClass* cls = dumper->FindClass(class_name);
                if (cls) {
                    json result;
                    result["name"] = cls->name;
                    result["module"] = cls->module;
                    result["size"] = cls->size;
                    result["base_class"] = cls->base_class;

                    json fields = json::array();
                    for (const auto& field : cls->fields) {
                        json f;
                        f["name"] = field.name;
                        f["type"] = field.type_name;
                        f["offset"] = field.offset;
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << field.offset;
                        f["offset_hex"] = ss.str();
                        f["size"] = field.size;
                        fields.push_back(f);
                    }
                    result["fields"] = fields;
                    result["field_count"] = cls->fields.size();
                    return CreateSuccessResponse(result.dump());
                }
            }
        }

        // Search cache
        namespace fs = std::filesystem;
        std::string cache_dir = GetCS2SchemaCacheDirectory();

        std::string class_lower = class_name;
        std::transform(class_lower.begin(), class_lower.end(), class_lower.begin(), ::tolower);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            for (const auto& cls : cache_data["classes"]) {
                std::string cls_name = cls.value("name", "");
                std::string cls_name_lower = cls_name;
                std::transform(cls_name_lower.begin(), cls_name_lower.end(), cls_name_lower.begin(), ::tolower);

                if (cls_name_lower == class_lower) {
                    json result;
                    result["name"] = cls_name;
                    result["module"] = cls.value("module", "");
                    result["size"] = cls.value("size", 0);
                    result["base_class"] = cls.value("base_class", "");

                    if (cls.contains("fields")) {
                        json fields = json::array();
                        for (const auto& field : cls["fields"]) {
                            json f;
                            f["name"] = field.value("name", "");
                            f["type"] = field.value("type", "");
                            f["offset"] = field.value("offset", 0);
                            std::stringstream ss;
                            ss << "0x" << std::hex << std::uppercase << field.value("offset", 0);
                            f["offset_hex"] = ss.str();
                            f["size"] = field.value("size", 0);
                            fields.push_back(f);
                        }
                        result["fields"] = fields;
                        result["field_count"] = fields.size();
                    }
                    result["from_cache"] = true;
                    return CreateSuccessResponse(result.dump());
                }
            }
        }

        return CreateErrorResponse("Class not found: " + class_name);

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheList(const std::string& body) {
    try {
        namespace fs = std::filesystem;
        std::string cache_dir = GetCS2SchemaCacheDirectory();

        json result;
        json scopes = json::array();

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();

                // Parse scope name and module size from filename (format: name_size.json)
                std::string scope_name = filename;
                uint32_t module_size = 0;
                size_t last_underscore = filename.rfind('_');
                if (last_underscore != std::string::npos) {
                    scope_name = filename.substr(0, last_underscore);
                    std::string size_str = filename.substr(last_underscore + 1);
                    try {
                        module_size = std::stoul(size_str);
                    } catch (...) {
                        // Old format without size - keep the full filename as scope name
                        scope_name = filename;
                        module_size = 0;
                    }
                }

                std::ifstream in(entry.path());
                if (in.is_open()) {
                    json cache_data = json::parse(in);
                    json scope;
                    scope["scope"] = scope_name;
                    scope["module_size"] = module_size;
                    scope["classes"] = cache_data.contains("classes") ? cache_data["classes"].size() : 0;
                    scope["cached_at"] = cache_data.value("cached_at", "unknown");
                    scope["cache_file"] = entry.path().string();

                    // Count total fields
                    size_t field_count = 0;
                    if (cache_data.contains("classes")) {
                        for (const auto& cls : cache_data["classes"]) {
                            if (cls.contains("fields")) {
                                field_count += cls["fields"].size();
                            }
                        }
                    }
                    scope["fields"] = field_count;
                    scopes.push_back(scope);
                }
            }
        }

        result["count"] = scopes.size();
        result["scopes"] = scopes;
        result["cache_directory"] = cache_dir;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheQuery(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string query = req.value("query", "");
        std::string scope_filter = req.value("scope", "");
        int max_results = req.value("max_results", 100);

        if (query.empty()) {
            return CreateErrorResponse("Missing required parameter: query");
        }

        std::string query_lower = query;
        std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

        namespace fs = std::filesystem;
        std::string cache_dir = GetCS2SchemaCacheDirectory();

        json result;
        json matches = json::array();
        int total_searched = 0;

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().stem().string();

            // Parse scope name from filename (format: name_size.json)
            std::string scope_name = filename;
            size_t last_underscore = filename.rfind('_');
            if (last_underscore != std::string::npos) {
                scope_name = filename.substr(0, last_underscore);
            }

            // Filter by scope if specified
            if (!scope_filter.empty()) {
                std::string filter_lower = scope_filter;
                std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);
                std::string scope_lower = scope_name;
                std::transform(scope_lower.begin(), scope_lower.end(), scope_lower.begin(), ::tolower);
                if (scope_lower.find(filter_lower) == std::string::npos) continue;
            }

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            for (const auto& cls : cache_data["classes"]) {
                total_searched++;

                std::string cls_name = cls.value("name", "");
                std::string cls_name_lower = cls_name;
                std::transform(cls_name_lower.begin(), cls_name_lower.end(), cls_name_lower.begin(), ::tolower);

                if (cls_name_lower.find(query_lower) != std::string::npos) {
                    json match;
                    match["name"] = cls_name;
                    match["module"] = cls.value("module", "");
                    match["size"] = cls.value("size", 0);
                    match["base_class"] = cls.value("base_class", "");
                    match["scope"] = scope_name;
                    match["field_count"] = cls.contains("fields") ? cls["fields"].size() : 0;
                    matches.push_back(match);

                    if ((int)matches.size() >= max_results) break;
                }
            }

            if ((int)matches.size() >= max_results) break;
        }

        result["query"] = query;
        result["matches"] = matches;
        result["match_count"] = matches.size();
        result["total_searched"] = total_searched;
        result["truncated"] = (int)matches.size() >= max_results;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheGet(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string scope = req.value("scope", "");
        int max_results = req.value("max_results", 1000);

        if (scope.empty()) {
            return CreateErrorResponse("Missing required parameter: scope");
        }

        // Find cache file for this scope (filename format: scope_size.json)
        namespace fs = std::filesystem;
        std::string cache_dir = GetCS2SchemaCacheDirectory();
        std::string cache_file;

        std::string scope_lower = scope;
        std::transform(scope_lower.begin(), scope_lower.end(), scope_lower.begin(), ::tolower);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().stem().string();

            // Parse scope name from filename (format: name_size.json)
            size_t last_underscore = filename.rfind('_');
            if (last_underscore != std::string::npos) {
                std::string file_scope = filename.substr(0, last_underscore);
                std::string file_scope_lower = file_scope;
                std::transform(file_scope_lower.begin(), file_scope_lower.end(), file_scope_lower.begin(), ::tolower);

                if (file_scope_lower == scope_lower) {
                    cache_file = entry.path().string();
                    break;
                }
            }
        }

        if (cache_file.empty()) {
            return CreateErrorResponse("Cache not found for scope: " + scope);
        }

        std::ifstream in(cache_file);
        if (!in.is_open()) {
            return CreateErrorResponse("Failed to open cache file for scope: " + scope);
        }

        std::stringstream ss;
        ss << in.rdbuf();
        std::string cached = ss.str();

        json cache_data = json::parse(cached);
        json result;
        result["scope"] = scope;
        result["module_size"] = cache_data.value("module_size", 0);
        result["cached_at"] = cache_data.value("cached_at", "unknown");
        result["cache_file"] = cache_file;

        if (cache_data.contains("classes")) {
            json classes = json::array();
            int count = 0;
            for (const auto& cls : cache_data["classes"]) {
                classes.push_back(cls);
                count++;
                if (count >= max_results) break;
            }
            result["classes"] = classes;
            result["class_count"] = classes.size();
            result["total_classes"] = cache_data["classes"].size();
            result["truncated"] = count >= max_results && cache_data["classes"].size() > (size_t)max_results;
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheClear(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string scope = req.value("scope", "");
        namespace fs = std::filesystem;
        std::string cache_dir = GetCS2SchemaCacheDirectory();

        int deleted = 0;

        if (scope.empty()) {
            // Clear all
            for (const auto& entry : fs::directory_iterator(cache_dir)) {
                if (entry.path().extension() == ".json") {
                    fs::remove(entry.path());
                    deleted++;
                }
            }
            LOG_INFO("CS2 schema cache cleared: {} files deleted", deleted);
        } else {
            // Clear specific scope - find all files matching scope prefix
            std::string scope_lower = scope;
            std::transform(scope_lower.begin(), scope_lower.end(), scope_lower.begin(), ::tolower);

            for (const auto& entry : fs::directory_iterator(cache_dir)) {
                if (entry.path().extension() != ".json") continue;

                std::string filename = entry.path().stem().string();

                // Parse scope name from filename (format: name_size.json)
                size_t last_underscore = filename.rfind('_');
                if (last_underscore != std::string::npos) {
                    std::string file_scope = filename.substr(0, last_underscore);
                    std::string file_scope_lower = file_scope;
                    std::transform(file_scope_lower.begin(), file_scope_lower.end(), file_scope_lower.begin(), ::tolower);

                    if (file_scope_lower == scope_lower) {
                        fs::remove(entry.path());
                        deleted++;
                        LOG_INFO("CS2 schema cache cleared: {}", entry.path().string());
                    }
                }
            }

            if (deleted > 0) {
                LOG_INFO("CS2 schema cache cleared for scope {}: {} files deleted", scope, deleted);
            }
        }

        json result;
        result["deleted"] = deleted;
        result["scope"] = scope.empty() ? "all" : scope;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

// ============================================================================
// CS2 Entity Tools (RTTI + Schema Bridge)
// ============================================================================

std::string MCPServer::StripTypePrefix(const std::string& name) {
    if (name.substr(0, 6) == "class ") return name.substr(6);
    if (name.substr(0, 7) == "struct ") return name.substr(7);
    return name;
}

std::string MCPServer::IdentifyClassFromPointer(uint32_t pid, uint64_t ptr, uint64_t module_base) {
    auto* dma = app_->GetDMA();
    if (!dma || !dma->IsConnected()) return "";

    // Read vtable pointer at object+0
    auto vtable_data = dma->ReadMemory(pid, ptr, 8);
    if (vtable_data.size() < 8) return "";

    uint64_t vtable_addr;
    std::memcpy(&vtable_addr, vtable_data.data(), 8);
    if (vtable_addr == 0 || vtable_addr < 0x10000) return "";

    // Find module base if not provided
    if (module_base == 0) {
        auto modules = dma->GetModuleList(pid);
        for (const auto& mod : modules) {
            if (vtable_addr >= mod.base_address && vtable_addr < mod.base_address + mod.size) {
                module_base = mod.base_address;
                break;
            }
        }
        if (module_base == 0) return "";
    }

    // Use RTTI parser
    analysis::RTTIParser parser(
        [dma, pid](uint64_t addr, size_t size) {
            return dma->ReadMemory(pid, addr, size);
        },
        module_base
    );

    auto info = parser.ParseVTable(vtable_addr);
    if (!info) return "";

    // Return class name with prefix stripped
    return StripTypePrefix(info->demangled_name);
}

std::string MCPServer::HandleCS2EntityInit(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Find client.dll
        auto client_mod = dma->GetModuleByName(pid, "client.dll");
        if (!client_mod) {
            return CreateErrorResponse("client.dll not found - is this Counter-Strike 2?");
        }

        cs2_entity_cache_.client_base = client_mod->base_address;
        cs2_entity_cache_.client_size = client_mod->size;

        // Pattern: CGameEntitySystem
        // 48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B F0
        const uint8_t entity_system_pattern[] = {
            0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD3,
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF0
        };
        const char entity_system_mask[] = "xxx????xx????xxx";

        // Pattern: LocalPlayerController array
        // 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1
        const uint8_t local_player_pattern[] = {
            0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x04, 0xC1
        };
        const char local_player_mask[] = "xxx????xxxx";

        // Scan for patterns
        auto client_data = dma->ReadMemory(pid, client_mod->base_address, client_mod->size);
        if (client_data.empty()) {
            return CreateErrorResponse("Failed to read client.dll memory");
        }

        uint64_t entity_system_match = 0;
        uint64_t local_player_match = 0;

        // Search for entity system pattern
        for (size_t i = 0; i + sizeof(entity_system_pattern) < client_data.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < sizeof(entity_system_pattern); j++) {
                if (entity_system_mask[j] == 'x' && client_data[i + j] != entity_system_pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                entity_system_match = client_mod->base_address + i;
                break;
            }
        }

        // Search for local player pattern
        for (size_t i = 0; i + sizeof(local_player_pattern) < client_data.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < sizeof(local_player_pattern); j++) {
                if (local_player_mask[j] == 'x' && client_data[i + j] != local_player_pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                local_player_match = client_mod->base_address + i;
                break;
            }
        }

        json result;

        // Resolve entity system pointer
        if (entity_system_match != 0) {
            // Read RIP-relative offset at pattern+3
            int32_t offset;
            std::memcpy(&offset, client_data.data() + (entity_system_match - client_mod->base_address) + 3, 4);
            uint64_t ptr_addr = entity_system_match + 7 + offset;

            // Read the actual pointer
            auto ptr_data = dma->ReadMemory(pid, ptr_addr, 8);
            if (ptr_data.size() == 8) {
                std::memcpy(&cs2_entity_cache_.entity_system, ptr_data.data(), 8);
            }

            result["entity_system_pattern"] = FormatAddress(entity_system_match);
            result["entity_system_ptr_addr"] = FormatAddress(ptr_addr);
            result["entity_system"] = FormatAddress(cs2_entity_cache_.entity_system);
        }

        // Resolve local player controller array
        if (local_player_match != 0) {
            // Read RIP-relative offset at pattern+3
            int32_t offset;
            std::memcpy(&offset, client_data.data() + (local_player_match - client_mod->base_address) + 3, 4);
            cs2_entity_cache_.local_player_controller = local_player_match + 7 + offset;

            result["local_player_pattern"] = FormatAddress(local_player_match);
            result["local_player_controller_array"] = FormatAddress(cs2_entity_cache_.local_player_controller);
        }

        if (cs2_entity_cache_.entity_system != 0 && cs2_entity_cache_.local_player_controller != 0) {
            cs2_entity_cache_.initialized = true;
            result["status"] = "initialized";
        } else {
            result["status"] = "partial";
            if (cs2_entity_cache_.entity_system == 0) {
                result["warning"] = "CGameEntitySystem pattern not found";
            }
            if (cs2_entity_cache_.local_player_controller == 0) {
                result["warning"] = "LocalPlayerController pattern not found";
            }
        }

        result["client_base"] = FormatAddress(cs2_entity_cache_.client_base);
        result["client_size"] = cs2_entity_cache_.client_size;

        LOG_INFO("CS2 Entity system initialized: EntitySystem={}, LocalPlayer={}",
                 FormatAddress(cs2_entity_cache_.entity_system),
                 FormatAddress(cs2_entity_cache_.local_player_controller));

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2Identify(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        std::string address_str = req.value("address", "");

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }
        if (address_str.empty()) {
            return CreateErrorResponse("Missing required parameter: address");
        }

        uint64_t address = std::stoull(address_str, nullptr, 16);
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Identify class via RTTI
        std::string class_name = IdentifyClassFromPointer(pid, address, cs2_entity_cache_.client_base);
        if (class_name.empty()) {
            return CreateErrorResponse("Could not identify class - no valid RTTI found at address");
        }

        json result;
        result["address"] = FormatAddress(address);
        result["class_name"] = class_name;

        // Try to find matching schema class
        if (cs2_schema_) {
            auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
            if (dumper->IsInitialized()) {
                const dumper::SchemaClass* schema_class = dumper->FindClass(class_name);
                if (schema_class) {
                    result["schema_found"] = true;
                    result["schema_class"] = schema_class->name;
                    result["schema_size"] = schema_class->size;
                    result["field_count"] = schema_class->fields.size();
                    result["base_class"] = schema_class->base_class;
                } else {
                    result["schema_found"] = false;
                }
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2ReadField(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        std::string address_str = req.value("address", "");
        std::string field_name = req.value("field", "");
        std::string class_name = req.value("class", "");  // Optional - auto-detect if not provided

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }
        if (address_str.empty()) {
            return CreateErrorResponse("Missing required parameter: address");
        }
        if (field_name.empty()) {
            return CreateErrorResponse("Missing required parameter: field");
        }

        uint64_t address = std::stoull(address_str, nullptr, 16);
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Auto-detect class if not provided
        if (class_name.empty()) {
            class_name = IdentifyClassFromPointer(pid, address, cs2_entity_cache_.client_base);
            if (class_name.empty()) {
                return CreateErrorResponse("Could not auto-detect class - please provide 'class' parameter");
            }
        }

        // Find schema class
        if (!cs2_schema_) {
            return CreateErrorResponse("CS2 Schema not initialized - call cs2_schema_init first");
        }

        auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
        if (!dumper->IsInitialized()) {
            return CreateErrorResponse("CS2 Schema not initialized");
        }

        const dumper::SchemaClass* schema_class = dumper->FindClass(class_name);
        if (!schema_class) {
            return CreateErrorResponse("Schema class not found: " + class_name);
        }

        // Find field
        const dumper::SchemaField* target_field = nullptr;
        for (const auto& field : schema_class->fields) {
            if (field.name == field_name) {
                target_field = &field;
                break;
            }
        }

        if (!target_field) {
            return CreateErrorResponse("Field not found: " + field_name + " in class " + class_name);
        }

        // Read field value
        uint64_t field_addr = address + target_field->offset;
        size_t read_size = target_field->size > 0 ? target_field->size : 8;  // Default to 8 bytes

        // Determine read size from type if size is 0
        if (read_size == 0 || read_size > 64) {
            std::string type_lower = target_field->type_name;
            std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

            // Check for char[N] array (string) - extract N from type like "char[128]"
            std::regex char_array_regex(R"(char\[(\d+)\])");
            std::smatch match;
            if (std::regex_search(target_field->type_name, match, char_array_regex)) {
                read_size = std::stoi(match[1].str());
            } else if (type_lower.find("int8") != std::string::npos || type_lower.find("bool") != std::string::npos ||
                type_lower.find("uint8") != std::string::npos || type_lower.find("char") != std::string::npos) {
                read_size = 1;
            } else if (type_lower.find("int16") != std::string::npos || type_lower.find("uint16") != std::string::npos ||
                       type_lower.find("short") != std::string::npos) {
                read_size = 2;
            } else if (type_lower.find("int32") != std::string::npos || type_lower.find("uint32") != std::string::npos ||
                       type_lower.find("int") != std::string::npos || type_lower.find("float32") != std::string::npos ||
                       type_lower.find("float") != std::string::npos || type_lower.find("chandle") != std::string::npos) {
                read_size = 4;
            } else if (type_lower.find("int64") != std::string::npos || type_lower.find("uint64") != std::string::npos ||
                       type_lower.find("*") != std::string::npos || type_lower.find("float64") != std::string::npos ||
                       type_lower.find("double") != std::string::npos) {
                read_size = 8;
            } else if (type_lower.find("vector") != std::string::npos || type_lower.find("qangle") != std::string::npos) {
                read_size = 12;  // 3 floats
            } else {
                read_size = 8;  // Default to pointer size
            }
        }

        auto data = dma->ReadMemory(pid, field_addr, read_size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read memory at field address");
        }

        json result;
        result["address"] = FormatAddress(address);
        result["class"] = class_name;
        result["field"] = field_name;
        result["type"] = target_field->type_name;
        result["offset"] = target_field->offset;
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << target_field->offset;
        result["offset_hex"] = ss.str();
        result["field_address"] = FormatAddress(field_addr);

        // Interpret value based on type
        std::string type_lower = target_field->type_name;
        std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

        // Check for char[N] array - convert to string
        std::regex char_array_regex(R"(char\[(\d+)\])");
        std::smatch match;
        if (std::regex_search(target_field->type_name, match, char_array_regex)) {
            // Null-terminated string
            std::string str_value(reinterpret_cast<const char*>(data.data()));
            // Ensure we don't read past buffer
            size_t max_len = std::stoi(match[1].str());
            if (str_value.length() > max_len) {
                str_value = str_value.substr(0, max_len);
            }
            result["value"] = str_value;
        } else if (type_lower.find("bool") != std::string::npos && data.size() >= 1) {
            result["value"] = data[0] != 0;
        } else if ((type_lower.find("int8") != std::string::npos || type_lower.find("char") != std::string::npos) && data.size() >= 1) {
            result["value"] = static_cast<int8_t>(data[0]);
        } else if (type_lower.find("uint8") != std::string::npos && data.size() >= 1) {
            result["value"] = data[0];
        } else if (type_lower.find("int16") != std::string::npos && data.size() >= 2) {
            int16_t v; std::memcpy(&v, data.data(), 2);
            result["value"] = v;
        } else if (type_lower.find("uint16") != std::string::npos && data.size() >= 2) {
            uint16_t v; std::memcpy(&v, data.data(), 2);
            result["value"] = v;
        } else if ((type_lower.find("int32") != std::string::npos || type_lower == "int") && data.size() >= 4) {
            int32_t v; std::memcpy(&v, data.data(), 4);
            result["value"] = v;
        } else if ((type_lower.find("uint32") != std::string::npos || type_lower.find("chandle") != std::string::npos) && data.size() >= 4) {
            uint32_t v; std::memcpy(&v, data.data(), 4);
            result["value"] = v;
            if (type_lower.find("chandle") != std::string::npos) {
                result["entity_index"] = v & 0x7FFF;
            }
        } else if (type_lower.find("float32") != std::string::npos || type_lower == "float") {
            if (data.size() >= 4) {
                float v; std::memcpy(&v, data.data(), 4);
                result["value"] = v;
            }
        } else if (type_lower.find("int64") != std::string::npos && data.size() >= 8) {
            int64_t v; std::memcpy(&v, data.data(), 8);
            result["value"] = v;
        } else if ((type_lower.find("uint64") != std::string::npos || type_lower.find("*") != std::string::npos) && data.size() >= 8) {
            uint64_t v; std::memcpy(&v, data.data(), 8);
            result["value"] = FormatAddress(v);
        } else if (type_lower.find("vector") != std::string::npos && data.size() >= 12) {
            float x, y, z;
            std::memcpy(&x, data.data(), 4);
            std::memcpy(&y, data.data() + 4, 4);
            std::memcpy(&z, data.data() + 8, 4);
            result["value"] = {{"x", x}, {"y", y}, {"z", z}};
        } else if (type_lower.find("qangle") != std::string::npos && data.size() >= 12) {
            float pitch, yaw, roll;
            std::memcpy(&pitch, data.data(), 4);
            std::memcpy(&yaw, data.data() + 4, 4);
            std::memcpy(&roll, data.data() + 8, 4);
            result["value"] = {{"pitch", pitch}, {"yaw", yaw}, {"roll", roll}};
        } else {
            // Return raw hex
            std::stringstream hex_ss;
            for (size_t i = 0; i < data.size(); i++) {
                hex_ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
            }
            result["value_hex"] = hex_ss.str();
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2Inspect(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        std::string address_str = req.value("address", "");
        std::string class_name = req.value("class", "");
        int max_fields = req.value("max_fields", 50);

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }
        if (address_str.empty()) {
            return CreateErrorResponse("Missing required parameter: address");
        }

        uint64_t address = std::stoull(address_str, nullptr, 16);
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Auto-detect class if not provided
        if (class_name.empty()) {
            class_name = IdentifyClassFromPointer(pid, address, cs2_entity_cache_.client_base);
            if (class_name.empty()) {
                return CreateErrorResponse("Could not auto-detect class - please provide 'class' parameter");
            }
        }

        // Find schema class
        if (!cs2_schema_) {
            return CreateErrorResponse("CS2 Schema not initialized - call cs2_schema_init first");
        }

        auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
        if (!dumper->IsInitialized()) {
            return CreateErrorResponse("CS2 Schema not initialized");
        }

        const dumper::SchemaClass* schema_class = dumper->FindClass(class_name);
        if (!schema_class) {
            return CreateErrorResponse("Schema class not found: " + class_name);
        }

        json result;
        result["address"] = FormatAddress(address);
        result["class"] = class_name;
        result["base_class"] = schema_class->base_class;
        result["size"] = schema_class->size;

        json fields = json::array();
        int field_count = 0;

        for (const auto& field : schema_class->fields) {
            if (field_count >= max_fields) break;

            json f;
            f["name"] = field.name;
            f["type"] = field.type_name;
            f["offset"] = field.offset;
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << field.offset;
            f["offset_hex"] = ss.str();

            // Try to read value
            uint64_t field_addr = address + field.offset;

            // Determine size
            size_t read_size = 8;
            std::string type_lower = field.type_name;
            std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

            if (type_lower.find("bool") != std::string::npos || type_lower.find("int8") != std::string::npos ||
                type_lower.find("uint8") != std::string::npos) {
                read_size = 1;
            } else if (type_lower.find("int16") != std::string::npos || type_lower.find("uint16") != std::string::npos) {
                read_size = 2;
            } else if (type_lower.find("int32") != std::string::npos || type_lower.find("uint32") != std::string::npos ||
                       type_lower.find("float") != std::string::npos || type_lower.find("chandle") != std::string::npos) {
                read_size = 4;
            } else if (type_lower.find("vector") != std::string::npos || type_lower.find("qangle") != std::string::npos) {
                read_size = 12;
            }

            auto data = dma->ReadMemory(pid, field_addr, read_size);
            if (!data.empty()) {
                if (type_lower.find("bool") != std::string::npos && data.size() >= 1) {
                    f["value"] = data[0] != 0;
                } else if ((type_lower.find("int32") != std::string::npos || type_lower == "int") && data.size() >= 4) {
                    int32_t v; std::memcpy(&v, data.data(), 4);
                    f["value"] = v;
                } else if ((type_lower.find("uint32") != std::string::npos || type_lower.find("chandle") != std::string::npos) && data.size() >= 4) {
                    uint32_t v; std::memcpy(&v, data.data(), 4);
                    f["value"] = v;
                } else if ((type_lower.find("float") != std::string::npos) && data.size() >= 4) {
                    float v; std::memcpy(&v, data.data(), 4);
                    f["value"] = v;
                } else if (type_lower.find("*") != std::string::npos && data.size() >= 8) {
                    uint64_t v; std::memcpy(&v, data.data(), 8);
                    f["value"] = FormatAddress(v);
                } else if (type_lower.find("vector") != std::string::npos && data.size() >= 12) {
                    float x, y, z;
                    std::memcpy(&x, data.data(), 4);
                    std::memcpy(&y, data.data() + 4, 4);
                    std::memcpy(&z, data.data() + 8, 4);
                    f["value"] = {{"x", x}, {"y", y}, {"z", z}};
                }
            }

            fields.push_back(f);
            field_count++;
        }

        result["fields"] = fields;
        result["field_count"] = schema_class->fields.size();
        result["fields_shown"] = field_count;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2GetLocalPlayer(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        int slot = req.value("slot", 0);

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }

        if (!cs2_entity_cache_.initialized) {
            return CreateErrorResponse("CS2 Entity system not initialized - call cs2_entity_init first");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Read local player controller from array
        uint64_t controller_ptr_addr = cs2_entity_cache_.local_player_controller + slot * 8;
        auto ptr_data = dma->ReadMemory(pid, controller_ptr_addr, 8);
        if (ptr_data.size() < 8) {
            return CreateErrorResponse("Failed to read local player controller pointer");
        }

        uint64_t controller;
        std::memcpy(&controller, ptr_data.data(), 8);

        if (controller == 0) {
            json result;
            result["slot"] = slot;
            result["controller"] = nullptr;
            result["message"] = "No local player at this slot";
            return CreateSuccessResponse(result.dump());
        }

        json result;
        result["slot"] = slot;
        result["controller"] = FormatAddress(controller);

        // Try to identify the controller class
        std::string class_name = IdentifyClassFromPointer(pid, controller, cs2_entity_cache_.client_base);
        if (!class_name.empty()) {
            result["controller_class"] = class_name;
        }

        // Try to read pawn handle from controller (offset 0x8FC for CCSPlayerController.m_hPlayerPawn)
        if (cs2_schema_) {
            auto* dumper = static_cast<dumper::CS2SchemaDumper*>(cs2_schema_);
            uint32_t pawn_offset = dumper->GetOffset("CCSPlayerController", "m_hPlayerPawn");
            if (pawn_offset != 0) {
                auto pawn_handle_data = dma->ReadMemory(pid, controller + pawn_offset, 4);
                if (pawn_handle_data.size() >= 4) {
                    uint32_t pawn_handle;
                    std::memcpy(&pawn_handle, pawn_handle_data.data(), 4);
                    result["pawn_handle"] = pawn_handle;
                    result["pawn_entity_index"] = pawn_handle & 0x7FFF;
                }
            }

            // Read health from controller
            uint32_t health_offset = dumper->GetOffset("CCSPlayerController", "m_iPawnHealth");
            if (health_offset != 0) {
                auto health_data = dma->ReadMemory(pid, controller + health_offset, 4);
                if (health_data.size() >= 4) {
                    uint32_t health;
                    std::memcpy(&health, health_data.data(), 4);
                    result["health"] = health;
                }
            }

            // Read armor from controller
            uint32_t armor_offset = dumper->GetOffset("CCSPlayerController", "m_iPawnArmor");
            if (armor_offset != 0) {
                auto armor_data = dma->ReadMemory(pid, controller + armor_offset, 4);
                if (armor_data.size() >= 4) {
                    int32_t armor;
                    std::memcpy(&armor, armor_data.data(), 4);
                    result["armor"] = armor;
                }
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2GetEntity(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        uint32_t handle = req.value("handle", 0);
        int index = req.value("index", -1);

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }
        if (handle == 0 && index < 0) {
            return CreateErrorResponse("Missing required parameter: handle or index");
        }

        if (!cs2_entity_cache_.initialized || cs2_entity_cache_.entity_system == 0) {
            return CreateErrorResponse("CS2 Entity system not initialized - call cs2_entity_init first");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Calculate entity index from handle if needed
        int entity_index = index >= 0 ? index : (handle & 0x7FFF);

        // Entity list is at EntitySystem + 0x10, organized in chunks of 512
        // Each chunk pointer is at EntitySystem + 0x10 + chunk_index * 8
        // Within chunk: 8-byte header, then entries at stride 0x70

        int chunk_index = entity_index / 512;
        int slot = entity_index % 512;

        // Read chunk pointer
        uint64_t chunk_ptr_addr = cs2_entity_cache_.entity_system + 0x10 + chunk_index * 8;
        auto chunk_data = dma->ReadMemory(pid, chunk_ptr_addr, 8);
        if (chunk_data.size() < 8) {
            return CreateErrorResponse("Failed to read entity chunk pointer");
        }

        uint64_t chunk_base;
        std::memcpy(&chunk_base, chunk_data.data(), 8);

        // Some entity systems have flags in low bits
        chunk_base &= ~0xFULL;

        if (chunk_base == 0) {
            json result;
            result["entity_index"] = entity_index;
            result["entity"] = nullptr;
            result["message"] = "Entity chunk not allocated";
            return CreateSuccessResponse(result.dump());
        }

        // Read entity pointer from chunk (entries start at +8, stride 0x70)
        uint64_t entity_entry_addr = chunk_base + 0x08 + slot * 0x70;
        auto entity_data = dma->ReadMemory(pid, entity_entry_addr, 8);
        if (entity_data.size() < 8) {
            return CreateErrorResponse("Failed to read entity entry");
        }

        uint64_t entity;
        std::memcpy(&entity, entity_data.data(), 8);

        if (entity == 0) {
            json result;
            result["entity_index"] = entity_index;
            result["entity"] = nullptr;
            result["message"] = "Entity slot is empty";
            return CreateSuccessResponse(result.dump());
        }

        json result;
        result["entity_index"] = entity_index;
        result["chunk"] = chunk_index;
        result["slot"] = slot;
        result["entity"] = FormatAddress(entity);

        // Try to identify entity class via RTTI
        std::string class_name = IdentifyClassFromPointer(pid, entity, cs2_entity_cache_.client_base);
        if (!class_name.empty()) {
            result["class"] = class_name;
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
