#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <functional>

namespace orpheus::analysis {

/**
 * PatternMatch - Single pattern match result
 */
struct PatternMatch {
    uint64_t address;
    std::vector<uint8_t> context;  // Bytes around the match
    std::string module_name;       // Module where found (if applicable)
};

/**
 * Pattern - Compiled pattern for scanning
 */
struct Pattern {
    std::string name;
    std::string original;          // Original pattern string
    std::vector<uint8_t> bytes;    // Pattern bytes
    std::vector<bool> mask;        // true = must match, false = wildcard

    bool IsValid() const { return !bytes.empty() && bytes.size() == mask.size(); }
};

/**
 * PatternScanner - IDA-style pattern scanning
 *
 * Supports patterns like:
 *   "48 8B 05 ?? ?? ?? ??"
 *   "48 8B 05 ? ? ? ?"
 *   "48 8B 05 * * * *"
 *   "488B05????????"  (no spaces)
 */
class PatternScanner {
public:
    /**
     * Compile a pattern string into a Pattern object
     * @param pattern IDA-style pattern (e.g., "48 8B 05 ?? ?? ?? ??")
     * @param name Optional name for the pattern
     * @return Compiled pattern, or nullopt on parse error
     */
    static std::optional<Pattern> Compile(const std::string& pattern,
                                           const std::string& name = "");

    /**
     * Scan a buffer for a pattern
     * @param data Buffer to scan
     * @param pattern Pattern to search for
     * @param base_address Base address for results
     * @param max_results Maximum matches to return (0 = unlimited)
     * @return Vector of match addresses (relative to base_address)
     */
    static std::vector<uint64_t> Scan(const std::vector<uint8_t>& data,
                                       const Pattern& pattern,
                                       uint64_t base_address = 0,
                                       size_t max_results = 0);

    /**
     * Scan for multiple patterns in one pass (more efficient)
     */
    static std::vector<PatternMatch> ScanMultiple(
        const std::vector<uint8_t>& data,
        const std::vector<Pattern>& patterns,
        uint64_t base_address = 0,
        size_t context_size = 16);

    /**
     * Find first match of a pattern
     */
    static std::optional<uint64_t> FindFirst(const std::vector<uint8_t>& data,
                                              const Pattern& pattern,
                                              uint64_t base_address = 0);

    /**
     * Quick scan with pattern string (compiles on the fly)
     */
    static std::vector<uint64_t> QuickScan(const std::vector<uint8_t>& data,
                                            const std::string& pattern,
                                            uint64_t base_address = 0);

private:
    static bool MatchAtPosition(const std::vector<uint8_t>& data,
                                 size_t pos,
                                 const Pattern& pattern);
};

/**
 * Common game/reversing patterns
 */
namespace patterns {
    // x64 patterns
    inline const char* CALL_REL32 = "E8 ?? ?? ?? ??";
    inline const char* JMP_REL32 = "E9 ?? ?? ?? ??";
    inline const char* LEA_RIP_REL = "48 8D ?? ?? ?? ?? ??";
    inline const char* MOV_RAX_IMM64 = "48 B8 ?? ?? ?? ?? ?? ?? ?? ??";
    inline const char* MOV_RCX_IMM64 = "48 B9 ?? ?? ?? ?? ?? ?? ?? ??";

    // Function prologue
    inline const char* FUNC_PROLOGUE_1 = "40 55 48 83 EC";          // push rbp; sub rsp
    inline const char* FUNC_PROLOGUE_2 = "48 89 5C 24 ?? 48 89 6C"; // mov [rsp+?], rbx; mov [rsp+?], rbp
    inline const char* FUNC_PROLOGUE_3 = "48 83 EC ?? 48 8B";       // sub rsp, ?; mov r?, ...

    // Anti-debug
    inline const char* ISDEBUGGERPRESENT = "FF 15 ?? ?? ?? ?? 85 C0 74";
    inline const char* NTQUERYINFO = "B9 07 00 00 00";  // ProcessDebugPort
}

} // namespace orpheus::analysis
