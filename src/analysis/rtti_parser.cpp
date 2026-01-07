#include "rtti_parser.h"
#include <algorithm>
#include <cstring>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace orpheus::analysis {

RTTIParser::RTTIParser(ReadMemoryFunc read_func, uint64_t module_base)
    : read_memory_(std::move(read_func))
    , module_base_(module_base) {
}

template<typename T>
std::optional<T> RTTIParser::ReadStruct(uint64_t address) {
    auto data = read_memory_(address, sizeof(T));
    if (data.size() < sizeof(T)) {
        return std::nullopt;
    }

    T result;
    std::memcpy(&result, data.data(), sizeof(T));
    return result;
}

bool RTTIParser::IsValidRVA(int32_t rva) const {
    if (rva <= 0) return false;
    if (module_size_ > 0 && static_cast<uint32_t>(rva) >= module_size_) {
        return false;
    }
    // Basic sanity check - RVA should be reasonable size
    return rva < 0x10000000;  // 256MB max
}

bool RTTIParser::IsExecutableAddress(uint64_t address) {
    // Read a few bytes to see if it looks like code
    auto data = read_memory_(address, 16);
    if (data.empty()) return false;

    // Very basic heuristic - check for common instruction patterns
    // Real implementation would check page protection
    return true;
}

uint32_t RTTIParser::CountVTableMethods(uint64_t vtable_address, size_t max_entries) {
    uint32_t count = 0;

    for (size_t i = 0; i < max_entries; i++) {
        auto entry_data = read_memory_(vtable_address + i * 8, 8);
        if (entry_data.size() < 8) break;

        uint64_t func_addr;
        std::memcpy(&func_addr, entry_data.data(), 8);

        // Stop if we hit a null or invalid pointer
        // Valid x64 function pointers are typically in user-mode range
        if (func_addr == 0 || func_addr < 0x10000 || func_addr > 0x00007FFFFFFFFFFF) {
            break;
        }

        // Verify the target looks like code (not data/padding)
        // Read first 4 bytes at target address for better detection
        auto code_check = read_memory_(func_addr, 4);
        if (code_check.size() < 4) break;

        uint8_t b0 = code_check[0];
        uint8_t b1 = code_check[1];
        uint8_t b2 = code_check[2];
        uint8_t b3 = code_check[3];

        // Stop if target is null bytes, int3 padding, or other non-code patterns
        if ((b0 == 0x00 && b1 == 0x00) ||  // Null bytes
            (b0 == 0xCC && b1 == 0xCC) ||  // int3 padding
            (b0 == 0x90 && b1 == 0x90) ||  // nop padding
            (b0 == 0xFF && b1 == 0xFF)) {  // Invalid
            break;
        }

        // Detect data patterns: small integers look like XX 00 00 00
        // Real x64 code rarely has 3 consecutive null bytes after the first byte
        if (b1 == 0x00 && b2 == 0x00 && b3 == 0x00 && b0 < 0x40) {
            break;
        }

        count++;
    }

    return count;
}

std::optional<RTTIClassInfo> RTTIParser::ParseVTable(uint64_t vtable_address) {
    // Read vtable[-1] to get COL pointer
    auto col_ptr_data = read_memory_(vtable_address - 8, 8);
    if (col_ptr_data.size() < 8) {
        return std::nullopt;
    }

    uint64_t col_address;
    std::memcpy(&col_address, col_ptr_data.data(), 8);

    if (col_address == 0) {
        return std::nullopt;
    }

    auto info = ParseCOL(col_address);
    if (info) {
        info->vtable_address = vtable_address;
        // Count methods in the vtable
        info->method_count = CountVTableMethods(vtable_address);
    }
    return info;
}

