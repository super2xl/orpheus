#include "cs2_schema.h"
#include "core/dma_interface.h"
#include "analysis/pattern_scanner.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

using json = nlohmann::json;
using orpheus::DMAInterface;

namespace dumper {

CS2SchemaDumper::CS2SchemaDumper(orpheus::DMAInterface* dma, uint32_t pid)
    : dma_(dma), pid_(pid) {
}

bool CS2SchemaDumper::Initialize(uint64_t schemasystem_base) {
    if (!dma_ || !dma_->IsConnected()) {
        last_error_ = "DMA not connected";
        return false;
    }

    schemasystem_base_ = schemasystem_base;

    if (!FindSchemaSystem(schemasystem_base)) {
        return false;
    }

    if (!EnumerateScopes()) {
        return false;
    }

    initialized_ = true;
    LOG_INFO("CS2 Schema System initialized: {} scopes found", scopes_.size());
    return true;
}

bool CS2SchemaDumper::FindSchemaSystem(uint64_t schemasystem_base) {
    // Pattern for GlobalTypeScope virtual call: 48 8B 05 ?? ?? ?? ?? 48 8B 40 48
    // This pattern finds the global CSchemaSystem pointer

    // Read module to scan for pattern
    auto mod_opt = dma_->GetModuleByName(pid_, "schemasystem.dll");
    if (!mod_opt) {
        last_error_ = "schemasystem.dll not found";
        return false;
    }

    // Try to find SchemaSystem_001 interface export
    // The interface is typically at a fixed offset from the module base
    // We'll scan for the interface pattern

    // Pattern: lea rax, [SchemaSystem] followed by GlobalTypeScope vfunc
    // Alternative: scan .rdata for "SchemaSystem_001" string and find xref

    const size_t scan_size = std::min((size_t)mod_opt->size, (size_t)(16 * 1024 * 1024));
    auto module_data = dma_->ReadMemory(pid_, schemasystem_base, scan_size);
    if (module_data.empty()) {
        last_error_ = "Failed to read schemasystem.dll memory";
        return false;
    }

    // Pattern for getting global SchemaSystem instance
    // 48 8B 0D ?? ?? ?? ?? 48 8B 01 - mov rcx, [g_pSchemaSystem]; mov rax, [rcx]
    auto pattern = orpheus::analysis::PatternScanner::Compile("48 8B 0D ?? ?? ?? ?? 48 8B 01");
    if (!pattern) {
        last_error_ = "Failed to compile SchemaSystem pattern";
        return false;
    }

    auto results = orpheus::analysis::PatternScanner::Scan(module_data, *pattern, schemasystem_base, 10);

    for (uint64_t match : results) {
        // Read the RIP-relative offset
        auto offset_data = dma_->ReadMemory(pid_, match + 3, 4);
        if (offset_data.size() < 4) continue;

        int32_t rel_offset = *reinterpret_cast<int32_t*>(offset_data.data());
        uint64_t schema_system_ptr = match + 7 + rel_offset;

        // Read the actual SchemaSystem pointer
        auto ptr_data = dma_->Read<uint64_t>(pid_, schema_system_ptr);
        if (!ptr_data || *ptr_data == 0) continue;

        schema_system_ = *ptr_data;

        // Verify by reading vtable
        auto vtable = dma_->Read<uint64_t>(pid_, schema_system_);
        if (vtable && *vtable > schemasystem_base && *vtable < schemasystem_base + mod_opt->size) {
            LOG_INFO("Found SchemaSystem at 0x{:X}", schema_system_);
            return true;
        }
    }

    // Alternative: Try direct interface pointer pattern
    // Many Source 2 games export the interface directly
    pattern = orpheus::analysis::PatternScanner::Compile("48 8D 05 ?? ?? ?? ?? C3");
    if (pattern) {
        results = orpheus::analysis::PatternScanner::Scan(module_data, *pattern, schemasystem_base, 50);

        for (uint64_t match : results) {
            auto offset_data = dma_->ReadMemory(pid_, match + 3, 4);
            if (offset_data.size() < 4) continue;

            int32_t rel_offset = *reinterpret_cast<int32_t*>(offset_data.data());
            uint64_t potential_schema = match + 7 + rel_offset;

            // Verify it looks like a SchemaSystem
            auto vtable = dma_->Read<uint64_t>(pid_, potential_schema);
            if (vtable && *vtable > schemasystem_base && *vtable < schemasystem_base + mod_opt->size) {
                // Additional verify: check GlobalTypeScope returns valid pointer
                // GlobalTypeScope is typically vfunc index 11
                auto vfunc_addr = dma_->Read<uint64_t>(pid_, *vtable + 11 * 8);
                if (vfunc_addr && *vfunc_addr > schemasystem_base) {
                    schema_system_ = potential_schema;
                    LOG_INFO("Found SchemaSystem via lea pattern at 0x{:X}", schema_system_);
                    return true;
                }
            }
        }
    }

    last_error_ = "Could not locate SchemaSystem interface";
    return false;
}

bool CS2SchemaDumper::EnumerateScopes() {
    scopes_.clear();

    // Read GlobalTypeScope (vfunc index 11)
    auto vtable = dma_->Read<uint64_t>(pid_, schema_system_);
    if (!vtable) {
        last_error_ = "Failed to read SchemaSystem vtable";
        return false;
    }

    // GlobalTypeScope is at vfunc slot 11
    auto global_scope_func = dma_->Read<uint64_t>(pid_, *vtable + 11 * 8);
    if (!global_scope_func) {
        last_error_ = "Failed to read GlobalTypeScope vfunc";
        return false;
    }

    // The global scope is typically stored as a static and returned directly
    // We need to find the scope pointer - scan the function for lea rax pattern
    auto func_data = dma_->ReadMemory(pid_, *global_scope_func, 64);
    if (func_data.empty()) {
        last_error_ = "Failed to read GlobalTypeScope function";
        return false;
    }

    // Look for: lea rax, [rip+??] or mov rax, [rip+??]
    for (size_t i = 0; i + 7 < func_data.size(); i++) {
        // lea rax, [rip+disp32] = 48 8D 05 xx xx xx xx
        // mov rax, [rip+disp32] = 48 8B 05 xx xx xx xx
        if (func_data[i] == 0x48 &&
            (func_data[i+1] == 0x8D || func_data[i+1] == 0x8B) &&
            func_data[i+2] == 0x05) {

            int32_t rel_offset = *reinterpret_cast<int32_t*>(&func_data[i+3]);
            uint64_t target = *global_scope_func + i + 7 + rel_offset;

            // For mov, dereference to get actual pointer
            if (func_data[i+1] == 0x8B) {
                auto deref = dma_->Read<uint64_t>(pid_, target);
                if (deref && *deref != 0) {
                    target = *deref;
                }
            }

            // Validate it looks like a scope
            auto scope_data = dma_->ReadMemory(pid_, target, 16);
            if (!scope_data.empty()) {
                global_scope_ = target;

                SchemaScope scope;
                scope.name = "GlobalTypeScope";
                scope.address = global_scope_;
                scopes_.push_back(scope);

                LOG_INFO("Found GlobalTypeScope at 0x{:X}", global_scope_);
                break;
            }
        }
    }

    // Try to find GetAllTypeScope pattern for additional scopes
    // Pattern from Andromeda: 48 8B 05 ?? ?? ?? ?? 48 8B D6 0F B7 CB 48 8B 3C C8
    auto mod_opt = dma_->GetModuleByName(pid_, "schemasystem.dll");
    if (mod_opt) {
        auto module_data = dma_->ReadMemory(pid_, schemasystem_base_,
            std::min((size_t)mod_opt->size, (size_t)(8 * 1024 * 1024)));

        // Use the full Andromeda pattern for GetAllTypeScope
        auto pattern = orpheus::analysis::PatternScanner::Compile(
            "48 8B 05 ?? ?? ?? ?? 48 8B D6 0F B7 CB 48 8B 3C C8");
        if (pattern && !module_data.empty()) {
            auto results = orpheus::analysis::PatternScanner::Scan(module_data, *pattern, schemasystem_base_, 5);

            for (uint64_t match : results) {
                LOG_INFO("Found GetAllTypeScope pattern at 0x{:X}", match);

                // Read RIP-relative offset at match+3
                auto offset_data = dma_->ReadMemory(pid_, match + 3, 4);
                if (offset_data.size() < 4) continue;

                int32_t rel_offset = *reinterpret_cast<int32_t*>(offset_data.data());
                uint64_t scope_array_ptr = match + 7 + rel_offset;

                // Dereference to get actual array pointer
                auto arr_ptr = dma_->Read<uint64_t>(pid_, scope_array_ptr);
                if (!arr_ptr || *arr_ptr == 0) {
                    LOG_WARN("Invalid scope array pointer at 0x{:X}", scope_array_ptr);
                    continue;
                }

                LOG_INFO("Scope array at 0x{:X}", *arr_ptr);

                // Try to find scope count - usually stored after the pattern
                // In Andromeda: 0F B7 CB = movzx ecx, bx (scope count in BX)
                // Look for the count at a nearby location
                // For now, iterate with reasonable limit
                for (int i = 0; i < 32; i++) {
                    auto scope_ptr = dma_->Read<uint64_t>(pid_, *arr_ptr + i * 8);
                    if (!scope_ptr || *scope_ptr == 0) break;

                    // Verify it's not already in our list
                    bool exists = false;
                    for (const auto& s : scopes_) {
                        if (s.address == *scope_ptr) {
                            exists = true;
                            break;
                        }
                    }
                    if (exists) continue;

                    // Read scope name - CSchemaSystemTypeScope has name at beginning of struct
                    // Try multiple offsets since it varies between versions
                    std::string scope_name;
                    for (uint64_t name_offset : {0x8ULL, 0x0ULL, 0x10ULL}) {
                        auto name_ptr = dma_->Read<uint64_t>(pid_, *scope_ptr + name_offset);
                        if (name_ptr && *name_ptr != 0) {
                            scope_name = ReadString(*name_ptr);
                            if (!scope_name.empty() && scope_name.find(".dll") != std::string::npos) {
                                break;
                            }
                            // Also try direct string (not pointer)
                            scope_name = ReadString(*scope_ptr + name_offset);
                            if (!scope_name.empty() && scope_name.find(".dll") != std::string::npos) {
                                break;
                            }
                        }
                    }

                    if (scope_name.empty()) {
                        scope_name = "Scope_" + std::to_string(i);
                    }

                    SchemaScope scope;
                    scope.name = scope_name;
                    scope.address = *scope_ptr;
                    scopes_.push_back(scope);

                    LOG_INFO("Found scope[{}]: {} at 0x{:X}", i, scope_name, *scope_ptr);
                }

                if (scopes_.size() > 1) {
                    all_scopes_ptr_ = *arr_ptr;
                    LOG_INFO("Found {} type scopes total", scopes_.size());
                    break;
                }
            }
        }

        // Fallback: if specific pattern didn't work, try simpler pattern
        if (scopes_.size() <= 1) {
            LOG_WARN("Specific pattern failed, trying fallback pattern");
            pattern = orpheus::analysis::PatternScanner::Compile("48 8B 05 ?? ?? ?? ?? 48 8B D6");
            if (pattern) {
                auto results = orpheus::analysis::PatternScanner::Scan(module_data, *pattern, schemasystem_base_, 20);
                for (uint64_t match : results) {
                    auto offset_data = dma_->ReadMemory(pid_, match + 3, 4);
                    if (offset_data.size() < 4) continue;

                    int32_t rel_offset = *reinterpret_cast<int32_t*>(offset_data.data());
                    uint64_t scope_array_ptr = match + 7 + rel_offset;

                    auto arr_ptr = dma_->Read<uint64_t>(pid_, scope_array_ptr);
                    if (!arr_ptr || *arr_ptr == 0) continue;

                    for (int i = 0; i < 32; i++) {
                        auto scope_ptr = dma_->Read<uint64_t>(pid_, *arr_ptr + i * 8);
                        if (!scope_ptr || *scope_ptr == 0) break;

                        bool exists = false;
                        for (const auto& s : scopes_) {
                            if (s.address == *scope_ptr) { exists = true; break; }
                        }
                        if (exists) continue;

                        std::string scope_name;
                        for (uint64_t name_offset : {0x8ULL, 0x0ULL, 0x10ULL}) {
                            auto name_ptr = dma_->Read<uint64_t>(pid_, *scope_ptr + name_offset);
                            if (name_ptr && *name_ptr != 0) {
                                scope_name = ReadString(*name_ptr);
                                if (!scope_name.empty() && scope_name.find(".dll") != std::string::npos) break;
                                scope_name = ReadString(*scope_ptr + name_offset);
                                if (!scope_name.empty() && scope_name.find(".dll") != std::string::npos) break;
                            }
                        }
                        if (scope_name.empty()) scope_name = "Scope_" + std::to_string(i);

                        SchemaScope scope;
                        scope.name = scope_name;
                        scope.address = *scope_ptr;
                        scopes_.push_back(scope);
                    }

                    if (scopes_.size() > 1) {
                        all_scopes_ptr_ = *arr_ptr;
                        break;
                    }
                }
            }
        }
    }

    return !scopes_.empty();
}

std::vector<SchemaClass> CS2SchemaDumper::DumpScope(uint64_t scope_addr,
    std::function<void(int, int)> progress) {

    std::vector<SchemaClass> classes;

    // CSchemaList is embedded at SCOPE_CLASS_CONTAINER_OFFSET, not a pointer
    // The structure layout is:
    //   [at -0x74 relative to BlockContainers: int num_schemas]
    //   [at 0x0: BlockContainers array of 256 * 0x18 bytes]
    uint64_t container_addr = scope_addr + SCOPE_CLASS_CONTAINER_OFFSET;

    // Read number of schemas (at offset -0x74 from container start)
    auto num_schemas = dma_->Read<int32_t>(pid_, container_addr + SCHEMA_LIST_NUM_SCHEMA_OFFSET);
    int total_count = num_schemas ? *num_schemas : 0;

    LOG_INFO("Reading schema count from 0x{:X}, got {}", container_addr + SCHEMA_LIST_NUM_SCHEMA_OFFSET, total_count);

    if (total_count <= 0 || total_count > 100000) {
        LOG_WARN("Invalid schema count {} at scope 0x{:X}", total_count, scope_addr);
        return classes;
    }

    LOG_INFO("Dumping {} classes from scope 0x{:X}", total_count, scope_addr);

    int block_index = 0;

    // Iterate 256 bucket containers
    for (int bucket = 0; bucket < SCHEMA_BUCKET_COUNT && block_index < total_count; bucket++) {
        // Each bucket container is 0x18 bytes: [void*, void*, SchemaBlock*]
        uint64_t bucket_addr = container_addr + bucket * 0x18;

        // Read first block pointer (at offset 0x10)
        auto first_block = dma_->Read<uint64_t>(pid_, bucket_addr + 0x10);
        if (!first_block || *first_block == 0) continue;

        // Walk the linked list
        uint64_t current_block = *first_block;
        while (current_block != 0 && block_index < total_count) {
            // SchemaBlock: [void*, SchemaBlock* next, CSchemaClassBinding* binding]
            auto next_block = dma_->Read<uint64_t>(pid_, current_block + 0x8);
            auto binding_ptr = dma_->Read<uint64_t>(pid_, current_block + 0x10);

            if (binding_ptr && *binding_ptr != 0) {
                SchemaClass cls;
                if (ReadClassBinding(*binding_ptr, cls)) {
                    classes.push_back(std::move(cls));
                }
            }

            block_index++;
            if (progress) {
                progress(block_index, total_count);
            }

            current_block = next_block ? *next_block : 0;
        }
    }

    return classes;
}

bool CS2SchemaDumper::ReadClassBinding(uint64_t binding_addr, SchemaClass& out_class) {
    // Read class name
    auto name_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_NAME_OFFSET);
    if (!name_ptr || *name_ptr == 0) return false;

