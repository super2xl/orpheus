#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>

namespace orpheus::analysis {

/**
 * StringType - Type of string encoding
 */
enum class StringType {
    ASCII,
    UTF16_LE,
    UTF16_BE,
    UTF8
};

/**
 * StringMatch - Found string result
 */
struct StringMatch {
    uint64_t address;
    std::string value;          // Decoded string (UTF-8)
    StringType type;
    size_t raw_length;          // Length in bytes
    std::string module_name;    // Module where found (if applicable)
};

/**
 * StringScanOptions - Configuration for string scanning
 */
struct StringScanOptions {
    size_t min_length = 4;      // Minimum string length
    size_t max_length = 1024;   // Maximum string length
    bool scan_ascii = true;
    bool scan_utf16 = true;
    bool scan_utf8 = true;
    bool printable_only = true; // Only include printable characters
    bool null_terminated = true; // Require null termination
};

/**
 * StringScanner - Find readable strings in binary data
 *
 * Similar to the 'strings' utility but with more encoding support
 */
class StringScanner {
public:
    /**
     * Scan buffer for strings
     * @param data Buffer to scan
     * @param options Scanning options
     * @param base_address Base address for results
     * @return Vector of found strings
     */
    static std::vector<StringMatch> Scan(const std::vector<uint8_t>& data,
                                          const StringScanOptions& options = {},
                                          uint64_t base_address = 0);

    /**
     * Search for a specific string (case-insensitive optional)
     * @param data Buffer to scan
     * @param search String to search for
     * @param case_sensitive Whether to match case
     * @param base_address Base address for results
     * @return Vector of match addresses
     */
    static std::vector<uint64_t> FindString(const std::vector<uint8_t>& data,
                                             const std::string& search,
                                             bool case_sensitive = true,
                                             uint64_t base_address = 0);

    /**
     * Search for a wide string
     */
    static std::vector<uint64_t> FindWideString(const std::vector<uint8_t>& data,
                                                 const std::wstring& search,
                                                 bool case_sensitive = true,
                                                 uint64_t base_address = 0);

    /**
     * Check if a byte sequence looks like ASCII text
     */
    static bool IsAsciiString(const uint8_t* data, size_t length);

    /**
     * Check if a byte sequence looks like UTF-16 text
     */
    static bool IsUtf16String(const uint8_t* data, size_t length, bool little_endian = true);

    /**
     * Decode a string from bytes
     */
    static std::string DecodeString(const uint8_t* data, size_t length, StringType type);

    /**
     * Encode a string to bytes
     */
    static std::vector<uint8_t> EncodeString(const std::string& str, StringType type);

private:
    static std::vector<StringMatch> ScanAscii(const std::vector<uint8_t>& data,
                                               const StringScanOptions& options,
                                               uint64_t base_address);

    static std::vector<StringMatch> ScanUtf16(const std::vector<uint8_t>& data,
                                               const StringScanOptions& options,
                                               uint64_t base_address,
                                               bool little_endian);

    static bool IsPrintableAscii(uint8_t c);
    static bool IsPrintableUtf16(uint16_t c);
};

} // namespace orpheus::analysis
