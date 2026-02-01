#include "pattern_scanner.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// SIMD intrinsics
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace orpheus::analysis {

// SIMD-optimized first-byte finder using SSE2
// Returns bitmask where bits are set for positions matching first_byte
static inline uint32_t FindFirstByteSIMD(const uint8_t* data, uint8_t first_byte) {
    __m128i needle = _mm_set1_epi8(static_cast<char>(first_byte));
    __m128i haystack = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
    __m128i cmp = _mm_cmpeq_epi8(needle, haystack);
    return static_cast<uint32_t>(_mm_movemask_epi8(cmp));
}

// SIMD match for patterns ≤16 bytes with wildcard support
// Uses SSE2 only for maximum compatibility
static bool MatchSIMD16(const uint8_t* data, const uint8_t* pattern_bytes,
                        const uint8_t* mask_bytes, size_t len) {
    if (len > 16) return false;

    // Pad pattern and mask to 16 bytes
    alignas(16) uint8_t pattern_buf[16] = {0};
    alignas(16) uint8_t mask_buf[16] = {0};  // 0x00 = wildcard, 0xFF = must match

    for (size_t i = 0; i < len; i++) {
        pattern_buf[i] = pattern_bytes[i];
        mask_buf[i] = mask_bytes[i];
    }

    __m128i pat = _mm_load_si128(reinterpret_cast<const __m128i*>(pattern_buf));
    __m128i msk = _mm_load_si128(reinterpret_cast<const __m128i*>(mask_buf));
    __m128i dat = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));

    // XOR data with pattern, then AND with mask
    // If result is all zeros, we have a match
    __m128i xored = _mm_xor_si128(dat, pat);
    __m128i masked = _mm_and_si128(xored, msk);

    // SSE2 compatible: compare to zero and check movemask
    __m128i zero = _mm_setzero_si128();
    __m128i cmp = _mm_cmpeq_epi8(masked, zero);
    return _mm_movemask_epi8(cmp) == 0xFFFF;
}

// Scalar fallback for patterns > 16 bytes
static bool MatchScalar(const uint8_t* data, const uint8_t* pattern_bytes,
                        const std::vector<bool>& mask, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (mask[i] && data[i] != pattern_bytes[i]) {
            return false;
        }
    }
    return true;
}

std::optional<Pattern> PatternScanner::Compile(const std::string& pattern,
                                                const std::string& name) {
    // Prevent excessively long patterns (DoS protection)
    // Max 1024 bytes = 2048 hex chars + spaces
    static constexpr size_t MAX_PATTERN_INPUT = 4096;
    static constexpr size_t MAX_PATTERN_BYTES = 1024;

    if (pattern.length() > MAX_PATTERN_INPUT) {
        return std::nullopt;
    }

    Pattern result;
    result.name = name;
    result.original = pattern;

    std::string cleaned;

    // Remove spaces and normalize
    for (char c : pattern) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            cleaned += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    if (cleaned.empty() || cleaned.length() % 2 != 0) {
        return std::nullopt;
    }

    // Additional size check after cleaning
    if (cleaned.length() / 2 > MAX_PATTERN_BYTES) {
        return std::nullopt;
    }

    // Parse pairs of characters
    for (size_t i = 0; i < cleaned.length(); i += 2) {
        char c1 = cleaned[i];
        char c2 = cleaned[i + 1];

        // Check for wildcards: ??, **, xx, XX
        if ((c1 == '?' && c2 == '?') ||
            (c1 == '*' && c2 == '*') ||
            (c1 == 'X' && c2 == 'X')) {
            result.bytes.push_back(0x00);
            result.mask.push_back(false);  // Don't match
        }
        // Single wildcard character (treat pair as wildcard)
        else if (c1 == '?' || c1 == '*' || c2 == '?' || c2 == '*') {
            result.bytes.push_back(0x00);
            result.mask.push_back(false);
        }
        else {
            // Parse hex byte
            auto hexCharToInt = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };

            int high = hexCharToInt(c1);
            int low = hexCharToInt(c2);

            if (high < 0 || low < 0) {
                return std::nullopt;  // Invalid hex
            }

            result.bytes.push_back(static_cast<uint8_t>((high << 4) | low));
            result.mask.push_back(true);  // Must match
        }
    }

    if (!result.IsValid()) {
        return std::nullopt;
    }

    return result;
}

bool PatternScanner::MatchAtPosition(const std::vector<uint8_t>& data,
                                      size_t pos,
                                      const Pattern& pattern) {
    const size_t pattern_len = pattern.bytes.size();

    if (pos + pattern_len > data.size()) {
        return false;
    }

    const uint8_t* data_ptr = data.data() + pos;

    // Use SIMD for patterns ≤16 bytes when we have enough buffer space
    if (pattern_len <= 16 && pos + 16 <= data.size()) {
        // Build byte mask on the fly (could optimize by caching)
        alignas(16) uint8_t byte_mask[16] = {0};
        for (size_t i = 0; i < pattern_len; i++) {
            byte_mask[i] = pattern.mask[i] ? 0xFF : 0x00;
        }
        return MatchSIMD16(data_ptr, pattern.bytes.data(), byte_mask, pattern_len);
    }

    // Scalar fallback
    return MatchScalar(data_ptr, pattern.bytes.data(), pattern.mask, pattern_len);
}