    out_class.name = ReadString(*name_ptr);
    if (out_class.name.empty()) return false;

    // Read DLL name
    auto dll_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_DLL_OFFSET);
    if (dll_ptr && *dll_ptr != 0) {
        out_class.module = ReadString(*dll_ptr);
    }

    // Read class size
    auto size = dma_->Read<int32_t>(pid_, binding_addr + BINDING_SIZE_OFFSET);
    if (size) {
        out_class.size = *size;
    }

    // Read field count
    auto field_count = dma_->Read<uint16_t>(pid_, binding_addr + BINDING_FIELD_COUNT_OFFSET);
    uint16_t num_fields = field_count ? *field_count : 0;

    // Read field array pointer
    auto field_array_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_FIELD_ARRAY_OFFSET);
    if (field_array_ptr && *field_array_ptr != 0 && num_fields > 0) {
        ReadFieldArray(*field_array_ptr, num_fields, out_class.fields);
    }

    // Read base class
    auto base_class_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_BASE_CLASS_OFFSET);
    if (base_class_ptr && *base_class_ptr != 0) {
        // Base class info has binding at offset 0x8
        auto base_binding = dma_->Read<uint64_t>(pid_, *base_class_ptr + 0x8);
        if (base_binding && *base_binding != 0) {
            auto base_name_ptr = dma_->Read<uint64_t>(pid_, *base_binding + BINDING_NAME_OFFSET);
            if (base_name_ptr && *base_name_ptr != 0) {
                out_class.base_class = ReadString(*base_name_ptr);
            }
        }
    }

    return true;
}

