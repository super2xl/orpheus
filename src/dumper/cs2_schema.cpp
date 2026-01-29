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

namespace orpheus::dumper {

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

    // Andromeda/anger approach: GlobalTypeScope FIRST, then all scopes from structure
    // This ensures we get all classes even if some scopes overlap

    LOG_INFO("Enumerating type scopes from CSchemaSystem at 0x{:X}", schema_system_);

    // Step 1: Try to get GlobalTypeScope via vfunc 11 (like anger does internally)
    // Since we're external (DMA), we need to parse the vfunc to find the global pointer
    bool found_global_scope = false;
    auto vtable = dma_->Read<uint64_t>(pid_, schema_system_);
    if (vtable) {
        auto global_scope_func = dma_->Read<uint64_t>(pid_, *vtable + 11 * 8);
        if (global_scope_func && *global_scope_func != 0) {
            LOG_INFO("GlobalTypeScope vfunc at 0x{:X}", *global_scope_func);

            // Read more bytes to handle different compiler outputs
            auto func_data = dma_->ReadMemory(pid_, *global_scope_func, 128);
            if (!func_data.empty()) {
                // Try multiple patterns for GlobalTypeScope getter:
                // Pattern 1: lea rax, [rip+??] ; ret  (48 8D 05 ?? ?? ?? ?? C3)
                // Pattern 2: mov rax, [rip+??] ; ret  (48 8B 05 ?? ?? ?? ?? C3)
                // Pattern 3: mov rax, cs:g_GlobalScope (48 8B 05 ?? ?? ?? ??)
                // Pattern 4: lea rax, cs:g_GlobalScope (48 8D 05 ?? ?? ?? ??)

                for (size_t i = 0; i + 7 < func_data.size() && !found_global_scope; i++) {
                    // Check for 48 8D 05 (lea rax, [rip+...]) or 48 8B 05 (mov rax, [rip+...])
                    if (func_data[i] == 0x48 &&
                        (func_data[i+1] == 0x8D || func_data[i+1] == 0x8B) &&
                        func_data[i+2] == 0x05) {

                        int32_t rel_offset = *reinterpret_cast<int32_t*>(&func_data[i+3]);
                        uint64_t target = *global_scope_func + i + 7 + rel_offset;

                        // If it's a mov (8B), we need to dereference
                        if (func_data[i+1] == 0x8B) {
                            auto deref = dma_->Read<uint64_t>(pid_, target);
                            if (deref && *deref != 0) {
                                target = *deref;
                            } else {
                                continue; // Try next pattern
                            }
                        }

                        // Validate the target looks like a TypeScope
                        if (target >= 0x10000 && target <= 0x7FFFFFFFFFFF) {
                            // Try to read the scope name to verify
                            std::string test_name = ReadString(target + 0x08, 64);
                            if (!test_name.empty() && test_name.find('\0') == std::string::npos) {
                                global_scope_ = target;
                                SchemaScope scope;
                                scope.name = "!GlobalTypes";  // Match anger's naming
                                scope.address = global_scope_;
                                scopes_.push_back(scope);
                                found_global_scope = true;
                                LOG_INFO("Found GlobalTypeScope at 0x{:X} (name: {})", global_scope_, test_name);
                            }
                        }
                    }
                }
            }
        }
    }

    if (!found_global_scope) {
        LOG_WARN("Could not find GlobalTypeScope via vfunc parsing");
    }

    // Step 2: Read ALL scopes from CSchemaSystem structure (like anger/Andromeda)
    // CSchemaSystem+0x190 = scope count (uint16_t)
    // CSchemaSystem+0x198 = scope array pointer (void**)
    auto scope_count_opt = dma_->Read<uint16_t>(pid_, schema_system_ + SCHEMA_SYSTEM_SCOPE_COUNT);
    auto scope_array_ptr_opt = dma_->Read<uint64_t>(pid_, schema_system_ + SCHEMA_SYSTEM_SCOPE_ARRAY);

    uint16_t scope_count = scope_count_opt ? *scope_count_opt : 0;
    uint64_t scope_array_ptr = scope_array_ptr_opt ? *scope_array_ptr_opt : 0;

