#include "string_scanner.h"
#include <algorithm>
#include <cctype>
#include <cwctype>

namespace orpheus::analysis {

bool StringScanner::IsPrintableAscii(uint8_t c) {
    return (c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\n' || c == '\r';
}

bool StringScanner::IsPrintableUtf16(uint16_t c) {
    // Basic Latin + Latin-1 Supplement + common ranges
    return (c >= 0x0020 && c <= 0x007E) ||  // Basic Latin
           (c >= 0x00A0 && c <= 0x00FF) ||  // Latin-1 Supplement
           (c >= 0x0100 && c <= 0x017F) ||  // Latin Extended-A
           c == 0x0009 || c == 0x000A || c == 0x000D;  // Tab, LF, CR
}

bool StringScanner::IsAsciiString(const uint8_t* data, size_t length) {
    if (length == 0) return false;

    for (size_t i = 0; i < length; i++) {
        if (!IsPrintableAscii(data[i])) {
            return false;
        }
    }
    return true;
}

bool StringScanner::IsUtf16String(const uint8_t* data, size_t length, bool little_endian) {
    if (length < 2 || length % 2 != 0) return false;

    size_t char_count = length / 2;
    for (size_t i = 0; i < char_count; i++) {
        uint16_t c;
        if (little_endian) {
            c = data[i * 2] | (data[i * 2 + 1] << 8);
        } else {
            c = (data[i * 2] << 8) | data[i * 2 + 1];
        }

        if (!IsPrintableUtf16(c)) {
            return false;
        }
    }
    return true;
}

std::string StringScanner::DecodeString(const uint8_t* data, size_t length, StringType type) {
    std::string result;

    switch (type) {
        case StringType::ASCII:
        case StringType::UTF8:
            result.assign(reinterpret_cast<const char*>(data), length);
            break;

        case StringType::UTF16_LE: {
            size_t char_count = length / 2;
            for (size_t i = 0; i < char_count; i++) {
                uint16_t c = data[i * 2] | (data[i * 2 + 1] << 8);
                if (c == 0) break;
                if (c < 0x80) {
                    result += static_cast<char>(c);
                } else if (c < 0x800) {
                    result += static_cast<char>(0xC0 | (c >> 6));
                    result += static_cast<char>(0x80 | (c & 0x3F));
                } else {
                    result += static_cast<char>(0xE0 | (c >> 12));
                    result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (c & 0x3F));
                }
            }
            break;
        }

        case StringType::UTF16_BE: {
            size_t char_count = length / 2;
            for (size_t i = 0; i < char_count; i++) {
                uint16_t c = (data[i * 2] << 8) | data[i * 2 + 1];
                if (c == 0) break;
                if (c < 0x80) {
                    result += static_cast<char>(c);
                } else if (c < 0x800) {
                    result += static_cast<char>(0xC0 | (c >> 6));
                    result += static_cast<char>(0x80 | (c & 0x3F));
                } else {
                    result += static_cast<char>(0xE0 | (c >> 12));
                    result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (c & 0x3F));
                }
            }
            break;
        }
    }

    return result;
}

std::vector<uint8_t> StringScanner::EncodeString(const std::string& str, StringType type) {
    std::vector<uint8_t> result;

    switch (type) {
        case StringType::ASCII:
        case StringType::UTF8:
            result.assign(str.begin(), str.end());
            result.push_back(0);  // Null terminator
            break;

        case StringType::UTF16_LE:
            for (char c : str) {
                result.push_back(static_cast<uint8_t>(c));
                result.push_back(0);
            }
            result.push_back(0);
            result.push_back(0);  // Wide null terminator
            break;

        case StringType::UTF16_BE:
            for (char c : str) {
                result.push_back(0);
                result.push_back(static_cast<uint8_t>(c));
            }
            result.push_back(0);
            result.push_back(0);  // Wide null terminator
            break;
    }

    return result;
}

std::vector<StringMatch> StringScanner::ScanAscii(const std::vector<uint8_t>& data,
                                                   const StringScanOptions& options,
                                                   uint64_t base_address) {
    std::vector<StringMatch> results;

    size_t i = 0;
    while (i < data.size()) {
        // Find start of potential string
        if (!IsPrintableAscii(data[i])) {
            i++;
            continue;
        }

        // Find end of string
        size_t start = i;
        size_t length = 0;

        while (i < data.size() && length < options.max_length) {
            if (data[i] == 0) {
                // Null terminator found
                if (options.null_terminated) {
                    break;
                }
            }

            if (!IsPrintableAscii(data[i]) && data[i] != 0) {
                break;
            }

            if (IsPrintableAscii(data[i])) {
                length++;
            }
            i++;
        }

        // Check minimum length
        if (length >= options.min_length) {
            StringMatch match;
            match.address = base_address + start;
            match.type = StringType::ASCII;
            match.raw_length = i - start;
            match.value = DecodeString(data.data() + start, match.raw_length, StringType::ASCII);

            // Trim null terminators from value
            while (!match.value.empty() && match.value.back() == '\0') {
                match.value.pop_back();
            }

            if (match.value.length() >= options.min_length) {
                results.push_back(std::move(match));
            }
        }

        i++;
    }

    return results;
}

