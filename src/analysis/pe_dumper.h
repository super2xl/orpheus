#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <functional>
#include <map>

namespace orpheus::analysis {

// Forward declare to avoid Windows.h dependency
#pragma pack(push, 1)
struct PE_DOS_HEADER {
    uint16_t e_magic;      // MZ
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;     // Offset to PE header
};

struct PE_FILE_HEADER {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct PE_DATA_DIRECTORY {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct PE_OPTIONAL_HEADER64 {
    uint16_t Magic;                    // 0x20B for PE32+
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    PE_DATA_DIRECTORY DataDirectory[16];
};

struct PE_OPTIONAL_HEADER32 {
    uint16_t Magic;                    // 0x10B for PE32
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    PE_DATA_DIRECTORY DataDirectory[16];
};

struct PE_SECTION_HEADER {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct PE_IMPORT_DESCRIPTOR {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
};

struct PE_EXPORT_DIRECTORY {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Name;
    uint32_t Base;
    uint32_t NumberOfFunctions;
    uint32_t NumberOfNames;
    uint32_t AddressOfFunctions;
    uint32_t AddressOfNames;
    uint32_t AddressOfNameOrdinals;
};
#pragma pack(pop)

/**
 * ImportEntry - Single imported function
 */
struct ImportEntry {
    std::string name;
    uint16_t ordinal;
    uint64_t thunk_rva;
    uint64_t resolved_address;  // In memory
    bool by_ordinal;
};

/**
 * ImportModule - Module with its imports
 */
struct ImportModule {
    std::string name;
    std::vector<ImportEntry> functions;
};

/**
 * ExportEntry - Single exported function
 */
struct ExportEntry {
    std::string name;
    uint16_t ordinal;
    uint32_t rva;
    uint64_t address;  // Base + RVA
    bool is_forwarder;
    std::string forwarder_name;
};

/**
 * SectionInfo - Section information for dumping
 */
struct SectionInfo {
    std::string name;
    uint32_t virtual_address;
    uint32_t virtual_size;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t characteristics;
};

/**
 * DumpOptions - Configuration for PE dumping
 */
struct DumpOptions {
    bool fix_headers = true;         // Fix PE headers for static analysis
    bool rebuild_iat = true;         // Rebuild Import Address Table
    bool fix_checksum = false;       // Recalculate PE checksum
    bool remove_relocations = false; // Clear relocation data
    bool unmap_sections = true;      // Convert from memory layout to file layout
    uint32_t file_alignment = 0x200; // File alignment for dumped PE
};

/**
 * PEDumper - Dump PE files from memory with reconstruction
 *
 * Features:
 * - Read PE headers and sections from memory
 * - Rebuild IAT for static analysis tools
 * - Fix section alignments
 * - Handle both PE32 and PE32+ (64-bit)
 */
class PEDumper {
public:
    using ReadMemoryFunc = std::function<std::vector<uint8_t>(uint64_t address, size_t size)>;

    /**
     * Create a dumper with a memory read callback
     * @param read_func Function to read memory (pid is captured in closure)
     */
    explicit PEDumper(ReadMemoryFunc read_func);

    /**
     * Dump a module from memory
     * @param base_address Module base address
     * @param options Dump options
     * @return Dumped PE file bytes, or empty on failure
     */
    std::vector<uint8_t> Dump(uint64_t base_address, const DumpOptions& options = {});

    /**
     * Parse PE headers only (without full dump)
     * @param base_address Module base address
     * @return true if valid PE
     */
    bool ParseHeaders(uint64_t base_address);

    /**
     * Get imports from a loaded module
     */
    std::vector<ImportModule> GetImports(uint64_t base_address);

    /**
     * Get exports from a loaded module
     */
    std::vector<ExportEntry> GetExports(uint64_t base_address);

    /**
     * Get section information
     */
    std::vector<SectionInfo> GetSections(uint64_t base_address);

    /**
     * Check if PE is 64-bit
     */
    bool Is64Bit() const { return is_64bit_; }

    /**
     * Get size of image
     */
    uint32_t GetImageSize() const { return image_size_; }

    /**
     * Get entry point RVA
     */
    uint32_t GetEntryPoint() const { return entry_point_; }

    /**
     * Get last error
     */
    const std::string& GetLastError() const { return last_error_; }

private:
    template<typename T>
    std::optional<T> ReadStruct(uint64_t address);

    std::string ReadNullString(uint64_t address, size_t max_len = 256);

    uint32_t AlignUp(uint32_t value, uint32_t alignment);
    uint32_t RvaToOffset(uint32_t rva, const std::vector<PE_SECTION_HEADER>& sections);

    ReadMemoryFunc read_memory_;
    std::string last_error_;

    // Cached header info
    bool is_64bit_ = false;
    uint32_t image_size_ = 0;
    uint32_t entry_point_ = 0;
    uint32_t section_alignment_ = 0;
    uint32_t file_alignment_ = 0;
    uint16_t num_sections_ = 0;
    PE_DATA_DIRECTORY data_directories_[16] = {};
};

} // namespace orpheus::analysis