    LOG_INFO("CSchemaSystem structure: scope_count={}, scope_array=0x{:X}", scope_count, scope_array_ptr);

    // Add all scopes from the array WITHOUT deduplication (matches Andromeda behavior)
    // Even if GlobalTypeScope is in the array, we process it again - later overwrites earlier
    if (scope_array_ptr != 0 && scope_count > 0 && scope_count < 100) {
        int valid_scopes = 0;
        int failed_reads = 0;

        for (uint16_t i = 0; i < scope_count; i++) {
            auto scope_ptr_opt = dma_->Read<uint64_t>(pid_, scope_array_ptr + i * 8);
            if (!scope_ptr_opt) {
                failed_reads++;
                LOG_WARN("Failed to read scope pointer at index {}", i);
                continue;
            }

            if (*scope_ptr_opt == 0) {
                continue; // Null entry, skip
            }

            uint64_t scope_addr = *scope_ptr_opt;

            // Validate pointer range
            if (scope_addr < 0x10000 || scope_addr > 0x7FFFFFFFFFFF) {
                LOG_WARN("Invalid scope pointer at index {}: 0x{:X}", i, scope_addr);
                continue;
            }

            // Read scope name at TypeScope+0x08 (embedded char array, not a pointer)
            std::string scope_name = ReadString(scope_addr + 0x08, 256);

            if (scope_name.empty()) {
                // Try harder - read raw bytes and look for printable chars
                auto name_data = dma_->ReadMemory(pid_, scope_addr + 0x08, 64);
                if (!name_data.empty()) {
                    size_t len = 0;
                    while (len < name_data.size() && name_data[len] >= 0x20 && name_data[len] < 0x7F) {
                        len++;
                    }
                    if (len > 0) {
                        scope_name = std::string(reinterpret_cast<char*>(name_data.data()), len);
                    }
                }

                if (scope_name.empty()) {
                    scope_name = "Scope_" + std::to_string(i);
                }
            }

            SchemaScope scope;
            scope.name = scope_name;
            scope.address = scope_addr;
            scopes_.push_back(scope);
            valid_scopes++;

            LOG_INFO("Scope[{}]: {} at 0x{:X}", i, scope_name, scope_addr);
        }

        all_scopes_ptr_ = scope_array_ptr;
        all_scopes_count_ = scope_count;

        LOG_INFO("Added {} valid scopes from structure ({} failed reads)", valid_scopes, failed_reads);
    }

    // Step 3: Fallback - pattern-based discovery if structure method failed
    if (scopes_.size() <= 1) {
        LOG_WARN("Structure-based scope discovery found {} scopes, trying pattern fallback", scopes_.size());

        auto mod_opt = dma_->GetModuleByName(pid_, "schemasystem.dll");
        if (mod_opt) {
            const size_t scan_size = std::min((size_t)mod_opt->size, (size_t)(16 * 1024 * 1024));
            auto module_data = dma_->ReadMemory(pid_, schemasystem_base_, scan_size);

            if (!module_data.empty()) {
                // Pattern from anger: 48 8B 05 ?? ?? ?? ?? 48 8B D6 0F B7 CB 48 8B 3C C8
                // This is in GetAllTypeScope and references the global scope array
                auto pattern = orpheus::analysis::PatternScanner::Compile(
                    "48 8B 05 ?? ?? ?? ?? 48 8B D6 0F B7 CB 48 8B 3C C8");

                if (pattern) {
                    auto results = orpheus::analysis::PatternScanner::Scan(module_data, *pattern, schemasystem_base_, 10);
                    LOG_INFO("Pattern scan found {} matches", results.size());

                    for (uint64_t match : results) {
                        auto offset_data = dma_->ReadMemory(pid_, match + 3, 4);
                        if (offset_data.size() < 4) continue;

                        int32_t rel_offset = *reinterpret_cast<int32_t*>(offset_data.data());
                        uint64_t arr_ptr_addr = match + 7 + rel_offset;

                        auto arr_ptr = dma_->Read<uint64_t>(pid_, arr_ptr_addr);
                        if (!arr_ptr || *arr_ptr == 0) continue;

                        // Scope count is 8 bytes before the array global (like anger does)
                        auto count_opt = dma_->Read<uint16_t>(pid_, arr_ptr_addr - 8);
                        int max_scopes = (count_opt && *count_opt > 0 && *count_opt < 100) ? *count_opt : 32;

                        LOG_INFO("Pattern fallback: array at 0x{:X}, count={}", *arr_ptr, max_scopes);

                        for (int i = 0; i < max_scopes; i++) {
                            auto scope_ptr = dma_->Read<uint64_t>(pid_, *arr_ptr + i * 8);
                            if (!scope_ptr || *scope_ptr == 0) break;

                            if (*scope_ptr < 0x10000 || *scope_ptr > 0x7FFFFFFFFFFF) continue;

                            std::string scope_name = ReadString(*scope_ptr + 0x08);
                            if (scope_name.empty()) scope_name = "Scope_" + std::to_string(i);

                            SchemaScope scope;
                            scope.name = scope_name;
                            scope.address = *scope_ptr;
                            scopes_.push_back(scope);
                        }

                        if (scopes_.size() > 1) {
                            all_scopes_ptr_ = *arr_ptr;
                            LOG_INFO("Pattern fallback successful: {} scopes found", scopes_.size());
                            break;
                        }
                    }
                }
            }
        }
    }