bool CS2SchemaDumper::ReadFieldArray(uint64_t array_addr, uint16_t count,
    std::vector<SchemaField>& out_fields) {

    out_fields.reserve(count);

    for (uint16_t i = 0; i < count; i++) {
        uint64_t field_addr = array_addr + i * FIELD_ENTRY_SIZE;

        // Field structure:
        // 0x00: const char* FieldName
        // 0x08: CSchemaType* FieldType
        // 0x10: int FieldOffset
        // 0x14+: padding/metadata

        auto name_ptr = dma_->Read<uint64_t>(pid_, field_addr);
        if (!name_ptr || *name_ptr == 0) continue;

        SchemaField field;
        field.name = ReadString(*name_ptr);
        if (field.name.empty()) continue;

        // Read type name (CSchemaType has name at offset 0x8 typically)
        auto type_ptr = dma_->Read<uint64_t>(pid_, field_addr + 0x8);
        if (type_ptr && *type_ptr != 0) {
            auto type_name_ptr = dma_->Read<uint64_t>(pid_, *type_ptr + 0x8);
            if (type_name_ptr && *type_name_ptr != 0) {
                field.type_name = ReadString(*type_name_ptr);
            }
        }

        auto offset = dma_->Read<int32_t>(pid_, field_addr + 0x10);
        if (offset) {
            field.offset = *offset;
        }

        out_fields.push_back(std::move(field));
    }

    return true;
}