std::optional<RTTIClassInfo> RTTIParser::ParseCOL(uint64_t col_address) {
    auto col = ReadStruct<RTTICompleteObjectLocator>(col_address);
    if (!col) {
        return std::nullopt;
    }

    // Validate COL signature (must be 1 for x64)
    if (col->signature != 1) {
        return std::nullopt;
    }

    // Validate RVAs
    if (!IsValidRVA(col->type_descriptor_rva) ||
        !IsValidRVA(col->class_hierarchy_rva)) {
        return std::nullopt;
    }

    // Calculate module base from self_rva if needed
    if (module_base_ == 0 && col->self_rva > 0) {
        module_base_ = col_address - col->self_rva;
    }

    RTTIClassInfo info;
    info.col_address = col_address;
    info.vftable_offset = col->offset;
    info.method_count = 0;  // Will be set by ParseVTable if vtable address is known

    // Get mangled name from Type Descriptor
    info.mangled_name = GetMangledName(col->type_descriptor_rva);
    if (info.mangled_name.empty()) {
        return std::nullopt;
    }

    // Demangle the name
    info.demangled_name = Demangle(info.mangled_name);

    // Parse class hierarchy
    auto chd = ReadStruct<RTTIClassHierarchyDescriptor>(RVAToVA(col->class_hierarchy_rva));
    if (chd) {
        info.has_virtual_base = (chd->attributes & CHD_VIRTINH) != 0;
        info.is_multiple_inheritance = (chd->attributes & CHD_MULTINH) != 0;
        info.base_classes = GetBaseClasses(col->class_hierarchy_rva);
    }

    return info;
}

std::string RTTIParser::GetMangledName(int32_t type_desc_rva) {
    // Check cache first
    auto it = name_cache_.find(type_desc_rva);
    if (it != name_cache_.end()) {
        return it->second;
    }

    uint64_t type_desc_addr = RVAToVA(type_desc_rva);

    // Read the fixed part of TypeDescriptor (vtable + internal ptr)
    auto header = read_memory_(type_desc_addr, 16);
    if (header.size() < 16) {
        return "";
    }

    // Read the mangled name string (starts at offset 0x10)
    // Read up to 256 bytes for the name
    auto name_data = read_memory_(type_desc_addr + 0x10, 256);
    if (name_data.empty()) {
        return "";
    }

    // Find null terminator
    size_t name_len = 0;
    for (size_t i = 0; i < name_data.size(); i++) {
        if (name_data[i] == 0) {
            name_len = i;
            break;
        }
    }

    if (name_len == 0 || name_len >= 256) {
        return "";
    }

    std::string mangled(reinterpret_cast<char*>(name_data.data()), name_len);

    // Validate mangled name format
    // MSVC mangled names start with ".?A" for classes
    if (mangled.size() < 4 || mangled.substr(0, 3) != ".?A") {
        return "";
    }

    // Cache the result
    name_cache_[type_desc_rva] = mangled;
    return mangled;
}

std::string RTTIParser::DemangleRTTI(const std::string& mangled) {
    // Manual demangling for MSVC RTTI type descriptors
    // Format: .?AVClassName@@  or  .?AUStructName@@  or  .?AVClass@Namespace@@

    if (mangled.size() < 5) {
        return mangled;  // Too short to be valid
    }

    // Must start with ".?A" followed by type char (V, U, T, W)
    if (mangled.substr(0, 3) != ".?A") {
        return mangled;
    }

    char type_char = mangled[3];
    std::string work = mangled.substr(4);  // Skip ".?AV" or ".?AU" etc

    // Remove trailing "@@" (end of mangled name)
    size_t end_pos = work.find("@@");
    if (end_pos != std::string::npos) {
        work = work.substr(0, end_pos);
    }

    // Handle nested namespaces (reversed order in MSVC mangling)
    // e.g., "MyClass@MyNamespace" -> "MyNamespace::MyClass"
    std::vector<std::string> parts;
    std::string current;

    for (size_t i = 0; i < work.size(); i++) {
        if (work[i] == '@') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += work[i];
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }

    // Reverse the parts to get correct namespace order
    std::reverse(parts.begin(), parts.end());

    // Build the final name with :: separators
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += "::";
        result += parts[i];
    }

    // Add type prefix based on the type character
    switch (type_char) {
        case 'V': return "class " + result;
        case 'U': return "struct " + result;
        case 'T': return "union " + result;
        case 'W': return "enum " + result;
        default: return result;
    }
}

std::string RTTIParser::Demangle(const std::string& mangled) {
    if (mangled.empty()) {
        return "";
    }

    // For RTTI type descriptors (.?AV..., .?AU..., etc), use our manual parser
    // UnDecorateSymbolName doesn't handle these properly - it produces garbage output
    if (mangled.size() >= 4 && mangled.substr(0, 3) == ".?A") {
        return DemangleRTTI(mangled);
    }

#ifdef _WIN32
    // For other decorated symbols (functions, etc), try Windows API
    char buffer[1024];
    std::string symbol = mangled;
    if (!symbol.empty() && symbol[0] == '.') {
        symbol = symbol.substr(1);
    }

    DWORD result = UnDecorateSymbolName(
        symbol.c_str(),
        buffer,
        sizeof(buffer),
        UNDNAME_NAME_ONLY
    );

    // Validate the result - if it still contains '?' or '@@', it wasn't demangled properly
    if (result > 0) {
        std::string demangled(buffer);
        if (demangled.find('?') == std::string::npos &&
            demangled.find("@@") == std::string::npos) {
            return demangled;
        }
    }
#endif

    // Return original if we couldn't demangle
    return mangled;
}