    LOG_INFO("EnumerateScopes complete: {} total scopes found", scopes_.size());
    return !scopes_.empty();
}

std::vector<SchemaClass> CS2SchemaDumper::DumpScope(uint64_t scope_addr,
    std::function<void(int, int)> progress) {

    std::vector<SchemaClass> classes;

    if (scope_addr == 0) {
        LOG_WARN("DumpScope called with null address");
        return classes;
    }

    // Read scope name for logging
    std::string scope_name = ReadString(scope_addr + 0x08, 64);

    // ClassContainer (bucket array) is directly at TypeScope + 0x5C0 (Andromeda approach)
    uint64_t class_container_addr = scope_addr + CLASS_CONTAINER_OFFSET;

    // NumSchema is at ClassContainer - 0x74 (total class count)
    auto num_schema_opt = dma_->Read<int32_t>(pid_, class_container_addr - NUM_SCHEMA_OFFSET);
    int num_schema = num_schema_opt ? *num_schema_opt : 0;

    // Sanity check: if numSchema is garbage (negative or > 100k), use unlimited like anger does
    int max_bindings = 100000; // Safety cap
    if (num_schema > 0 && num_schema < 100000) {
        max_bindings = num_schema;
    } else if (num_schema != 0) {
        LOG_WARN("TypeScope {} has suspicious numSchema={}, using unlimited", scope_name, num_schema);
    }

    LOG_INFO("DumpScope: {} at 0x{:X}, ClassContainer=0x{:X}, numSchema={}",
             scope_name, scope_addr, class_container_addr, num_schema);

    // Collect bindings from bucket iteration (Andromeda approach)
    // CSchemaList<T>::BlockContainer structure (24 bytes each):
    //   +0x00: void* unkn[2] (16 bytes) - lock/first_uncommitted
    //   +0x10: SchemaBlock* m_firstBlock
    //
    // SchemaBlock structure:
    //   +0x00: void* unkn0
    //   +0x08: SchemaBlock* m_nextBlock
    //   +0x10: CSchemaClassBinding* m_classBinding

    std::vector<uint64_t> bindings;
    bindings.reserve(std::min(max_bindings, 5000)); // Reserve reasonable amount

    int non_empty_buckets = 0;
    int total_blocks = 0;
    int failed_reads = 0;

    // Iterate 256 buckets directly at class_container_addr
    for (int bucket = 0; bucket < SCHEMA_BUCKET_COUNT && total_blocks < max_bindings; bucket++) {
        uint64_t bucket_addr = class_container_addr + bucket * BLOCK_CONTAINER_SIZE;

        // GetFirstBlock() returns m_firstBlock at +0x10
        auto first_block_opt = dma_->Read<uint64_t>(pid_, bucket_addr + BLOCK_CONTAINER_FIRST_BLOCK);
        if (!first_block_opt) {
            failed_reads++;
            continue;
        }

        if (*first_block_opt == 0) continue;

        uint64_t block = *first_block_opt;
        bool has_data = false;
        int blocks_in_bucket = 0;

        // Walk the linked list: block -> next -> next -> ...
        while (block != 0 && total_blocks < max_bindings) {
            // Validate block pointer
            if (block < 0x10000 || block > 0x7FFFFFFFFFFF) {
                LOG_WARN("Invalid block pointer 0x{:X} in bucket {}", block, bucket);
                break;
            }

            has_data = true;
            total_blocks++;
            blocks_in_bucket++;

            // SchemaBlock: +0x10 = binding, +0x08 = next
            auto binding_ptr_opt = dma_->Read<uint64_t>(pid_, block + SCHEMA_BLOCK_BINDING);
            auto next_block_opt = dma_->Read<uint64_t>(pid_, block + SCHEMA_BLOCK_NEXT);

            if (binding_ptr_opt && *binding_ptr_opt != 0) {
                uint64_t binding = *binding_ptr_opt;
                // Validate binding pointer
                if (binding >= 0x10000 && binding <= 0x7FFFFFFFFFFF) {
                    bindings.push_back(binding);
                }
            }

            block = next_block_opt ? *next_block_opt : 0;

            // Prevent infinite loops
            if (blocks_in_bucket > 1000) {
                LOG_WARN("Bucket {} has >1000 blocks, breaking", bucket);
                break;
            }
        }

        if (has_data) non_empty_buckets++;
    }

    LOG_INFO("TypeScope {}: {} bindings, {} blocks, {} non-empty buckets, {} failed reads",
             scope_name, bindings.size(), total_blocks, non_empty_buckets, failed_reads);

    int total_count = static_cast<int>(bindings.size());
    int processed = 0;
    int valid_classes = 0;

    // Process all bindings
    for (uint64_t binding_addr : bindings) {
        SchemaClass cls;
        if (ReadClassBinding(binding_addr, cls)) {
            classes.push_back(std::move(cls));
            valid_classes++;
        }

        processed++;
        if (progress && (processed % 100 == 0 || processed == total_count)) {
            progress(processed, total_count);
        }
    }

    LOG_INFO("Processed {} valid classes from scope {} (0x{:X})", valid_classes, scope_name, scope_addr);
    return classes;
}

