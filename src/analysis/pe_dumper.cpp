#include "pe_dumper.h"
#include <cstring>
#include <algorithm>

namespace orpheus::analysis {

constexpr uint16_t DOS_MAGIC = 0x5A4D;      // MZ
constexpr uint32_t PE_MAGIC = 0x00004550;   // PE\0\0
constexpr uint16_t PE32_MAGIC = 0x10B;
constexpr uint16_t PE32PLUS_MAGIC = 0x20B;

// Data directory indices
constexpr int DIR_EXPORT = 0;
constexpr int DIR_IMPORT = 1;
constexpr int DIR_RESOURCE = 2;
constexpr int DIR_EXCEPTION = 3;
constexpr int DIR_SECURITY = 4;
constexpr int DIR_BASERELOC = 5;
constexpr int DIR_DEBUG = 6;
constexpr int DIR_TLS = 9;
constexpr int DIR_IAT = 12;

PEDumper::PEDumper(ReadMemoryFunc read_func)
    : read_memory_(std::move(read_func)) {
}

template<typename T>
std::optional<T> PEDumper::ReadStruct(uint64_t address) {
    auto data = read_memory_(address, sizeof(T));
    if (data.size() != sizeof(T)) {
        return std::nullopt;
    }
    T result;
    std::memcpy(&result, data.data(), sizeof(T));
    return result;
}

std::string PEDumper::ReadNullString(uint64_t address, size_t max_len) {
    auto data = read_memory_(address, max_len);
    if (data.empty()) return "";

    std::string result;
    for (uint8_t c : data) {
        if (c == 0) break;
        result += static_cast<char>(c);
    }
    return result;
}

uint32_t PEDumper::AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t PEDumper::RvaToOffset(uint32_t rva, const std::vector<PE_SECTION_HEADER>& sections) {
    for (const auto& section : sections) {
        if (rva >= section.VirtualAddress &&
            rva < section.VirtualAddress + section.VirtualSize) {
            return rva - section.VirtualAddress + section.PointerToRawData;
        }
    }
    return rva;  // Return as-is if not in any section (headers)
}

bool PEDumper::ParseHeaders(uint64_t base_address) {
    // Read DOS header
    auto dos_header = ReadStruct<PE_DOS_HEADER>(base_address);
    if (!dos_header || dos_header->e_magic != DOS_MAGIC) {
        last_error_ = "Invalid DOS header";
        return false;
    }

    // Read PE signature
    uint64_t pe_offset = base_address + dos_header->e_lfanew;
    auto pe_sig = ReadStruct<uint32_t>(pe_offset);
    if (!pe_sig || *pe_sig != PE_MAGIC) {
        last_error_ = "Invalid PE signature";
        return false;
    }

    // Read file header
    auto file_header = ReadStruct<PE_FILE_HEADER>(pe_offset + 4);
    if (!file_header) {
        last_error_ = "Failed to read file header";
        return false;
    }

    num_sections_ = file_header->NumberOfSections;

    // Read optional header magic to determine bitness
    auto opt_magic = ReadStruct<uint16_t>(pe_offset + 4 + sizeof(PE_FILE_HEADER));
    if (!opt_magic) {
        last_error_ = "Failed to read optional header";
        return false;
    }

    if (*opt_magic == PE32PLUS_MAGIC) {
        is_64bit_ = true;
        auto opt_header = ReadStruct<PE_OPTIONAL_HEADER64>(pe_offset + 4 + sizeof(PE_FILE_HEADER));
        if (!opt_header) {
            last_error_ = "Failed to read PE32+ optional header";
            return false;
        }
        image_size_ = opt_header->SizeOfImage;
        entry_point_ = opt_header->AddressOfEntryPoint;
        section_alignment_ = opt_header->SectionAlignment;
        file_alignment_ = opt_header->FileAlignment;
        std::memcpy(data_directories_, opt_header->DataDirectory, sizeof(data_directories_));
    } else if (*opt_magic == PE32_MAGIC) {
        is_64bit_ = false;
        auto opt_header = ReadStruct<PE_OPTIONAL_HEADER32>(pe_offset + 4 + sizeof(PE_FILE_HEADER));
        if (!opt_header) {
            last_error_ = "Failed to read PE32 optional header";
            return false;
        }
        image_size_ = opt_header->SizeOfImage;
        entry_point_ = opt_header->AddressOfEntryPoint;
        section_alignment_ = opt_header->SectionAlignment;
        file_alignment_ = opt_header->FileAlignment;
        std::memcpy(data_directories_, opt_header->DataDirectory, sizeof(data_directories_));
    } else {
        last_error_ = "Unknown PE optional header magic";
        return false;
    }

    return true;
}

std::vector<SectionInfo> PEDumper::GetSections(uint64_t base_address) {
    std::vector<SectionInfo> result;

    if (!ParseHeaders(base_address)) {
        return result;
    }

    // Read DOS header to get PE offset
    auto dos_header = ReadStruct<PE_DOS_HEADER>(base_address);
    if (!dos_header) return result;

    // Calculate section headers offset
    size_t opt_header_size = is_64bit_ ? sizeof(PE_OPTIONAL_HEADER64) : sizeof(PE_OPTIONAL_HEADER32);
    uint64_t section_offset = base_address + dos_header->e_lfanew + 4 +
                               sizeof(PE_FILE_HEADER) + opt_header_size;

    for (uint16_t i = 0; i < num_sections_; i++) {
        auto section = ReadStruct<PE_SECTION_HEADER>(section_offset + i * sizeof(PE_SECTION_HEADER));
        if (!section) break;

        SectionInfo info;
        info.name = std::string(section->Name, strnlen(section->Name, 8));
        info.virtual_address = section->VirtualAddress;
        info.virtual_size = section->VirtualSize;
        info.raw_size = section->SizeOfRawData;
        info.raw_offset = section->PointerToRawData;
        info.characteristics = section->Characteristics;

        result.push_back(info);
    }

    return result;
}

std::vector<ImportModule> PEDumper::GetImports(uint64_t base_address) {
    std::vector<ImportModule> result;

    if (!ParseHeaders(base_address)) {
        return result;
    }

    auto& import_dir = data_directories_[DIR_IMPORT];
    if (import_dir.VirtualAddress == 0 || import_dir.Size == 0) {
        return result;
    }

    uint64_t import_addr = base_address + import_dir.VirtualAddress;

    while (true) {
        auto descriptor = ReadStruct<PE_IMPORT_DESCRIPTOR>(import_addr);
        if (!descriptor || (descriptor->OriginalFirstThunk == 0 && descriptor->FirstThunk == 0)) {
            break;
        }

        ImportModule module;
        module.name = ReadNullString(base_address + descriptor->Name);

        // Read thunk entries
        uint32_t thunk_rva = descriptor->OriginalFirstThunk ?
                             descriptor->OriginalFirstThunk : descriptor->FirstThunk;
        uint32_t iat_rva = descriptor->FirstThunk;

        uint64_t thunk_addr = base_address + thunk_rva;
        uint64_t iat_addr = base_address + iat_rva;

        while (true) {
            ImportEntry entry;
            entry.thunk_rva = iat_rva;

            if (is_64bit_) {
                auto thunk = ReadStruct<uint64_t>(thunk_addr);
                auto iat = ReadStruct<uint64_t>(iat_addr);
                if (!thunk || *thunk == 0) break;

                entry.resolved_address = iat ? *iat : 0;

                if (*thunk & 0x8000000000000000ULL) {
                    entry.by_ordinal = true;
                    entry.ordinal = static_cast<uint16_t>(*thunk & 0xFFFF);
                } else {
                    entry.by_ordinal = false;
                    // Read hint + name
                    auto hint = ReadStruct<uint16_t>(base_address + (*thunk & 0x7FFFFFFF));
                    entry.ordinal = hint ? *hint : 0;
                    entry.name = ReadNullString(base_address + (*thunk & 0x7FFFFFFF) + 2);
                }

                thunk_addr += 8;
                iat_addr += 8;
                iat_rva += 8;
            } else {
                auto thunk = ReadStruct<uint32_t>(thunk_addr);
                auto iat = ReadStruct<uint32_t>(iat_addr);
                if (!thunk || *thunk == 0) break;

                entry.resolved_address = iat ? *iat : 0;

                if (*thunk & 0x80000000) {
                    entry.by_ordinal = true;
                    entry.ordinal = static_cast<uint16_t>(*thunk & 0xFFFF);
                } else {
                    entry.by_ordinal = false;
                    auto hint = ReadStruct<uint16_t>(base_address + *thunk);
                    entry.ordinal = hint ? *hint : 0;
                    entry.name = ReadNullString(base_address + *thunk + 2);
                }

                thunk_addr += 4;
                iat_addr += 4;
                iat_rva += 4;
            }

            module.functions.push_back(entry);
        }

        if (!module.functions.empty()) {
            result.push_back(std::move(module));
        }

        import_addr += sizeof(PE_IMPORT_DESCRIPTOR);
    }

    return result;
}

std::vector<ExportEntry> PEDumper::GetExports(uint64_t base_address) {
    std::vector<ExportEntry> result;

    if (!ParseHeaders(base_address)) {
        return result;
    }

    auto& export_dir = data_directories_[DIR_EXPORT];
    if (export_dir.VirtualAddress == 0 || export_dir.Size == 0) {
        return result;
    }

    auto exports = ReadStruct<PE_EXPORT_DIRECTORY>(base_address + export_dir.VirtualAddress);
    if (!exports) {
        return result;
    }

    uint64_t functions_addr = base_address + exports->AddressOfFunctions;
    uint64_t names_addr = base_address + exports->AddressOfNames;
    uint64_t ordinals_addr = base_address + exports->AddressOfNameOrdinals;

    // Build ordinal-to-name map
    std::map<uint16_t, std::string> ordinal_names;
    for (uint32_t i = 0; i < exports->NumberOfNames; i++) {
        auto name_rva = ReadStruct<uint32_t>(names_addr + i * 4);
        auto ordinal = ReadStruct<uint16_t>(ordinals_addr + i * 2);
        if (name_rva && ordinal) {
            std::string name = ReadNullString(base_address + *name_rva);
            ordinal_names[*ordinal] = name;
        }
    }

    // Read all exported functions
    for (uint32_t i = 0; i < exports->NumberOfFunctions; i++) {
        auto func_rva = ReadStruct<uint32_t>(functions_addr + i * 4);
        if (!func_rva || *func_rva == 0) continue;

        ExportEntry entry;
        entry.ordinal = static_cast<uint16_t>(exports->Base + i);
        entry.rva = *func_rva;
        entry.address = base_address + *func_rva;

        // Check if ordinal has a name
        auto name_it = ordinal_names.find(static_cast<uint16_t>(i));
        if (name_it != ordinal_names.end()) {
            entry.name = name_it->second;
        }

        // Check if it's a forwarder (RVA points into export directory)
        if (*func_rva >= export_dir.VirtualAddress &&
            *func_rva < export_dir.VirtualAddress + export_dir.Size) {
            entry.is_forwarder = true;
            entry.forwarder_name = ReadNullString(base_address + *func_rva);
        } else {
            entry.is_forwarder = false;
        }

        result.push_back(entry);
    }

    return result;
}

std::vector<uint8_t> PEDumper::Dump(uint64_t base_address, const DumpOptions& options) {
    if (!ParseHeaders(base_address)) {
        return {};
    }

    // Read the full image from memory
    std::vector<uint8_t> image = read_memory_(base_address, image_size_);
    if (image.size() < image_size_) {
        last_error_ = "Failed to read full image from memory";
        return {};
    }

    // Get section headers
    auto sections = GetSections(base_address);
    if (sections.empty()) {
        last_error_ = "No sections found";
        return {};
    }

    if (!options.unmap_sections) {
        // Return as-is (memory layout)
        return image;
    }

    // Build a new PE with proper file alignment
    std::vector<uint8_t> output;

    // Read DOS header info
    auto dos_header = reinterpret_cast<PE_DOS_HEADER*>(image.data());
    uint32_t pe_offset = dos_header->e_lfanew;

    // Calculate headers size
    size_t opt_header_size = is_64bit_ ? sizeof(PE_OPTIONAL_HEADER64) : sizeof(PE_OPTIONAL_HEADER32);
    size_t headers_size = pe_offset + 4 + sizeof(PE_FILE_HEADER) + opt_header_size +
                          num_sections_ * sizeof(PE_SECTION_HEADER);
    headers_size = AlignUp(static_cast<uint32_t>(headers_size), options.file_alignment);

    // Copy headers
    output.resize(headers_size, 0);
    std::memcpy(output.data(), image.data(), std::min(headers_size, image.size()));

    // Get section headers in output
    uint8_t* section_headers_ptr = output.data() + pe_offset + 4 +
                                    sizeof(PE_FILE_HEADER) + opt_header_size;

    // Process each section
    uint32_t current_offset = static_cast<uint32_t>(headers_size);

    for (uint16_t i = 0; i < num_sections_; i++) {
        auto* section = reinterpret_cast<PE_SECTION_HEADER*>(
            section_headers_ptr + i * sizeof(PE_SECTION_HEADER));

        // Calculate raw size
        uint32_t raw_size = AlignUp(section->VirtualSize, options.file_alignment);
        if (raw_size == 0) raw_size = options.file_alignment;

        // Update section header
        section->PointerToRawData = current_offset;
        section->SizeOfRawData = raw_size;

        // Copy section data
        size_t output_pos = output.size();
        output.resize(output_pos + raw_size, 0);

        if (section->VirtualAddress < image.size()) {
            size_t copy_size = std::min(
                static_cast<size_t>(section->VirtualSize),
                image.size() - section->VirtualAddress
            );
            std::memcpy(output.data() + output_pos,
                       image.data() + section->VirtualAddress,
                       copy_size);
        }

        current_offset += raw_size;
    }

    // Fix optional header
    if (options.fix_headers) {
        if (is_64bit_) {
            auto* opt = reinterpret_cast<PE_OPTIONAL_HEADER64*>(
                output.data() + pe_offset + 4 + sizeof(PE_FILE_HEADER));
            opt->FileAlignment = options.file_alignment;
            opt->SizeOfHeaders = static_cast<uint32_t>(headers_size);
        } else {
            auto* opt = reinterpret_cast<PE_OPTIONAL_HEADER32*>(
                output.data() + pe_offset + 4 + sizeof(PE_FILE_HEADER));
            opt->FileAlignment = options.file_alignment;
            opt->SizeOfHeaders = static_cast<uint32_t>(headers_size);
        }
    }

    return output;
}

} // namespace orpheus::analysis
