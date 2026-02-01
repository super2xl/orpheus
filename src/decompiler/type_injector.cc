// Type Injector Implementation
// Copyright (C) 2025 Orpheus Project
// GPL-3.0 License

#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER

#include "type_injector.hh"
#include "architecture.hh"
#include "type.hh"

#include "utils/logger.h"

#include <algorithm>
#include <regex>

namespace orpheus {

void TypeInjector::SetArchitecture(ghidra::Architecture* arch) {
    architecture_ = arch;
    if (arch) {
        type_factory_ = arch->types;
    } else {
        type_factory_ = nullptr;
    }
}

void TypeInjector::AddSchemaClass(const orpheus::dumper::SchemaClass& class_def) {
    pending_classes_.push_back(class_def);
}

void TypeInjector::AddSchemaClasses(const std::vector<orpheus::dumper::SchemaClass>& classes) {
    pending_classes_.insert(pending_classes_.end(), classes.begin(), classes.end());
}

void TypeInjector::ClearSchemaClasses() {
    pending_classes_.clear();
}

int TypeInjector::InjectTypes() {
    if (!architecture_ || !type_factory_) {
        last_error_ = "Architecture not set";
        return 0;
    }

    injected_count_ = 0;
    last_error_.clear();

    // Sort classes by dependency (parent classes first)
    // Simple approach: multiple passes until all are processed
    std::vector<const orpheus::dumper::SchemaClass*> to_process;
    for (const auto& cls : pending_classes_) {
        to_process.push_back(&cls);
    }

    int max_passes = 10;  // Prevent infinite loops
    int pass = 0;

    while (!to_process.empty() && pass < max_passes) {
        std::vector<const orpheus::dumper::SchemaClass*> deferred;

        for (const auto* class_def : to_process) {
            // Check if parent is already defined (if needed)
            if (!class_def->base_class.empty()) {
                if (!HasType(class_def->base_class)) {
                    // Parent not yet defined, defer
                    deferred.push_back(class_def);
                    continue;
                }
            }

            // Try to create the struct type
            try {
                ghidra::TypeStruct* struct_type = CreateStructType(*class_def);
                if (struct_type) {
                    injected_types_[class_def->name] = struct_type;
                    injected_count_++;
                }
            } catch (const std::exception& e) {
                // Log but continue with other types
                LOG_WARN("[TypeInjector] Failed to create type {}: {}", class_def->name, e.what());
            }
        }

        to_process = std::move(deferred);
        pass++;
    }

    if (!to_process.empty()) {
        last_error_ = "Could not resolve all type dependencies after "
                     + std::to_string(max_passes) + " passes";
    }

    return injected_count_;
}

bool TypeInjector::HasType(const std::string& name) const {
    // Check our injected types first
    if (injected_types_.find(name) != injected_types_.end()) {
        return true;
    }

    // Check the type factory
    if (type_factory_) {
        return type_factory_->findByName(name) != nullptr;
    }

    return false;
}

ghidra::Datatype* TypeInjector::GetType(const std::string& class_name) const {
    auto it = injected_types_.find(class_name);
    if (it != injected_types_.end()) {
        return it->second;
    }

    if (type_factory_) {
        return type_factory_->findByName(class_name);
    }

    return nullptr;
}

ghidra::Datatype* TypeInjector::GetPointerType(const std::string& class_name) {
    // First get the struct type
    ghidra::Datatype* struct_type = GetType(class_name);
    if (!struct_type || !type_factory_) {
        return nullptr;
    }

    // Get the default pointer space (usually RAM for x64)
    ghidra::AddrSpace* space = architecture_->getDefaultCodeSpace();

    // Get or create pointer type to the struct
    // The pointer size is determined by the address space (8 bytes for x64)
    return type_factory_->getTypePointer(8, struct_type, space->getWordSize());
}

void TypeInjector::ParseTypeString(const std::string& type_str,
                                   std::string& base_type,
                                   bool& is_pointer,
                                   bool& is_array,
                                   uint32_t& array_count) {
    base_type = type_str;
    is_pointer = false;
    is_array = false;
    array_count = 0;

    // Check for pointer suffix
    if (!type_str.empty() && type_str.back() == '*') {
        is_pointer = true;
        base_type = type_str.substr(0, type_str.length() - 1);
    }

    // Check for array suffix [N]
    std::regex array_regex(R"((.+)\[(\d+)\]$)");
    std::smatch match;
    if (std::regex_match(base_type, match, array_regex)) {
        base_type = match[1].str();
        is_array = true;
        array_count = static_cast<uint32_t>(std::stoul(match[2].str()));
    }

    // Trim whitespace
    while (!base_type.empty() && base_type.back() == ' ') {
        base_type.pop_back();
    }
    while (!base_type.empty() && base_type.front() == ' ') {
        base_type.erase(base_type.begin());
    }
}

ghidra::Datatype* TypeInjector::ResolveType(const std::string& type_name, uint32_t size) {
    if (!type_factory_) {
        return nullptr;
    }

    std::string base_type;
    bool is_pointer, is_array;
    uint32_t array_count;
    ParseTypeString(type_name, base_type, is_pointer, is_array, array_count);

    ghidra::Datatype* resolved = nullptr;

    // Handle common CS2/Source 2 primitive types
    // Integer types
    if (base_type == "int8" || base_type == "int8_t" || base_type == "char") {
        resolved = type_factory_->getBase(1, ghidra::TYPE_INT);
    }
    else if (base_type == "uint8" || base_type == "uint8_t" || base_type == "byte" || base_type == "unsigned char") {
        resolved = type_factory_->getBase(1, ghidra::TYPE_UINT);
    }
    else if (base_type == "int16" || base_type == "int16_t" || base_type == "short") {
        resolved = type_factory_->getBase(2, ghidra::TYPE_INT);
    }
    else if (base_type == "uint16" || base_type == "uint16_t" || base_type == "unsigned short") {
        resolved = type_factory_->getBase(2, ghidra::TYPE_UINT);
    }
    else if (base_type == "int32" || base_type == "int32_t" || base_type == "int") {
        resolved = type_factory_->getBase(4, ghidra::TYPE_INT);
    }
    else if (base_type == "uint32" || base_type == "uint32_t" || base_type == "unsigned int") {
        resolved = type_factory_->getBase(4, ghidra::TYPE_UINT);
    }
    else if (base_type == "int64" || base_type == "int64_t" || base_type == "long long") {
        resolved = type_factory_->getBase(8, ghidra::TYPE_INT);
    }
    else if (base_type == "uint64" || base_type == "uint64_t" || base_type == "unsigned long long") {
        resolved = type_factory_->getBase(8, ghidra::TYPE_UINT);
    }
    // Floating point
    else if (base_type == "float" || base_type == "float32") {
        resolved = type_factory_->getBase(4, ghidra::TYPE_FLOAT);
    }
    else if (base_type == "double" || base_type == "float64") {
        resolved = type_factory_->getBase(8, ghidra::TYPE_FLOAT);
    }
    // Boolean
    else if (base_type == "bool" || base_type == "boolean") {
        resolved = type_factory_->getBase(1, ghidra::TYPE_BOOL);
    }
    // Void
    else if (base_type == "void") {
        resolved = type_factory_->getTypeVoid();
    }
    // CS2-specific types
    else if (base_type == "CUtlString" || base_type == "CUtlSymbolLarge") {
        // These are typically pointer-sized
        resolved = type_factory_->getBase(8, ghidra::TYPE_UINT);
    }
    else if (base_type == "Vector" || base_type == "QAngle") {
        // 3-component float vector (12 bytes)
        // For now, treat as unknown struct - will be defined if in schema
        resolved = type_factory_->findByName(base_type);
        if (!resolved) {
            resolved = type_factory_->getBase(12, ghidra::TYPE_UNKNOWN);
        }
    }
    else if (base_type == "Vector2D") {
        resolved = type_factory_->findByName(base_type);
        if (!resolved) {
            resolved = type_factory_->getBase(8, ghidra::TYPE_UNKNOWN);
        }
    }
    else if (base_type == "Vector4D" || base_type == "Quaternion") {
        resolved = type_factory_->findByName(base_type);
        if (!resolved) {
            resolved = type_factory_->getBase(16, ghidra::TYPE_UNKNOWN);
        }
    }
    else if (base_type == "Color" || base_type == "color32") {
        resolved = type_factory_->getBase(4, ghidra::TYPE_UINT);
    }
    else if (base_type.find("CHandle") != std::string::npos ||
             base_type.find("CEntityHandle") != std::string::npos) {
        // Entity handles are 32-bit unsigned integers
        resolved = type_factory_->getBase(4, ghidra::TYPE_UINT);
    }
    else if (base_type.find("CNetworked") != std::string::npos) {
        // Networked wrappers - try to extract inner type
        std::regex networked_regex(R"(CNetworked(?:Quantized)?<(.+)>)");
        std::smatch match;
        if (std::regex_match(base_type, match, networked_regex)) {
            return ResolveType(match[1].str(), size);
        }
        resolved = type_factory_->getBase(size > 0 ? size : 4, ghidra::TYPE_UNKNOWN);
    }
    else {
        // Try to find as existing type (from schema or built-in)
        resolved = type_factory_->findByName(base_type);
        if (!resolved) {
            // Check our injected types
            auto it = injected_types_.find(base_type);
            if (it != injected_types_.end()) {
                resolved = it->second;
            }
        }
    }

    // Fallback to unknown type with correct size
    if (!resolved) {
        int actual_size = size > 0 ? static_cast<int>(size) : 8;  // Default to pointer size
        resolved = type_factory_->getBase(actual_size, ghidra::TYPE_UNKNOWN);
    }

    // Apply pointer modifier
    if (is_pointer && resolved) {
        int ptr_size = type_factory_->getSizeOfPointer();
        resolved = type_factory_->getTypePointer(ptr_size, resolved, 1);
    }

    // Apply array modifier
    if (is_array && array_count > 0 && resolved) {
        resolved = type_factory_->getTypeArray(static_cast<int>(array_count), resolved);
    }

    return resolved;
}

ghidra::TypeStruct* TypeInjector::CreateStructType(const orpheus::dumper::SchemaClass& class_def) {
    if (!type_factory_) {
        return nullptr;
    }

    // Check if type already exists
    ghidra::Datatype* existing = type_factory_->findByName(class_def.name);
    if (existing && existing->getMetatype() == ghidra::TYPE_STRUCT) {
        // Already exists, return it
        return static_cast<ghidra::TypeStruct*>(existing);
    }

    // Create new struct type
    ghidra::TypeStruct* struct_type = type_factory_->getTypeStruct(class_def.name);

    // Build field list
    std::vector<ghidra::TypeField> fields;
    int field_id = 0;

    for (const auto& schema_field : class_def.fields) {
        // Resolve field type
        ghidra::Datatype* field_type = ResolveType(schema_field.type_name, schema_field.size);

        if (!field_type) {
            // Should not happen due to fallback in ResolveType
            continue;
        }

        // Create TypeField
        ghidra::TypeField tf(
            field_id++,
            static_cast<int>(schema_field.offset),
            schema_field.name,
            field_type
        );
        fields.push_back(tf);
    }

    // Sort fields by offset (should already be sorted, but ensure it)
    std::sort(fields.begin(), fields.end(),
              [](const ghidra::TypeField& a, const ghidra::TypeField& b) {
                  return a.offset < b.offset;
              });

    // Set fields on the struct
    // Flags: 0 = no special flags
    if (!fields.empty()) {
        type_factory_->setFields(fields, struct_type,
                                 static_cast<int>(class_def.size),
                                 8,   // alignment (assume 8-byte for x64)
                                 0);  // flags
    }

    return struct_type;
}

} // namespace orpheus

#endif // ORPHEUS_HAS_GHIDRA_DECOMPILER
