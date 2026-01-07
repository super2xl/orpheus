#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace orpheus { class DMAInterface; }

namespace dumper {

/**
 * Schema field information
 */
struct SchemaField {
    std::string name;
    std::string type_name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

/**
 * Schema class information
 */
struct SchemaClass {
    std::string name;
    std::string module;        // DLL containing class (e.g., "client.dll")
    uint32_t size = 0;         // Class size in bytes
    std::string base_class;    // Parent class name
    std::vector<SchemaField> fields;
};

/**
 * Schema type scope (module namespace)
 */
struct SchemaScope {
    std::string name;
    uint64_t address = 0;
    int class_count = 0;
};

/**
 * CS2 Schema Dumper
 *
 * Dumps class/field offsets from CS2's SchemaSystem interface.
 * Works via DMA by reading game memory structures.
 */
class CS2SchemaDumper {
public:
    CS2SchemaDumper(orpheus::DMAInterface* dma, uint32_t pid);
    ~CS2SchemaDumper() = default;

    /**
     * Initialize by finding SchemaSystem interface
     * @param schemasystem_base Base address of schemasystem.dll
     * @return true if SchemaSystem was found
     */
    bool Initialize(uint64_t schemasystem_base);

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * Get SchemaSystem address
     */
    uint64_t GetSchemaSystemAddress() const { return schema_system_; }

    /**
     * Get all type scopes
     */
    const std::vector<SchemaScope>& GetScopes() const { return scopes_; }

    /**
     * Dump all schemas from a specific scope
     * @param scope_addr Address of the type scope
     * @param progress Optional progress callback (current, total)
     * @return Vector of discovered classes
     */
    std::vector<SchemaClass> DumpScope(uint64_t scope_addr,
        std::function<void(int, int)> progress = nullptr);

    /**
     * Dump all schemas from all scopes
     * @param progress Optional progress callback
     * @return Map of scope name -> classes
     */
    std::unordered_map<std::string, std::vector<SchemaClass>> DumpAll(
        std::function<void(int, int)> progress = nullptr);

    /**
     * Dump all schemas from all scopes with deduplication (like Andromeda)
     * Iterates GlobalTypeScope + all module scopes, deduplicates by class name.
     * Later scopes overwrite earlier ones for duplicate class names.
     * @param progress Optional progress callback
     * @return Vector of unique classes
     */
    std::vector<SchemaClass> DumpAllDeduplicated(
        std::function<void(int, int)> progress = nullptr);

    /**
     * Get offset for a specific class.field
     * @param class_name Class name (e.g., "C_BaseEntity")
     * @param field_name Field name (e.g., "m_iHealth")
     * @return Offset or 0 if not found
     */
    uint32_t GetOffset(const std::string& class_name, const std::string& field_name) const;

    /**
     * Find a class by name
     * @param class_name Class name to search for
     * @return Pointer to class info or nullptr
     */
    const SchemaClass* FindClass(const std::string& class_name) const;

    /**
     * Export to JSON file
     * @param filepath Output file path
     * @return true if successful
     */
    bool ExportToJson(const std::string& filepath) const;

    /**
     * Export to header file (C++ offsets)
     * @param filepath Output file path
     * @return true if successful
     */
    bool ExportToHeader(const std::string& filepath) const;

    /**
     * Get last error message
     */
    const std::string& GetLastError() const { return last_error_; }

    /**
     * Get total class count
     */
    size_t GetTotalClassCount() const;

    /**
     * Get total field count
     */
    size_t GetTotalFieldCount() const;

private:
    // Find SchemaSystem interface via pattern
    bool FindSchemaSystem(uint64_t schemasystem_base);

    // Enumerate all type scopes
    bool EnumerateScopes();

    // Read class binding at address
    bool ReadClassBinding(uint64_t binding_addr, SchemaClass& out_class);

    // Read field data array
    bool ReadFieldArray(uint64_t array_addr, uint16_t count, std::vector<SchemaField>& out_fields);

    // Read null-terminated string from memory
    std::string ReadString(uint64_t addr, size_t max_len = 256);

    orpheus::DMAInterface* dma_;
    uint32_t pid_;

    bool initialized_ = false;
    uint64_t schemasystem_base_ = 0;
    uint64_t schema_system_ = 0;        // CSchemaSystem pointer
    uint64_t global_scope_ = 0;         // Global type scope
    uint64_t all_scopes_ptr_ = 0;       // Pointer to scope array
    uint16_t all_scopes_count_ = 0;     // Number of scopes

    std::vector<SchemaScope> scopes_;
    std::unordered_map<std::string, std::vector<SchemaClass>> cached_schemas_;

    std::string last_error_;

    // Structure offsets (CS2 specific)
    static constexpr uint64_t SCOPE_CLASS_CONTAINER_OFFSET = 0x580;
    static constexpr uint64_t SCHEMA_LIST_BLOCK_CONTAINERS_OFFSET = 0x0;
    static constexpr int64_t SCHEMA_LIST_NUM_SCHEMA_OFFSET = -0x74;  // Negative offset (int64_t)
    static constexpr int SCHEMA_BUCKET_COUNT = 256;

    // CSchemaClassBinding offsets
    static constexpr uint64_t BINDING_NAME_OFFSET = 0x8;
    static constexpr uint64_t BINDING_DLL_OFFSET = 0x10;
    static constexpr uint64_t BINDING_SIZE_OFFSET = 0x18;
    static constexpr uint64_t BINDING_FIELD_COUNT_OFFSET = 0x1C;
    static constexpr uint64_t BINDING_FIELD_ARRAY_OFFSET = 0x28;
    static constexpr uint64_t BINDING_BASE_CLASS_OFFSET = 0x30;

    // SchemaClassFieldDataArray_t size
    static constexpr size_t FIELD_ENTRY_SIZE = 0x20;  // Field name, type, offset, padding
};

} // namespace dumper
