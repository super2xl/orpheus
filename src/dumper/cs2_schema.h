#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace orpheus { class DMAInterface; }

namespace orpheus::dumper {

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

    // Structure offsets (CS2 specific - January 2025 patch, from Andromeda-CS2-Base)
    //
    // CSchemaSystem structure:
    //   +0x190: uint16_t scope_count
    //   +0x198: CSchemaSystemTypeScope** scope_array
    //
    // CSchemaSystemTypeScope structure:
    //   +0x08: char name[256]
    //   +0x5C0: ClassContainer (CSchemaList<CSchemaClassBinding> buckets)
    //
    // CSchemaList structure (Andromeda approach):
    //   -0x74: numSchema (int) - total class count
    //   +0x00: BlockContainers[256] - array of 256 buckets
    //
    // BlockContainer structure (24 bytes):
    //   +0x00: void* unkn[2]
    //   +0x10: SchemaBlock* m_firstBlock
    //
    // SchemaBlock structure:
    //   +0x00: void* unkn0
    //   +0x08: SchemaBlock* m_nextBlock
    //   +0x10: CSchemaClassBinding* m_classBinding

    // CSchemaSystem offsets
    static constexpr uint64_t SCHEMA_SYSTEM_SCOPE_COUNT = 0x190;    // uint16_t at CSchemaSystem+0x190
    static constexpr uint64_t SCHEMA_SYSTEM_SCOPE_ARRAY = 0x198;    // void** at CSchemaSystem+0x198

    static constexpr uint64_t CLASS_CONTAINER_OFFSET = 0x5C0;       // CSchemaList buckets at TypeScope+0x5C0
    static constexpr uint64_t NUM_SCHEMA_OFFSET = 0x74;             // numSchema at ClassContainer-0x74
    static constexpr int SCHEMA_BUCKET_COUNT = 256;
    static constexpr int BLOCK_CONTAINER_SIZE = 24;                 // Each bucket is 24 bytes
    static constexpr uint64_t BLOCK_CONTAINER_FIRST_BLOCK = 0x10;   // firstBlock at bucket+0x10
    static constexpr uint64_t SCHEMA_BLOCK_NEXT = 0x08;             // next at block+0x08
    static constexpr uint64_t SCHEMA_BLOCK_BINDING = 0x10;          // binding at block+0x10

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

} // namespace orpheus::dumper
