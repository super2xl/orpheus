#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <functional>
#include <map>

namespace orpheus::analysis {

/**
 * MSVC x64 RTTI Structures
 *
 * In x64, RTTI uses RVA offsets from module base instead of absolute pointers.
 * The Complete Object Locator is found at vtable[-1].
 */

#pragma pack(push, 1)

/**
 * RTTICompleteObjectLocator (COL) - x64 version
 * Found at vtable[-1], points to type info and class hierarchy
 */
struct RTTICompleteObjectLocator {
    uint32_t signature;           // 0x00: Always 1 for x64
    uint32_t offset;              // 0x04: Offset of vftable within complete class
    uint32_t cd_offset;           // 0x08: Constructor displacement offset
    int32_t  type_descriptor_rva; // 0x0C: RVA to RTTITypeDescriptor
    int32_t  class_hierarchy_rva; // 0x10: RVA to RTTIClassHierarchyDescriptor
    int32_t  self_rva;            // 0x14: RVA to this object (for base calculation)
};
static_assert(sizeof(RTTICompleteObjectLocator) == 0x18, "COL size mismatch");

/**
 * RTTITypeDescriptor - Contains mangled class name
 * Variable size due to name string
 */
struct RTTITypeDescriptor {
    uint64_t vtable_ptr;          // 0x00: Pointer to type_info vftable
    uint64_t internal_ptr;        // 0x08: Internal runtime reference (usually NULL)
    char     mangled_name[1];     // 0x10: Null-terminated mangled name (variable length)
};

/**
 * RTTIClassHierarchyDescriptor - Describes inheritance hierarchy
 */
struct RTTIClassHierarchyDescriptor {
    uint32_t signature;           // 0x00: Always 0
    uint32_t attributes;          // 0x04: Bit flags (see CHD_* constants)
    uint32_t num_base_classes;    // 0x08: Number of base classes + 1 (includes self)
    int32_t  base_class_array_rva;// 0x0C: RVA to array of RTTIBaseClassDescriptor RVAs
};
static_assert(sizeof(RTTIClassHierarchyDescriptor) == 0x10, "CHD size mismatch");

// Class Hierarchy Descriptor attributes
constexpr uint32_t CHD_MULTINH   = 0x01;  // Multiple inheritance
constexpr uint32_t CHD_VIRTINH   = 0x02;  // Virtual inheritance
constexpr uint32_t CHD_AMBIGUOUS = 0x04;  // Ambiguous inheritance

/**
 * RTTIBaseClassDescriptor - Describes a single base class
 */
struct RTTIBaseClassDescriptor {
    int32_t  type_descriptor_rva; // 0x00: RVA to RTTITypeDescriptor
    uint32_t num_contained_bases; // 0x04: Number of nested base classes
    int32_t  member_displacement; // 0x08: mdisp - member displacement
    int32_t  vbtable_displacement;// 0x0C: pdisp - vbtable displacement (-1 if not virtual)
    uint32_t vbtable_offset;      // 0x10: vdisp - offset within vbtable
    uint32_t attributes;          // 0x14: Bit flags (see BCD_* constants)
    int32_t  class_hierarchy_rva; // 0x18: RVA to RTTIClassHierarchyDescriptor
};
static_assert(sizeof(RTTIBaseClassDescriptor) == 0x1C, "BCD size mismatch");

// Base Class Descriptor attributes
constexpr uint32_t BCD_NOTVISIBLE  = 0x01;  // Not publicly visible
constexpr uint32_t BCD_AMBIGUOUS   = 0x02;  // Ambiguous base
constexpr uint32_t BCD_PRIVORPROTBASE = 0x04; // Private or protected base
constexpr uint32_t BCD_PRIVORPROTINCOMPOBJ = 0x08;
constexpr uint32_t BCD_VBOFCONTOBJ = 0x10;  // Virtual base of containing object
constexpr uint32_t BCD_NONPOLYMORPHIC = 0x20; // Non-polymorphic base
constexpr uint32_t BCD_HASPCHD = 0x40;      // Has class hierarchy descriptor

#pragma pack(pop)

/**
 * PE Section information for targeted scanning
 */
struct PESection {
    std::string name;             // Section name (e.g., ".rdata", ".data")
    uint64_t virtual_address;     // VA of section start
    uint32_t virtual_size;        // Size of section in memory
    uint32_t characteristics;     // Section flags