bool CS2SchemaDumper::ReadClassBinding(uint64_t binding_addr, SchemaClass& out_class) {
    if (binding_addr == 0) return false;

    // CSchemaClassBinding structure (from anger/Andromeda):
    // +0x08: const char* m_pszName
    // +0x10: const char* m_pszDLLName
    // +0x18: int32 m_nSizeOf
    // +0x1C: uint16 m_nFieldCount
    // +0x28: SchemaClassFieldData_t* m_pFields
    // +0x30: CSchemaClassInfo* m_pBaseClass

    // Read class name pointer
    auto name_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_NAME_OFFSET);
    if (!name_ptr || *name_ptr == 0) return false;

    // Validate name pointer range
    if (*name_ptr < 0x10000 || *name_ptr > 0x7FFFFFFFFFFF) return false;

    out_class.name = ReadString(*name_ptr);
    if (out_class.name.empty()) return false;

    // Skip invalid class names (junk data)
    if (out_class.name[0] < 0x20 || out_class.name[0] > 0x7E) return false;

    // Read DLL name (optional)
    auto dll_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_DLL_OFFSET);
    if (dll_ptr && *dll_ptr != 0 && *dll_ptr >= 0x10000 && *dll_ptr <= 0x7FFFFFFFFFFF) {
        out_class.module = ReadString(*dll_ptr);
    }

    // Read class size
    auto size = dma_->Read<int32_t>(pid_, binding_addr + BINDING_SIZE_OFFSET);
    if (size && *size >= 0 && *size < 0x100000) {  // Sanity check: < 1MB
        out_class.size = static_cast<uint32_t>(*size);
    }

    // Read field count
    auto field_count = dma_->Read<uint16_t>(pid_, binding_addr + BINDING_FIELD_COUNT_OFFSET);
    uint16_t num_fields = field_count ? *field_count : 0;

    // Process even classes with 0 fields (like Andromeda does) but only if > 0
    if (num_fields > 0 && num_fields < 2000) {  // Sanity check: < 2000 fields per class
        // Read field array pointer
        auto field_array_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_FIELD_ARRAY_OFFSET);
        if (field_array_ptr && *field_array_ptr != 0 &&
            *field_array_ptr >= 0x10000 && *field_array_ptr <= 0x7FFFFFFFFFFF) {
            ReadFieldArray(*field_array_ptr, num_fields, out_class.fields);
        }
    }

    // Read base class (optional)
    auto base_class_ptr = dma_->Read<uint64_t>(pid_, binding_addr + BINDING_BASE_CLASS_OFFSET);
    if (base_class_ptr && *base_class_ptr != 0 &&
        *base_class_ptr >= 0x10000 && *base_class_ptr <= 0x7FFFFFFFFFFF) {
        // Base class info structure has the binding pointer at offset 0x8
        auto base_binding = dma_->Read<uint64_t>(pid_, *base_class_ptr + 0x8);
        if (base_binding && *base_binding != 0 &&
            *base_binding >= 0x10000 && *base_binding <= 0x7FFFFFFFFFFF) {
            auto base_name_ptr = dma_->Read<uint64_t>(pid_, *base_binding + BINDING_NAME_OFFSET);
            if (base_name_ptr && *base_name_ptr != 0 &&
                *base_name_ptr >= 0x10000 && *base_name_ptr <= 0x7FFFFFFFFFFF) {
                out_class.base_class = ReadString(*base_name_ptr);
            }
        }
    }

    return true;
}