std::string CS2SchemaDumper::ReadString(uint64_t addr, size_t max_len) {
    if (addr == 0) return "";

    auto data = dma_->ReadMemory(pid_, addr, max_len);
    if (data.empty()) return "";

    // Find null terminator
    size_t len = 0;
    while (len < data.size() && data[len] != 0) {
        len++;
    }

    return std::string(reinterpret_cast<char*>(data.data()), len);
}

std::unordered_map<std::string, std::vector<SchemaClass>> CS2SchemaDumper::DumpAll(
    std::function<void(int, int)> progress) {

    cached_schemas_.clear();

    int total_classes = 0;
    for (const auto& scope : scopes_) {
        auto classes = DumpScope(scope.address, progress);
        if (!classes.empty()) {
            cached_schemas_[scope.name] = std::move(classes);
            total_classes += cached_schemas_[scope.name].size();
        }
    }

    LOG_INFO("Dumped {} total classes from {} scopes", total_classes, scopes_.size());
    return cached_schemas_;
}

std::vector<SchemaClass> CS2SchemaDumper::DumpAllDeduplicated(
    std::function<void(int, int)> progress) {

    // Use a map for deduplication - key is class name
    // Later scopes overwrite earlier ones (matches Andromeda behavior)
    std::unordered_map<std::string, SchemaClass> class_map;

    int total_processed = 0;
    int scopes_processed = 0;

    // Iterate all scopes (GlobalTypeScope should be first, then module scopes)
    for (const auto& scope : scopes_) {
        LOG_INFO("Dumping scope: {} (0x{:X})", scope.name, scope.address);

        auto classes = DumpScope(scope.address, [&](int current, int total) {
            if (progress) {
                progress(total_processed + current, -1); // -1 indicates unknown total
            }
        });

        // Add/overwrite classes in map (deduplication by name)
        for (auto& cls : classes) {
            class_map[cls.name] = std::move(cls);
        }

        total_processed += classes.size();
        scopes_processed++;
        LOG_INFO("Scope {} complete: {} classes (total unique so far: {})",
                 scope.name, classes.size(), class_map.size());
    }

    // Convert map to vector
    std::vector<SchemaClass> result;
    result.reserve(class_map.size());
    for (auto& [name, cls] : class_map) {
        result.push_back(std::move(cls));
    }

    // Also update cached_schemas_ with a merged "all" entry
    cached_schemas_.clear();
    cached_schemas_["all_deduplicated"] = result;

    LOG_INFO("DumpAllDeduplicated complete: {} scopes, {} total classes processed, {} unique classes",
             scopes_processed, total_processed, result.size());

    return result;
}