    bool IsReadable() const { return (characteristics & 0x40000000) != 0; }
    bool IsWritable() const { return (characteristics & 0x80000000) != 0; }
    bool IsExecutable() const { return (characteristics & 0x20000000) != 0; }
    bool IsInitializedData() const { return (characteristics & 0x00000040) != 0; }
};

/**
 * Parsed RTTI information for a class
 */
struct RTTIClassInfo {
    uint64_t vtable_address;      // Address of the vtable
    uint64_t col_address;         // Address of Complete Object Locator
    std::string mangled_name;     // Raw mangled name (e.g., ".?AVMyClass@@")
    std::string demangled_name;   // Demangled name (e.g., "class MyClass")
    uint32_t vftable_offset;      // Offset of vtable in complete class
    bool has_virtual_base;        // True if virtual inheritance (V flag)
    bool is_multiple_inheritance; // True if multiple inheritance (M flag)
    uint32_t method_count;        // Number of virtual methods in vtable
    std::vector<std::string> base_classes; // List of base class names

    // ClassInformer-style flags string (e.g., "M", "V", "MV", or "")
    std::string GetFlags() const {
        std::string flags;
        if (is_multiple_inheritance) flags += "M";
        if (has_virtual_base) flags += "V";
        return flags;
    }

    // ClassInformer-style hierarchy string (e.g., "MyClass: BaseA, BaseB")
    std::string GetHierarchyString() const {
        std::string result = demangled_name;
        // Strip "class " or "struct " prefix for cleaner output
        if (result.substr(0, 6) == "class ") result = result.substr(6);
        else if (result.substr(0, 7) == "struct ") result = result.substr(7);

        if (!base_classes.empty()) {
            result += ": ";
            for (size_t i = 0; i < base_classes.size(); i++) {
                if (i > 0) result += ", ";
                std::string base = base_classes[i];
                // Strip prefix from base classes too
                if (base.substr(0, 6) == "class ") base = base.substr(6);
                else if (base.substr(0, 7) == "struct ") base = base.substr(7);
                result += base;
            }
        }
        return result;
    }
};

/**
 * Parsed vtable entry
 */
struct VTableEntry {
    uint64_t address;             // Address of the function
    int32_t index;                // Index in vtable (0-based)
    std::string function_name;    // Demangled function name (if available)
};

/**
 * Complete vtable information
 */
struct VTableInfo {
    uint64_t address;             // Vtable address
    RTTIClassInfo class_info;     // Associated class info
    std::vector<VTableEntry> entries; // Function pointers
    size_t size;                  // Size in bytes
};

/**
 * RTTIParser - Parse MSVC RTTI from process memory
 *
 * Usage:
 *   RTTIParser parser(read_memory_func, module_base);
 *   auto info = parser.ParseVTable(vtable_address);
 *   if (info) {
 *       std::cout << "Class: " << info->class_info.demangled_name << std::endl;
 *   }
 */
class RTTIParser {
public:
    using ReadMemoryFunc = std::function<std::vector<uint8_t>(uint64_t address, size_t size)>;

    /**
     * Create parser with memory read function and module base
     * @param read_func Function to read process memory
     * @param module_base Base address of the module containing RTTI
     */
    RTTIParser(ReadMemoryFunc read_func, uint64_t module_base);