bool CS2SchemaDumper::ReadFieldArray(uint64_t array_addr, uint16_t count,
    std::vector<SchemaField>& out_fields) {

    if (array_addr == 0 || count == 0) return false;

    out_fields.reserve(count);

    // SchemaClassFieldData_t structure (0x20 bytes each - matches anger):
    // 0x00: const char* FieldName
    // 0x08: CSchemaType* FieldType
    // 0x10: int32 FieldOffset
    // 0x14: int32 metadata_size (optional)
    // 0x18: void* metadata (optional)

    for (uint16_t i = 0; i < count; i++) {
        uint64_t field_addr = array_addr + i * FIELD_ENTRY_SIZE;

        // Read field name pointer
        auto name_ptr = dma_->Read<uint64_t>(pid_, field_addr);
        if (!name_ptr || *name_ptr == 0) continue;

        // Validate pointer range
        if (*name_ptr < 0x10000 || *name_ptr > 0x7FFFFFFFFFFF) continue;

        SchemaField field;
        field.name = ReadString(*name_ptr);
        if (field.name.empty()) continue;

        // Read field offset (this is what we really need)
        auto offset = dma_->Read<int32_t>(pid_, field_addr + 0x10);
        if (offset) {
            field.offset = static_cast<uint32_t>(*offset);
        }

        // Read type name (optional - CSchemaType has name at offset 0x8)
        auto type_ptr = dma_->Read<uint64_t>(pid_, field_addr + 0x8);
        if (type_ptr && *type_ptr != 0 && *type_ptr >= 0x10000 && *type_ptr <= 0x7FFFFFFFFFFF) {
            auto type_name_ptr = dma_->Read<uint64_t>(pid_, *type_ptr + 0x8);
            if (type_name_ptr && *type_name_ptr != 0 &&
                *type_name_ptr >= 0x10000 && *type_name_ptr <= 0x7FFFFFFFFFFF) {
                field.type_name = ReadString(*type_name_ptr);
            }
        }

        out_fields.push_back(std::move(field));
    }

    return !out_fields.empty();
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

} // namespace orpheus::dumper