uint32_t CS2SchemaDumper::GetOffset(const std::string& class_name,
    const std::string& field_name) const {

    for (const auto& [scope_name, classes] : cached_schemas_) {
        for (const auto& cls : classes) {
            if (cls.name == class_name) {
                for (const auto& field : cls.fields) {
                    if (field.name == field_name) {
                        return field.offset;
                    }
                }
            }
        }
    }
    return 0;
}

const SchemaClass* CS2SchemaDumper::FindClass(const std::string& class_name) const {
    for (const auto& [scope_name, classes] : cached_schemas_) {
        for (const auto& cls : classes) {
            if (cls.name == class_name) {
                return &cls;
            }
        }
    }
    return nullptr;
}

size_t CS2SchemaDumper::GetTotalClassCount() const {
    size_t count = 0;
    for (const auto& [scope, classes] : cached_schemas_) {
        count += classes.size();
    }
    return count;
}

size_t CS2SchemaDumper::GetTotalFieldCount() const {
    size_t count = 0;
    for (const auto& [scope, classes] : cached_schemas_) {
        for (const auto& cls : classes) {
            count += cls.fields.size();
        }
    }
    return count;
}

bool CS2SchemaDumper::ExportToJson(const std::string& filepath) const {
    try {
        json root;

        // Metadata
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream time_ss;
        time_ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");

        root["exported_at"] = time_ss.str();
        root["schema_system"] = "0x" + ([&]() {
            std::stringstream ss;
            ss << std::hex << std::uppercase << schema_system_;
            return ss.str();
        })();
        root["total_classes"] = GetTotalClassCount();
        root["total_fields"] = GetTotalFieldCount();

        // Scopes and classes
        json scopes_json = json::object();

        for (const auto& [scope_name, classes] : cached_schemas_) {
            json classes_array = json::array();

            for (const auto& cls : classes) {
                json cls_json;
                cls_json["name"] = cls.name;
                cls_json["module"] = cls.module;
                cls_json["size"] = cls.size;
                cls_json["base_class"] = cls.base_class;

                json fields_array = json::array();
                for (const auto& field : cls.fields) {
                    json field_json;
                    field_json["name"] = field.name;
                    field_json["type"] = field.type_name;
                    field_json["offset"] = field.offset;
                    fields_array.push_back(field_json);
                }
                cls_json["fields"] = fields_array;

                classes_array.push_back(cls_json);
            }

            scopes_json[scope_name] = classes_array;
        }

        root["scopes"] = scopes_json;

        // Write to file
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        file << root.dump(2);
        file.close();

        LOG_INFO("Exported schema to {}", filepath);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to export schema JSON: {}", e.what());
        return false;
    }
}