    /**
     * Parse RTTI from a vtable address
     * @param vtable_address Address of the vtable (first function pointer)
     * @return Parsed class info, or nullopt if not valid RTTI
     */
    std::optional<RTTIClassInfo> ParseVTable(uint64_t vtable_address);

    /**
     * Parse Complete Object Locator
     * @param col_address Address of the COL
     * @return Parsed class info, or nullopt if invalid
     */
    std::optional<RTTIClassInfo> ParseCOL(uint64_t col_address);

    /**
     * Get the mangled name from a Type Descriptor
     * @param type_desc_rva RVA to the Type Descriptor
     * @return Mangled name string, or empty if invalid
     */
    std::string GetMangledName(int32_t type_desc_rva);

    /**
     * Scan memory region for vtables with valid RTTI
     * @param start Start address of region
     * @param size Size of region in bytes
     * @param callback Called for each discovered vtable
     * @return Number of vtables found
     */
    size_t ScanForVTables(uint64_t start, size_t size,
                          std::function<void(const RTTIClassInfo&)> callback = nullptr);

    /**
     * Get PE sections from a module
     * @param module_base Base address of the module
     * @return Vector of PE sections, empty if parsing failed
     */
    std::vector<PESection> GetPESections(uint64_t module_base);

    /**
     * Scan an entire module for RTTI by automatically finding and scanning
     * the appropriate sections (.rdata, .data)
     * @param module_base Base address of the module
     * @param callback Called for each discovered vtable
     * @return Number of vtables found
     */
    size_t ScanModule(uint64_t module_base,
                      std::function<void(const RTTIClassInfo&)> callback = nullptr);

    /**
     * Demangle an MSVC mangled name
     * @param mangled Mangled name (e.g., ".?AVMyClass@@")
     * @return Demangled name (e.g., "class MyClass")
     */
    static std::string Demangle(const std::string& mangled);

    /**
     * Demangle RTTI type descriptor names specifically
     * @param mangled Mangled RTTI name (e.g., ".?AVMyClass@@")
     * @return Demangled name (e.g., "class MyClass")
     */
    static std::string DemangleRTTI(const std::string& mangled);

    /**
     * Get base class list from Class Hierarchy Descriptor
     * @param chd_rva RVA to the Class Hierarchy Descriptor
     * @return List of base class names
     */
    std::vector<std::string> GetBaseClasses(int32_t chd_rva);

    /**
     * Parse full vtable with function entries
     * @param vtable_address Address of the vtable
     * @param max_entries Maximum entries to parse (0 = auto-detect)
     * @return VTable info with entries, or nullopt if invalid
     */
    std::optional<VTableInfo> ParseFullVTable(uint64_t vtable_address, size_t max_entries = 100);

    /**
     * Check if an address looks like a valid vtable
     * @param address Address to check
     * @return True if address appears to point to a vtable
     */
    bool IsValidVTable(uint64_t address);

private:
    template<typename T>
    std::optional<T> ReadStruct(uint64_t address);

    uint64_t RVAToVA(int32_t rva) const { return module_base_ + rva; }
    bool IsValidRVA(int32_t rva) const;
    bool IsExecutableAddress(uint64_t address);
    uint32_t CountVTableMethods(uint64_t vtable_address, size_t max_entries = 1024);

    ReadMemoryFunc read_memory_;
    uint64_t module_base_;
    uint64_t module_size_ = 0;  // Set during scanning for validation

    // Cache for parsed type descriptors
    std::map<int32_t, std::string> name_cache_;
};

/**
 * Common RTTI patterns for scanning
 */
namespace rtti_patterns {
    // Pattern for Complete Object Locator signature (x64)
    // signature=1, offset=0, cd_offset=0 (common case)
    inline const char* COL_SIGNATURE = "01 00 00 00 00 00 00 00 00 00 00 00";

    // Pattern for type_info vtable reference (points to ??_7type_info@@6B@)
    // This is less reliable but can help find Type Descriptors
}

} // namespace orpheus::analysis
