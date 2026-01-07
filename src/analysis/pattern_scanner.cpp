#include "pattern_scanner.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace orpheus::analysis {

std::optional<Pattern> PatternScanner::Compile(const std::string& pattern,
                                                const std::string& name) {
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
    if (pos + pattern.bytes.size() > data.size()) {
        return false;
    }

    for (size_t i = 0; i < pattern.bytes.size(); i++) {
        if (pattern.mask[i] && data[pos + i] != pattern.bytes[i]) {
            return false;
        }
    }

    return true;
}

std::vector<uint64_t> PatternScanner::Scan(const std::vector<uint8_t>& data,
                                            const Pattern& pattern,
                                            uint64_t base_address,
                                            size_t max_results) {
    std::vector<uint64_t> results;

    if (!pattern.IsValid() || data.empty()) {
        return results;
    }

    size_t scan_end = data.size() - pattern.bytes.size() + 1;

    for (size_t i = 0; i < scan_end; i++) {
        if (MatchAtPosition(data, i, pattern)) {
            results.push_back(base_address + i);

            if (max_results > 0 && results.size() >= max_results) {
                break;
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