std::vector<uint64_t> PatternScanner::Scan(const std::vector<uint8_t>& data,
                                            const Pattern& pattern,
                                            uint64_t base_address,
                                            size_t max_results) {
    std::vector<uint64_t> results;

    if (!pattern.IsValid() || data.empty() || pattern.bytes.size() > data.size()) {
        return results;
    }

    const size_t pattern_len = pattern.bytes.size();
    const size_t scan_end = data.size() - pattern_len + 1;
    const uint8_t* data_ptr = data.data();
    const uint8_t first_byte = pattern.bytes[0];
    const bool first_byte_is_wildcard = !pattern.mask[0];

    // Pre-compute byte mask for SIMD (0xFF = must match, 0x00 = wildcard)
    std::vector<uint8_t> byte_mask(pattern_len);
    for (size_t i = 0; i < pattern_len; i++) {
        byte_mask[i] = pattern.mask[i] ? 0xFF : 0x00;
    }

    // Use SIMD for small patterns (≤16 bytes)
    const bool use_simd = (pattern_len <= 16);

    // SIMD scan: process 16 bytes at a time looking for first-byte candidates
    size_t i = 0;

    // Only use SIMD first-byte filter if first byte is not a wildcard
    if (!first_byte_is_wildcard && scan_end >= 16) {
        while (i + 16 <= scan_end) {
            uint32_t mask = FindFirstByteSIMD(data_ptr + i, first_byte);

            if (mask != 0) {
                // Found potential matches - check each bit
                while (mask != 0) {
                    // Find position of lowest set bit
#ifdef _MSC_VER
                    unsigned long bit_pos;
                    _BitScanForward(&bit_pos, mask);
#else
                    int bit_pos = __builtin_ctz(mask);
#endif
                    size_t pos = i + bit_pos;

                    if (pos < scan_end) {
                        bool matched = false;
                        if (use_simd) {
                            matched = MatchSIMD16(data_ptr + pos, pattern.bytes.data(),
                                                  byte_mask.data(), pattern_len);
                        } else {
                            matched = MatchScalar(data_ptr + pos, pattern.bytes.data(),
                                                  pattern.mask, pattern_len);
                        }

                        if (matched) {
                            results.push_back(base_address + pos);
                            if (max_results > 0 && results.size() >= max_results) {
                                return results;
                            }
                        }
                    }

                    // Clear the bit we just processed
                    mask &= mask - 1;
                }
            }

            i += 16;
        }
    }

    // Scalar fallback for remaining bytes or when first byte is wildcard
    for (; i < scan_end; i++) {
        bool matched = false;
        if (use_simd && i + 16 <= data.size()) {
            matched = MatchSIMD16(data_ptr + i, pattern.bytes.data(),
                                  byte_mask.data(), pattern_len);
        } else {
            matched = MatchScalar(data_ptr + i, pattern.bytes.data(),
                                  pattern.mask, pattern_len);
        }

        if (matched) {
            results.push_back(base_address + i);
            if (max_results > 0 && results.size() >= max_results) {
                return results;
            }
        }
    }

    return results;
}

std::vector<PatternMatch> PatternScanner::ScanMultiple(
    const std::vector<uint8_t>& data,
    const std::vector<Pattern>& patterns,
    uint64_t base_address,
    size_t context_size) {

    std::vector<PatternMatch> results;

    if (data.empty() || patterns.empty()) {
        return results;
    }

    // Find minimum pattern length
    size_t min_pattern_len = SIZE_MAX;
    for (const auto& p : patterns) {
        if (p.IsValid() && p.bytes.size() < min_pattern_len) {
            min_pattern_len = p.bytes.size();
        }
    }

    if (min_pattern_len == SIZE_MAX) {
        return results;
    }

    size_t scan_end = data.size() - min_pattern_len + 1;

    for (size_t i = 0; i < scan_end; i++) {
        for (const auto& pattern : patterns) {
            if (!pattern.IsValid()) continue;

            if (MatchAtPosition(data, i, pattern)) {
                PatternMatch match;
                match.address = base_address + i;

                // Extract context bytes
                size_t ctx_start = (i >= context_size) ? i - context_size : 0;
                size_t ctx_end = std::min(i + pattern.bytes.size() + context_size, data.size());

                match.context.assign(data.begin() + ctx_start, data.begin() + ctx_end);

                results.push_back(std::move(match));
            }
        }
    }

    return results;
}

std::optional<uint64_t> PatternScanner::FindFirst(const std::vector<uint8_t>& data,
                                                   const Pattern& pattern,
                                                   uint64_t base_address) {
    auto results = Scan(data, pattern, base_address, 1);
    if (!results.empty()) {
        return results[0];
    }
    return std::nullopt;
}

std::vector<uint64_t> PatternScanner::QuickScan(const std::vector<uint8_t>& data,
                                                 const std::string& pattern,
                                                 uint64_t base_address) {
    auto compiled = Compile(pattern);
    if (!compiled) {
        return {};
    }
    return Scan(data, *compiled, base_address, 0);
}

} // namespace orpheus::analysis
