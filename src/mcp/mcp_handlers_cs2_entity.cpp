/**
 * MCP Handlers - CS2 Entity
 *
 * Counter-Strike 2 entity system handlers:
 * - HandleCS2Init (one-shot initialization)
 * - HandleCS2Identify
 * - HandleCS2ReadField
 * - HandleCS2Inspect
 * - HandleCS2GetLocalPlayer
 * - HandleCS2GetEntity
 * - Helper functions (StripTypePrefix, IdentifyClassFromPointer)
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "dumper/cs2_schema.h"
#include "analysis/rtti_parser.h"
#include "utils/cache_manager.h"
#include "utils/string_utils.h"
#include "utils/type_resolver.h"
#include "utils/memory_reader.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <cstring>

using json = nlohmann::json;

namespace orpheus::mcp {

// Helper function to strip "class " or "struct " prefix from RTTI names
std::string MCPServer::StripTypePrefix(const std::string& name) {
    if (name.substr(0, 6) == "class ") return name.substr(6);
    if (name.substr(0, 7) == "struct ") return name.substr(7);
    return name;
}

// Helper function to identify class from an object pointer using RTTI
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

std::string MCPServer::HandleCS2Init(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        bool force_refresh = req.value("force_refresh", false);

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        json result;
        result["pid"] = pid;

        // ===== STEP 1: Initialize Schema System =====
        auto schemasystem_mod = dma->GetModuleByName(pid, "schemasystem.dll");
        if (!schemasystem_mod) {
            return CreateErrorResponse("schemasystem.dll not found - is this Counter-Strike 2?");
        }

        // Create or recreate dumper if PID changed
        if (cs2_schema_ && cs2_schema_pid_ != pid) {
            cs2_schema_.reset();
        }

        if (!cs2_schema_) {
            cs2_schema_ = std::make_unique<orpheus::dumper::CS2SchemaDumper>(dma, pid);
            cs2_schema_pid_ = pid;
        }

        auto* dumper = cs2_schema_.get();

        if (!dumper->Initialize(schemasystem_mod->base_address)) {
            return CreateErrorResponse("Failed to initialize CS2 Schema: " + dumper->GetLastError());
        }

        // ===== STEP 2: Dump Schema (or load from cache) =====
        uint32_t module_size = schemasystem_mod->size;
        std::string cache_key = "all_deduplicated";
        size_t class_count = 0;
        size_t field_count = 0;
        bool schema_cached = false;

        if (!force_refresh && cs2_schema_cache_.Exists(cache_key, module_size)) {
            std::string cached = cs2_schema_cache_.Load(cache_key, module_size);
            if (!cached.empty()) {
                json cache_data = json::parse(cached);
                class_count = cache_data.contains("classes") ? cache_data["classes"].size() : 0;
                // Count fields
                if (cache_data.contains("classes")) {
                    for (const auto& cls : cache_data["classes"]) {
                        if (cls.contains("fields")) {
                            field_count += cls["fields"].size();
                        }
                    }
                }
                schema_cached = true;
            }
        }

        if (!schema_cached) {
            // Perform fresh dump with deduplication (built into DumpAllDeduplicated)
            auto all_classes = dumper->DumpAllDeduplicated();
            class_count = all_classes.size();

            // Build cache data
            json cache_data;
            cache_data["scope"] = cache_key;
            cache_data["scopes_processed"] = dumper->GetScopes().size();
            cache_data["deduplicated"] = true;
            cache_data["classes"] = json::array();

            for (const auto& cls : all_classes) {
                json c;
                c["name"] = cls.name;
                c["module"] = cls.module;
                c["size"] = cls.size;
                c["base_class"] = cls.base_class;

                json fields = json::array();
                for (const auto& field : cls.fields) {
                    json f;
                    f["name"] = field.name;
                    f["offset"] = field.offset;
                    f["type"] = field.type_name;
                    f["size"] = field.size;
                    fields.push_back(f);
                    field_count++;
                }
                c["fields"] = fields;
                cache_data["classes"].push_back(c);
            }

            cs2_schema_cache_.Save(cache_key, module_size, cache_data.dump(2));
        }

        json schema_info;
        schema_info["scopes"] = dumper->GetScopes().size();
        schema_info["classes"] = class_count;
        schema_info["fields"] = field_count;
        schema_info["cached"] = schema_cached;
        result["schema"] = schema_info;

        // ===== STEP 3: Initialize Entity System =====
        auto client_mod = dma->GetModuleByName(pid, "client.dll");
        if (!client_mod) {
            result["entity_system"] = nullptr;
            result["warning"] = "client.dll not found - entity system not initialized";
            return CreateSuccessResponse(result.dump());
        }

        cs2_entity_cache_.client_base = client_mod->base_address;
        cs2_entity_cache_.client_size = client_mod->size;

        // ===== STEP 3.5: RTTI Scan for client.dll (enables class identification) =====
        json rtti_info;
        size_t rtti_class_count = 0;
        bool rtti_cached = false;

        if (!force_refresh && rtti_cache_.Exists("client.dll", client_mod->size)) {
            std::string cached_rtti = rtti_cache_.Load("client.dll", client_mod->size);
            if (!cached_rtti.empty()) {
                json rtti_data = json::parse(cached_rtti);
                rtti_class_count = rtti_data.contains("classes") ? rtti_data["classes"].size() : 0;
                rtti_cached = true;
            }
        }

        if (!rtti_cached) {
            // Perform RTTI scan
            analysis::RTTIParser parser(
                [dma, pid](uint64_t addr, size_t size) {
                    return dma->ReadMemory(pid, addr, size);
                },
                client_mod->base_address
            );

            // Collect discovered classes via callback
            std::vector<analysis::RTTIClassInfo> found_classes;
            rtti_class_count = parser.ScanModule(client_mod->base_address,
                [&found_classes](const analysis::RTTIClassInfo& info) {
                    found_classes.push_back(info);
                });

            if (!found_classes.empty()) {
                // Build cache
                json cache_data;
                cache_data["module"] = "client.dll";
                cache_data["module_base_rva"] = 0;  // RVA from module base
                cache_data["scan_size"] = client_mod->size;

                json classes_array = json::array();
                for (const auto& info : found_classes) {
                    json cls;
                    cls["vtable_rva"] = info.vtable_address - client_mod->base_address;
                    cls["methods"] = info.method_count;
                    cls["flags"] = info.GetFlags();
                    cls["type"] = info.demangled_name;
                    cls["hierarchy"] = info.GetHierarchyString();
                    classes_array.push_back(cls);
                }
                cache_data["classes"] = classes_array;

                rtti_cache_.Save("client.dll", client_mod->size, cache_data.dump(2));
            }
        }

        rtti_info["module"] = "client.dll";
        rtti_info["classes"] = rtti_class_count;
        rtti_info["cached"] = rtti_cached;
        result["rtti"] = rtti_info;

        // Pattern: CGameEntitySystem
        const uint8_t entity_system_pattern[] = {
            0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD3,
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF0
        };
        const char entity_system_mask[] = "xxx????xx????xxx";

        // Pattern: LocalPlayerController array
        const uint8_t local_player_pattern[] = {
            0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x04, 0xC1
        };
        const char local_player_mask[] = "xxx????xxxx";

        auto client_data = dma->ReadMemory(pid, client_mod->base_address, client_mod->size);
        if (client_data.empty()) {
            result["entity_system"] = nullptr;
            result["warning"] = "Failed to read client.dll memory";
            return CreateSuccessResponse(result.dump());
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

        // Resolve entity system pointer
        if (entity_system_match != 0) {
            int32_t offset;
            std::memcpy(&offset, client_data.data() + (entity_system_match - client_mod->base_address) + 3, 4);
            uint64_t ptr_addr = entity_system_match + 7 + offset;

            auto ptr_data = dma->ReadMemory(pid, ptr_addr, 8);
            if (ptr_data.size() == 8) {
                std::memcpy(&cs2_entity_cache_.entity_system, ptr_data.data(), 8);
            }
        }

        // Resolve local player controller array
        if (local_player_match != 0) {
            int32_t offset;
            std::memcpy(&offset, client_data.data() + (local_player_match - client_mod->base_address) + 3, 4);
            cs2_entity_cache_.local_player_controller = local_player_match + 7 + offset;
        }

        if (cs2_entity_cache_.entity_system != 0 && cs2_entity_cache_.local_player_controller != 0) {
            cs2_entity_cache_.initialized = true;
        }

        result["entity_system"] = FormatAddress(cs2_entity_cache_.entity_system);
        result["client_base"] = FormatAddress(cs2_entity_cache_.client_base);
        result["client_size"] = cs2_entity_cache_.client_size;

        // ===== STEP 4: Get Local Player Info =====
        json local_player;
        if (cs2_entity_cache_.initialized) {
            uint64_t controller_ptr_addr = cs2_entity_cache_.local_player_controller;
            auto ptr_data = dma->ReadMemory(pid, controller_ptr_addr, 8);
            if (ptr_data.size() >= 8) {
                uint64_t controller;
                std::memcpy(&controller, ptr_data.data(), 8);

                if (controller != 0) {
                    local_player["controller"] = FormatAddress(controller);

                    // Identify class
                    std::string class_name = IdentifyClassFromPointer(pid, controller, cs2_entity_cache_.client_base);
                    if (!class_name.empty()) {
                        local_player["controller_class"] = class_name;
                    }

                    // Read key fields from schema
                    uint32_t pawn_offset = dumper->GetOffset("CCSPlayerController", "m_hPlayerPawn");
                    if (pawn_offset != 0) {
                        auto pawn_data = dma->ReadMemory(pid, controller + pawn_offset, 4);
                        if (pawn_data.size() >= 4) {
                            uint32_t pawn_handle;
                            std::memcpy(&pawn_handle, pawn_data.data(), 4);
                            local_player["pawn_handle"] = pawn_handle;
                            local_player["pawn_entity_index"] = pawn_handle & 0x7FFF;
                        }
                    }

                    uint32_t health_offset = dumper->GetOffset("CCSPlayerController", "m_iPawnHealth");
                    if (health_offset != 0) {
                        auto health_data = dma->ReadMemory(pid, controller + health_offset, 4);
                        if (health_data.size() >= 4) {
                            uint32_t health;
                            std::memcpy(&health, health_data.data(), 4);
                            local_player["health"] = health;
                        }
                    }

                    uint32_t armor_offset = dumper->GetOffset("CCSPlayerController", "m_iPawnArmor");
                    if (armor_offset != 0) {
                        auto armor_data = dma->ReadMemory(pid, controller + armor_offset, 4);
                        if (armor_data.size() >= 4) {
                            int32_t armor;
                            std::memcpy(&armor, armor_data.data(), 4);
                            local_player["armor"] = armor;
                        }
                    }
                }
            }
        }
        result["local_player"] = local_player.empty() ? json(nullptr) : local_player;
        result["ready"] = cs2_entity_cache_.initialized && !local_player.empty();

        // Load schema into memory cache for fast field lookups
        LoadSchemaIntoMemory();
        result["schema_mem_cached"] = schema_mem_cache_loaded_;
        {
            std::lock_guard<std::mutex> lock(schema_mem_cache_mutex_);
            result["schema_mem_classes"] = schema_mem_cache_.size();
        }

        LOG_INFO("CS2 initialized: {} classes, {} fields, entity_system={}, ready={}",
                 class_count, field_count,
                 FormatAddress(cs2_entity_cache_.entity_system),
                 result["ready"].get<bool>());

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
            auto* dumper = cs2_schema_.get();
            if (dumper->IsInitialized()) {
                const orpheus::dumper::SchemaClass* schema_class = dumper->FindClass(class_name);
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

        // Look up field from in-memory cache (ASLR-safe - offsets are class-relative)
        uint32_t field_offset = 0;
        std::string field_type;
        bool found = false;

        // Use in-memory cache for O(1) lookup
        const SchemaClassInfo* schema_class = FindSchemaClass(class_name);
        if (schema_class) {
            class_name = schema_class->name;  // Use actual case
            std::string field_lower = utils::string_utils::ToLower(field_name);
            for (const auto& fld : schema_class->fields) {
                if (utils::string_utils::ToLower(fld.name) == field_lower) {
                    field_offset = fld.offset;
                    field_type = fld.type;
                    field_name = fld.name;  // Use actual case
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            // Memory cache might not be loaded, provide helpful error
            if (!schema_mem_cache_loaded_) {
                return CreateErrorResponse("Schema not loaded - call cs2_init first");
            }
            return CreateErrorResponse("Field not found in cache: " + field_name + " in class " + class_name);
        }

        // Read field value using TypeResolver
        uint64_t field_addr = address + field_offset;
        size_t read_size = utils::TypeResolver::GetReadSize(field_type);

        auto data = dma->ReadMemory(pid, field_addr, read_size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read memory at field address");
        }

        json result;
        result["address"] = FormatAddress(address);
        result["class"] = class_name;
        result["field"] = field_name;
        result["type"] = field_type;
        result["offset"] = field_offset;
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << field_offset;
        result["offset_hex"] = ss.str();
        result["field_address"] = FormatAddress(field_addr);

        // Interpret value using TypeResolver
        auto type_info = utils::TypeResolver::Parse(field_type);
        json interpreted = utils::TypeResolver::Interpret(field_type, data);

        if (!interpreted.is_null()) {
            // Handle special cases for entity_index from handles
            if (type_info.category == utils::TypeResolver::Category::Handle && interpreted.is_object()) {
                result["value"] = interpreted["handle"];
                result["entity_index"] = interpreted["entity_index"];
            } else {
                result["value"] = interpreted;
            }
        } else {
            // Return raw hex for failed interpretation
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

        // Look up class from in-memory cache (ASLR-safe - offsets are class-relative)
        const SchemaClassInfo* schema_class = FindSchemaClass(class_name);
        if (!schema_class) {
            if (!schema_mem_cache_loaded_) {
                return CreateErrorResponse("Schema not loaded - call cs2_init first");
            }
            return CreateErrorResponse("Schema class not found in cache: " + class_name);
        }

        class_name = schema_class->name;  // Use actual case

        json result;
        result["address"] = FormatAddress(address);
        result["class"] = class_name;
        result["base_class"] = schema_class->parent;
        result["size"] = 0;  // Size not stored in memory cache

        json fields_out = json::array();
        int field_count = 0;

        for (const auto& field : schema_class->fields) {
            if (field_count >= max_fields) break;

            json f;
            f["name"] = field.name;
            f["type"] = field.type;
            f["offset"] = field.offset;
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << field.offset;
            f["offset_hex"] = ss.str();

            // Read field value using TypeResolver
            uint64_t field_addr = address + field.offset;
            size_t read_size = utils::TypeResolver::GetReadSize(field.type);

            auto data = dma->ReadMemory(pid, field_addr, read_size);
            if (!data.empty()) {
                json interpreted = utils::TypeResolver::Interpret(field.type, data);
                if (!interpreted.is_null()) {
                    f["value"] = interpreted;
                }
            }

            fields_out.push_back(f);
            field_count++;
        }

        result["fields"] = fields_out;
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
            return CreateErrorResponse("CS2 Entity system not initialized - call cs2_init first");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Create typed memory reader
        auto reader = utils::MakeReader(dma, pid);

        // Read local player controller from array
        uint64_t controller_ptr_addr = cs2_entity_cache_.local_player_controller + slot * 8;
        auto controller = reader.ReadPtr(controller_ptr_addr);
        if (!controller) {
            return CreateErrorResponse("Failed to read local player controller pointer");
        }

        if (*controller == 0) {
            json result;
            result["slot"] = slot;
            result["controller"] = nullptr;
            result["message"] = "No local player at this slot";
            return CreateSuccessResponse(result.dump());
        }

        json result;
        result["slot"] = slot;
        result["controller"] = FormatAddress(*controller);

        // Try to identify the controller class
        std::string class_name = IdentifyClassFromPointer(pid, *controller, cs2_entity_cache_.client_base);
        if (!class_name.empty()) {
            result["controller_class"] = class_name;
        }

        // Try to read pawn handle from controller
        if (cs2_schema_) {
            auto* dumper = cs2_schema_.get();

            uint32_t pawn_offset = dumper->GetOffset("CCSPlayerController", "m_hPlayerPawn");
            if (pawn_offset != 0) {
                if (auto pawn_handle = reader.ReadU32(*controller + pawn_offset)) {
                    result["pawn_handle"] = *pawn_handle;
                    result["pawn_entity_index"] = *pawn_handle & 0x7FFF;
                }
            }

            uint32_t health_offset = dumper->GetOffset("CCSPlayerController", "m_iPawnHealth");
            if (health_offset != 0) {
                if (auto health = reader.ReadU32(*controller + health_offset)) {
                    result["health"] = *health;
                }
            }

            uint32_t armor_offset = dumper->GetOffset("CCSPlayerController", "m_iPawnArmor");
            if (armor_offset != 0) {
                if (auto armor = reader.ReadI32(*controller + armor_offset)) {
                    result["armor"] = *armor;
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
            return CreateErrorResponse("CS2 Entity system not initialized - call cs2_init first");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Create typed memory reader
        auto reader = utils::MakeReader(dma, pid);

        // Calculate entity index from handle if needed
        int entity_index = index >= 0 ? index : (handle & 0x7FFF);

        // Entity list is at EntitySystem + 0x10, organized in chunks of 512
        // Each chunk pointer is at EntitySystem + 0x10 + chunk_index * 8
        // Within chunk: 8-byte header, then entries at stride 0x70

        int chunk_index = entity_index / 512;
        int slot = entity_index % 512;

        // Read chunk pointer
        uint64_t chunk_ptr_addr = cs2_entity_cache_.entity_system + 0x10 + chunk_index * 8;
        auto chunk_base_opt = reader.ReadPtr(chunk_ptr_addr);
        if (!chunk_base_opt) {
            return CreateErrorResponse("Failed to read entity chunk pointer");
        }

        // Some entity systems have flags in low bits
        uint64_t chunk_base = *chunk_base_opt & ~0xFULL;

        if (chunk_base == 0) {
            json result;
            result["entity_index"] = entity_index;
            result["entity"] = nullptr;
            result["message"] = "Entity chunk not allocated";
            return CreateSuccessResponse(result.dump());
        }

        // Read entity pointer from chunk (entries start at +8, stride 0x70)
        uint64_t entity_entry_addr = chunk_base + 0x08 + slot * 0x70;
        auto entity = reader.ReadPtr(entity_entry_addr);
        if (!entity) {
            return CreateErrorResponse("Failed to read entity entry");
        }

        if (*entity == 0) {
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
        result["entity"] = FormatAddress(*entity);

        // Try to identify entity class via RTTI
        std::string class_name = IdentifyClassFromPointer(pid, *entity, cs2_entity_cache_.client_base);
        if (!class_name.empty()) {
            result["class"] = class_name;
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2ListPlayers(const std::string& body) {
    try {
        json req = json::parse(body);
        uint32_t pid = req.value("pid", 0);
        bool include_bots = req.value("include_bots", true);
        bool include_position = req.value("include_position", false);
        bool include_spotted = req.value("include_spotted", false);

        if (pid == 0) {
            return CreateErrorResponse("Missing required parameter: pid");
        }

        if (!cs2_entity_cache_.initialized || cs2_entity_cache_.entity_system == 0) {
            return CreateErrorResponse("CS2 Entity system not initialized - call cs2_init first");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto reader = utils::MakeReader(dma, pid);

        // Controller offsets (from schema - updated 2026-02-15)
        constexpr uint32_t OFFSET_PLAYER_NAME = 0x6F8;      // m_iszPlayerName
        constexpr uint32_t OFFSET_TEAM_NUM = 0x3F3;         // m_iTeamNum (on controller)
        constexpr uint32_t OFFSET_PAWN_HANDLE = 0x90C;      // m_hPlayerPawn
        constexpr uint32_t OFFSET_PAWN_IS_ALIVE = 0x914;    // m_bPawnIsAlive
        constexpr uint32_t OFFSET_PAWN_HEALTH = 0x918;      // m_iPawnHealth
        constexpr uint32_t OFFSET_CONNECTED = 0x6F4;        // m_iConnected
        constexpr uint32_t OFFSET_STEAM_ID = 0x780;         // m_steamID
        constexpr uint32_t OFFSET_IS_LOCAL = 0x788;         // m_bIsLocalPlayerController

        // Pawn offsets
        constexpr uint32_t OFFSET_PAWN_TEAM = 0x3F3;        // m_iTeamNum on pawn
        constexpr uint32_t OFFSET_SCENE_NODE = 0x338;       // m_pGameSceneNode
        constexpr uint32_t OFFSET_ABS_ORIGIN = 0xD0;        // m_vecAbsOrigin on scene node

        // EntitySpottedState_t offsets (relative to pawn + 0x26E0)
        constexpr uint32_t OFFSET_SPOTTED_STATE = 0x26E0;   // m_entitySpottedState
        constexpr uint32_t OFFSET_SPOTTED = 0x08;           // m_bSpotted within EntitySpottedState_t
        constexpr uint32_t OFFSET_SPOTTED_MASK = 0x0C;      // m_bSpottedByMask within EntitySpottedState_t

        // Read chunk 0 pointer (player controllers are in indices 1-64)
        auto chunk0_ptr = reader.ReadPtr(cs2_entity_cache_.entity_system + 0x10);
        if (!chunk0_ptr || *chunk0_ptr == 0) {
            return CreateErrorResponse("Failed to read entity chunk 0");
        }
        uint64_t chunk0_base = *chunk0_ptr & ~0xFULL;

        json result;
        json players = json::array();
        int player_count = 0;

        // Iterate through controller indices 1-64
        for (int idx = 1; idx <= 64; idx++) {
            // Calculate entry address: chunk_base + 0x08 + slot * 0x70
            uint64_t entry_addr = chunk0_base + 0x08 + idx * 0x70;
            auto controller = reader.ReadPtr(entry_addr);

            if (!controller || *controller == 0) continue;

            // Check if it's a valid pointer (not a module address)
            if (*controller < 0x10000000000ULL) continue;

            // Read connection state (PlayerConnectedState: 0=Connected, 1=Connecting, 2=Reconnecting, 3+=Disconnected)
            auto connected = reader.ReadU32(*controller + OFFSET_CONNECTED);
            if (!connected || *connected > 2) continue; // Skip disconnected/reserved/never connected

            // Read player name
            auto name_data = dma->ReadMemory(pid, *controller + OFFSET_PLAYER_NAME, 64);
            if (name_data.empty()) continue;
            std::string name(reinterpret_cast<char*>(name_data.data()));
            if (name.empty()) continue;

            // Read Steam ID to check for bots
            auto steam_id = reader.ReadU64(*controller + OFFSET_STEAM_ID);
            bool is_bot = steam_id && *steam_id == 0;
            if (!include_bots && is_bot) continue;

            // Read pawn handle and alive status
            auto pawn_handle = reader.ReadU32(*controller + OFFSET_PAWN_HANDLE);
            auto is_alive = reader.ReadU8(*controller + OFFSET_PAWN_IS_ALIVE);
            auto health = reader.ReadU32(*controller + OFFSET_PAWN_HEALTH);
            auto team = reader.ReadU8(*controller + OFFSET_TEAM_NUM);
            auto is_local = reader.ReadU8(*controller + OFFSET_IS_LOCAL);

            json player;
            player["index"] = idx;
            player["controller"] = FormatAddress(*controller);
            player["name"] = name;
            player["team"] = team ? *team : 0;
            player["team_name"] = (team && *team == 2) ? "T" : (team && *team == 3) ? "CT" : "SPEC";
            player["is_alive"] = is_alive && *is_alive != 0;
            player["health"] = health ? *health : 0;
            player["is_bot"] = is_bot;
            player["is_local"] = is_local && *is_local != 0;

            if (pawn_handle && *pawn_handle != 0) {
                int pawn_index = *pawn_handle & 0x7FFF;
                player["pawn_handle"] = *pawn_handle;
                player["pawn_index"] = pawn_index;

                // Resolve pawn for position and/or spotted state
                if ((include_position || include_spotted) && is_alive && *is_alive) {
                    int chunk_idx = pawn_index / 512;
                    int slot = pawn_index % 512;

                    auto pawn_chunk = reader.ReadPtr(cs2_entity_cache_.entity_system + 0x10 + chunk_idx * 8);
                    if (pawn_chunk && *pawn_chunk != 0) {
                        uint64_t pawn_chunk_base = *pawn_chunk & ~0xFULL;
                        auto pawn = reader.ReadPtr(pawn_chunk_base + 0x08 + slot * 0x70);

                        if (pawn && *pawn != 0) {
                            player["pawn"] = FormatAddress(*pawn);

                            // Read position via GameSceneNode
                            if (include_position) {
                                auto scene_node = reader.ReadPtr(*pawn + OFFSET_SCENE_NODE);
                                if (scene_node && *scene_node != 0) {
                                    auto pos_data = dma->ReadMemory(pid, *scene_node + OFFSET_ABS_ORIGIN, 12);
                                    if (pos_data.size() >= 12) {
                                        float x, y, z;
                                        std::memcpy(&x, pos_data.data(), 4);
                                        std::memcpy(&y, pos_data.data() + 4, 4);
                                        std::memcpy(&z, pos_data.data() + 8, 4);
                                        player["position"] = {{"x", x}, {"y", y}, {"z", z}};
                                    }
                                }
                            }

                            // Read EntitySpottedState_t
                            if (include_spotted) {
                                // m_bSpotted at pawn + 0x26E0 + 0x08
                                auto spotted = reader.ReadU8(*pawn + OFFSET_SPOTTED_STATE + OFFSET_SPOTTED);
                                if (spotted) {
                                    player["is_spotted"] = *spotted != 0;
                                }

                                // m_bSpottedByMask at pawn + 0x26E0 + 0x0C (uint32[2])
                                auto mask_data = dma->ReadMemory(pid, *pawn + OFFSET_SPOTTED_STATE + OFFSET_SPOTTED_MASK, 8);
                                if (mask_data.size() >= 8) {
                                    uint32_t mask_low, mask_high;
                                    std::memcpy(&mask_low, mask_data.data(), 4);
                                    std::memcpy(&mask_high, mask_data.data() + 4, 4);

                                    // Convert to list of player indices who spotted this entity
                                    json spotted_by = json::array();
                                    for (int bit = 0; bit < 32; bit++) {
                                        if (mask_low & (1 << bit)) spotted_by.push_back(bit);
                                    }
                                    for (int bit = 0; bit < 32; bit++) {
                                        if (mask_high & (1 << bit)) spotted_by.push_back(32 + bit);
                                    }
                                    player["spotted_by_mask"] = {mask_low, mask_high};
                                    player["spotted_by"] = spotted_by;
                                }
                            }
                        }
                    }
                }
            }

            players.push_back(player);
            player_count++;
        }

        result["players"] = players;
        result["count"] = player_count;
        result["entity_system"] = FormatAddress(cs2_entity_cache_.entity_system);

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2GetGameState(const std::string& body) {
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

        json result;
        result["pid"] = pid;

        // Check if CS2 modules are loaded
        auto client_mod = dma->GetModuleByName(pid, "client.dll");
        auto engine_mod = dma->GetModuleByName(pid, "engine2.dll");

        result["client_loaded"] = client_mod.has_value();
        result["engine_loaded"] = engine_mod.has_value();

        if (!client_mod) {
            result["state"] = "not_in_game";
            result["message"] = "client.dll not loaded - likely in main menu or loading";
            return CreateSuccessResponse(result.dump());
        }

        // Check entity system initialization
        if (!cs2_entity_cache_.initialized) {
            result["state"] = "not_initialized";
            result["message"] = "Entity system not initialized - call cs2_init first";
            return CreateSuccessResponse(result.dump());
        }

        auto reader = utils::MakeReader(dma, pid);

        // Read highest entity index from EntitySystem + 0x20F0
        constexpr uint32_t OFFSET_HIGHEST_ENTITY = 0x20F0;
        auto highest_entity = reader.ReadI32(cs2_entity_cache_.entity_system + OFFSET_HIGHEST_ENTITY);

        result["entity_system"] = FormatAddress(cs2_entity_cache_.entity_system);
        result["highest_entity_index"] = highest_entity ? *highest_entity : 0;

        // Check if local player controller exists
        auto local_controller = reader.ReadPtr(cs2_entity_cache_.local_player_controller);
        bool has_local_player = local_controller && *local_controller != 0;
        result["has_local_player"] = has_local_player;

        // Determine game state based on entity count and local player
        if (!highest_entity || *highest_entity < 10) {
            result["state"] = "menu";
            result["message"] = "Very few entities - likely in main menu";
        } else if (!has_local_player) {
            result["state"] = "loading";
            result["message"] = "Entities exist but no local player - loading or spectating";
        } else {
            // Count player controllers to determine if in match
            int player_count = 0;
            auto chunk0_ptr = reader.ReadPtr(cs2_entity_cache_.entity_system + 0x10);
            if (chunk0_ptr && *chunk0_ptr != 0) {
                uint64_t chunk0_base = *chunk0_ptr & ~0xFULL;
                for (int i = 1; i <= 64; i++) {
                    auto controller = reader.ReadPtr(chunk0_base + 0x08 + i * 0x70);
                    if (controller && *controller != 0 && *controller > 0x10000000000ULL) {
                        auto connected = reader.ReadU32(*controller + 0x6F4);
                        if (connected && *connected <= 2) {  // 0=Connected, 1=Connecting, 2=Reconnecting
                            player_count++;
                        }
                    }
                }
            }

            result["connected_players"] = player_count;

            if (player_count > 1) {
                result["state"] = "in_match";
                result["message"] = "In active match with " + std::to_string(player_count) + " players";
            } else if (player_count == 1) {
                result["state"] = "in_game_solo";
                result["message"] = "In game solo (practice/workshop)";
            } else {
                result["state"] = "in_game";
                result["message"] = "In game";
            }
        }

        // Additional info: read local player health if available
        if (has_local_player) {
            auto health = reader.ReadU32(*local_controller + 0x918);
            auto alive = reader.ReadU8(*local_controller + 0x914);
            if (health) result["local_health"] = *health;
            if (alive) result["local_alive"] = *alive != 0;
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

// ============================================================================
// In-Memory Schema Cache Helpers
// ============================================================================

void MCPServer::LoadSchemaIntoMemory() {
    std::lock_guard<std::mutex> lock(schema_mem_cache_mutex_);

    // Clear existing cache
    schema_mem_cache_.clear();
    schema_mem_cache_loaded_ = false;

    namespace fs = std::filesystem;
    std::string cache_dir = cs2_schema_cache_.GetDirectory();

    if (!fs::exists(cache_dir)) {
        LOG_WARN("Schema cache directory does not exist: {}", cache_dir);
        return;
    }

    size_t class_count = 0;
    size_t field_count = 0;

    for (const auto& entry : fs::directory_iterator(cache_dir)) {
        if (entry.path().extension() != ".json") continue;

        std::ifstream in(entry.path());
        if (!in.is_open()) continue;

        try {
            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            std::string scope = cache_data.value("scope", "");

            for (const auto& cls : cache_data["classes"]) {
                SchemaClassInfo class_info;
                class_info.name = cls.value("name", "");
                class_info.scope = scope;
                class_info.parent = cls.value("base_class", "");

                if (cls.contains("fields")) {
                    for (const auto& fld : cls["fields"]) {
                        SchemaFieldInfo field_info;
                        field_info.name = fld.value("name", "");
                        field_info.type = fld.value("type", "");
                        field_info.offset = fld.value("offset", 0);
                        class_info.fields.push_back(std::move(field_info));
                        field_count++;
                    }
                }

                // Store with lowercase key for case-insensitive lookup
                std::string key = utils::string_utils::ToLower(class_info.name);
                schema_mem_cache_[key] = std::move(class_info);
                class_count++;
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to parse schema cache file {}: {}", entry.path().string(), e.what());
            continue;
        }
    }

    schema_mem_cache_loaded_ = true;
    LOG_INFO("Loaded {} classes with {} fields into memory cache", class_count, field_count);
}

const MCPServer::SchemaClassInfo* MCPServer::FindSchemaClass(const std::string& class_name) const {
    std::lock_guard<std::mutex> lock(schema_mem_cache_mutex_);

    if (!schema_mem_cache_loaded_) {
        return nullptr;
    }

    std::string key = utils::string_utils::ToLower(class_name);
    auto it = schema_mem_cache_.find(key);
    if (it != schema_mem_cache_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace orpheus::mcp