std::vector<std::string> RTTIParser::GetBaseClasses(int32_t chd_rva) {
    std::vector<std::string> bases;

    auto chd = ReadStruct<RTTIClassHierarchyDescriptor>(RVAToVA(chd_rva));
    if (!chd || chd->num_base_classes == 0) {
        return bases;
    }

    // Read the base class array (array of RVAs to BCDs)
    size_t array_size = chd->num_base_classes * sizeof(int32_t);
    auto array_data = read_memory_(RVAToVA(chd->base_class_array_rva), array_size);
    if (array_data.size() < array_size) {
        return bases;
    }

    // Parse each base class descriptor
    for (uint32_t i = 0; i < chd->num_base_classes; i++) {
        int32_t bcd_rva;
        std::memcpy(&bcd_rva, array_data.data() + i * 4, 4);

        if (!IsValidRVA(bcd_rva)) continue;

        auto bcd = ReadStruct<RTTIBaseClassDescriptor>(RVAToVA(bcd_rva));
        if (!bcd) continue;

        std::string name = GetMangledName(bcd->type_descriptor_rva);
        if (!name.empty()) {
            bases.push_back(Demangle(name));
        }
    }

    return bases;
}

size_t RTTIParser::ScanForVTables(uint64_t start, size_t size,
                                   std::function<void(const RTTIClassInfo&)> callback) {
    size_t found = 0;

    // Scan in chunks to handle large regions (DMA has read limits)
    constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024;  // 4MB chunks

    for (size_t chunk_offset = 0; chunk_offset < size; chunk_offset += CHUNK_SIZE) {
        size_t chunk_size = std::min(CHUNK_SIZE, size - chunk_offset);
        uint64_t chunk_start = start + chunk_offset;

        auto data = read_memory_(chunk_start, chunk_size);
        if (data.empty()) {
            continue;  // Skip failed chunks, try next
        }

        // Scan for potential vtable pointers (8-byte aligned)
        for (size_t offset = 0; offset + 16 <= data.size(); offset += 8) {
            uint64_t potential_col;
            std::memcpy(&potential_col, data.data() + offset, 8);

            if (potential_col == 0) continue;

            // Check if this looks like a COL pointer
            auto col = ReadStruct<RTTICompleteObjectLocator>(potential_col);
            if (!col || col->signature != 1) continue;

            // Validate the COL
            if (!IsValidRVA(col->type_descriptor_rva)) continue;

            // This appears to be vtable[-1], so vtable is at offset+8
            uint64_t vtable_addr = chunk_start + offset + 8;

            auto info = ParseVTable(vtable_addr);
            if (info) {
                found++;
                if (callback) {
                    callback(*info);
                }
            }
        }
    }

    return found;
}

bool RTTIParser::IsValidVTable(uint64_t address) {
    // Check for COL at vtable[-1]
    auto col_ptr_data = read_memory_(address - 8, 8);
    if (col_ptr_data.size() < 8) {
        return false;
    }

    uint64_t col_address;
    std::memcpy(&col_address, col_ptr_data.data(), 8);
    if (col_address == 0) {
        return false;
    }

    // Validate COL signature
    auto col = ReadStruct<RTTICompleteObjectLocator>(col_address);
    if (!col || col->signature != 1) {
        return false;
    }

    // Check first vtable entry is a valid function pointer
    auto first_entry = read_memory_(address, 8);
    if (first_entry.size() < 8) {
        return false;
    }

    uint64_t first_func;
    std::memcpy(&first_func, first_entry.data(), 8);

    // Function pointers should be in high memory on x64
    return first_func > 0x10000 && first_func < 0x00007FFFFFFFFFFF;
}