std::vector<StringMatch> StringScanner::ScanUtf16(const std::vector<uint8_t>& data,
                                                   const StringScanOptions& options,
                                                   uint64_t base_address,
                                                   bool little_endian) {
    std::vector<StringMatch> results;

    size_t i = 0;
    while (i + 1 < data.size()) {
        // Read UTF-16 character
        uint16_t c;
        if (little_endian) {
            c = data[i] | (data[i + 1] << 8);
        } else {
            c = (data[i] << 8) | data[i + 1];
        }

        if (!IsPrintableUtf16(c)) {
            i += 2;
            continue;
        }

        // Found potential start
        size_t start = i;
        size_t char_count = 0;

        while (i + 1 < data.size() && char_count < options.max_length) {
            if (little_endian) {
                c = data[i] | (data[i + 1] << 8);
            } else {
                c = (data[i] << 8) | data[i + 1];
            }

            if (c == 0) {
                // Null terminator
                if (options.null_terminated) {
                    i += 2;
                    break;
                }
            }

            if (!IsPrintableUtf16(c) && c != 0) {
                break;
            }

            if (IsPrintableUtf16(c)) {
                char_count++;
            }
            i += 2;
        }

        if (char_count >= options.min_length) {
            StringMatch match;
            match.address = base_address + start;
            match.type = little_endian ? StringType::UTF16_LE : StringType::UTF16_BE;
            match.raw_length = i - start;
            match.value = DecodeString(data.data() + start, match.raw_length, match.type);

            // Trim null terminators
            while (!match.value.empty() && match.value.back() == '\0') {
                match.value.pop_back();
            }

            if (match.value.length() >= options.min_length) {
                results.push_back(std::move(match));
            }
        }
    }

    return results;
}

std::vector<StringMatch> StringScanner::Scan(const std::vector<uint8_t>& data,
                                              const StringScanOptions& options,
                                              uint64_t base_address) {
    std::vector<StringMatch> results;

    if (options.scan_ascii) {
        auto ascii_results = ScanAscii(data, options, base_address);
        results.insert(results.end(), ascii_results.begin(), ascii_results.end());
    }

    if (options.scan_utf16) {
        // Little-endian UTF-16 (Windows default)
        auto utf16le_results = ScanUtf16(data, options, base_address, true);
        results.insert(results.end(), utf16le_results.begin(), utf16le_results.end());
    }

    // Sort by address
    std::sort(results.begin(), results.end(),
              [](const StringMatch& a, const StringMatch& b) {
                  return a.address < b.address;
              });

    // Remove duplicates (strings found at same address by different methods)
    results.erase(std::unique(results.begin(), results.end(),
                              [](const StringMatch& a, const StringMatch& b) {
                                  return a.address == b.address;
                              }),
                  results.end());

    return results;
}

std::vector<uint64_t> StringScanner::FindString(const std::vector<uint8_t>& data,
                                                 const std::string& search,
                                                 bool case_sensitive,
                                                 uint64_t base_address) {
    std::vector<uint64_t> results;

    if (search.empty() || data.size() < search.size()) {
        return results;
    }

    std::string search_lower;
    if (!case_sensitive) {
        search_lower.reserve(search.size());
        for (char c : search) {
            search_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    for (size_t i = 0; i <= data.size() - search.size(); i++) {
        bool match = true;

        for (size_t j = 0; j < search.size(); j++) {
            char data_char = static_cast<char>(data[i + j]);
            char search_char = case_sensitive ? search[j] : search_lower[j];

            if (!case_sensitive) {
                data_char = static_cast<char>(std::tolower(static_cast<unsigned char>(data_char)));
            }

            if (data_char != search_char) {
                match = false;
                break;
            }
        }

        if (match) {
            results.push_back(base_address + i);
        }
    }

    return results;
}

std::vector<uint64_t> StringScanner::FindWideString(const std::vector<uint8_t>& data,
                                                     const std::wstring& search,
                                                     bool case_sensitive,
                                                     uint64_t base_address) {
    std::vector<uint64_t> results;

    size_t search_bytes = search.size() * 2;
    if (search.empty() || data.size() < search_bytes) {
        return results;
    }

    for (size_t i = 0; i <= data.size() - search_bytes; i++) {
        bool match = true;

        for (size_t j = 0; j < search.size(); j++) {
            uint16_t data_char = data[i + j * 2] | (data[i + j * 2 + 1] << 8);
            wchar_t search_char = search[j];

            if (!case_sensitive) {
                data_char = static_cast<uint16_t>(towlower(data_char));
                search_char = static_cast<wchar_t>(towlower(search_char));
            }

            if (data_char != static_cast<uint16_t>(search_char)) {
                match = false;
                break;
            }
        }

        if (match) {
            results.push_back(base_address + i);
        }
    }

    return results;
}

} // namespace orpheus::analysis