bool CS2SchemaDumper::ExportToHeader(const std::string& filepath) const {
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream time_ss;
        time_ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");

        file << "// CS2 Schema Offsets\n";
        file << "// Generated by Orpheus DMA Framework\n";
        file << "// Date: " << time_ss.str() << "\n";
        file << "// Total Classes: " << GetTotalClassCount() << "\n";
        file << "// Total Fields: " << GetTotalFieldCount() << "\n";
        file << "\n#pragma once\n\n";
        file << "#include <cstdint>\n\n";

        file << "namespace cs2_schema {\n\n";

        for (const auto& [scope_name, classes] : cached_schemas_) {
            file << "// Scope: " << scope_name << "\n";
            file << "// Classes: " << classes.size() << "\n\n";

            for (const auto& cls : classes) {
                // Sanitize class name for C++ namespace
                std::string safe_name = cls.name;
                for (char& c : safe_name) {
                    if (!isalnum(c) && c != '_') c = '_';
                }

                file << "namespace " << safe_name << " {\n";
                file << "    constexpr uint32_t class_size = 0x"
                     << std::hex << std::uppercase << cls.size << ";\n";

                if (!cls.base_class.empty()) {
                    file << "    // Base: " << cls.base_class << "\n";
                }

                for (const auto& field : cls.fields) {
                    // Sanitize field name
                    std::string safe_field = field.name;
                    for (char& c : safe_field) {
                        if (!isalnum(c) && c != '_') c = '_';
                    }

                    file << "    constexpr uint32_t " << safe_field
                         << " = 0x" << std::hex << std::uppercase << field.offset;

                    if (!field.type_name.empty()) {
                        file << "; // " << field.type_name;
                    }
                    file << "\n";
                }

                file << "}\n\n";
            }
        }

        file << "} // namespace cs2_schema\n";
        file.close();

        LOG_INFO("Exported schema header to {}", filepath);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to export schema header: {}", e.what());
        return false;
    }
}

} // namespace dumper