std::optional<VTableInfo> RTTIParser::ParseFullVTable(uint64_t vtable_address, size_t max_entries) {
    auto class_info = ParseVTable(vtable_address);
    if (!class_info) {
        return std::nullopt;
    }

    VTableInfo info;
    info.address = vtable_address;
    info.class_info = *class_info;

    // Read vtable entries until we hit an invalid pointer
    for (size_t i = 0; i < max_entries; i++) {
        auto entry_data = read_memory_(vtable_address + i * 8, 8);
        if (entry_data.size() < 8) break;

        uint64_t func_addr;
        std::memcpy(&func_addr, entry_data.data(), 8);

        // Stop if we hit a null or invalid pointer
        if (func_addr == 0 || func_addr < 0x10000 || func_addr > 0x00007FFFFFFFFFFF) {
            break;
        }

        VTableEntry entry;
        entry.address = func_addr;
        entry.index = static_cast<int32_t>(i);
        // Function names could be resolved via symbols if available
        entry.function_name = "";

        info.entries.push_back(entry);
    }

    info.size = info.entries.size() * 8;
    return info;
}

std::vector<PESection> RTTIParser::GetPESections(uint64_t module_base) {
    std::vector<PESection> sections;

    // Read DOS header to get PE header offset
    auto dos_header = read_memory_(module_base, 64);
    if (dos_header.size() < 64) {
        return sections;
    }

    // Check DOS signature "MZ"
    if (dos_header[0] != 'M' || dos_header[1] != 'Z') {
        return sections;
    }

    // Get e_lfanew (PE header offset) at offset 0x3C
    uint32_t pe_offset;
    std::memcpy(&pe_offset, dos_header.data() + 0x3C, 4);

    if (pe_offset == 0 || pe_offset > 0x1000) {
        return sections;  // Sanity check
    }

    // Read PE header
    auto pe_header = read_memory_(module_base + pe_offset, 0x200);
    if (pe_header.size() < 0x108) {  // Need at least through optional header
        return sections;
    }

    // Check PE signature "PE\0\0"
    if (pe_header[0] != 'P' || pe_header[1] != 'E' ||
        pe_header[2] != 0 || pe_header[3] != 0) {
        return sections;
    }

    // COFF file header starts at offset 4
    // NumberOfSections at offset 4+2 = 6
    uint16_t num_sections;
    std::memcpy(&num_sections, pe_header.data() + 6, 2);

    // SizeOfOptionalHeader at offset 4+16 = 20
    uint16_t optional_header_size;
    std::memcpy(&optional_header_size, pe_header.data() + 20, 2);

    if (num_sections == 0 || num_sections > 96) {
        return sections;  // Sanity check
    }

    // Section headers start after optional header
    // PE sig (4) + COFF header (20) + optional header
    size_t section_header_offset = 4 + 20 + optional_header_size;

    // Each section header is 40 bytes
    size_t section_data_size = num_sections * 40;

    // Read all section headers
    auto section_data = read_memory_(module_base + pe_offset + section_header_offset, section_data_size);
    if (section_data.size() < section_data_size) {
        return sections;
    }

    // Parse each section header
    for (uint16_t i = 0; i < num_sections; i++) {
        size_t offset = i * 40;

        PESection section;

        // Name is first 8 bytes (null-padded)
        char name[9] = {0};
        std::memcpy(name, section_data.data() + offset, 8);
        section.name = name;

        // VirtualSize at offset 8
        std::memcpy(&section.virtual_size, section_data.data() + offset + 8, 4);

        // VirtualAddress (RVA) at offset 12
        uint32_t rva;
        std::memcpy(&rva, section_data.data() + offset + 12, 4);
        section.virtual_address = module_base + rva;

        // Characteristics at offset 36
        std::memcpy(&section.characteristics, section_data.data() + offset + 36, 4);

        sections.push_back(section);
    }

    return sections;
}

size_t RTTIParser::ScanModule(uint64_t module_base,
                               std::function<void(const RTTIClassInfo&)> callback) {
    size_t total_found = 0;

    // Get PE sections
    auto sections = GetPESections(module_base);
    if (sections.empty()) {
        return 0;
    }

    // Scan sections that may contain vtables
    // Vtables are typically in .rdata (read-only initialized data)
    // Sometimes also in .data (read-write initialized data)
    for (const auto& section : sections) {
        // Skip non-data sections
        if (!section.IsInitializedData()) {
            continue;
        }

        // Skip executable sections (code, not data)
        if (section.IsExecutable()) {
            continue;
        }

        // Skip tiny sections
        if (section.virtual_size < 0x1000) {
            continue;
        }

        // Prioritize .rdata, but also scan .data
        if (section.name != ".rdata" && section.name != ".data") {
            continue;
        }

        // Scan this section
        size_t found = ScanForVTables(section.virtual_address, section.virtual_size, callback);
        total_found += found;
    }

    return total_found;
}

} // namespace orpheus::analysis
