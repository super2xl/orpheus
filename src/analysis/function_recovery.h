#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <map>
#include <set>
#include <functional>

namespace orpheus::analysis {

// Forward declarations
struct InstructionInfo;
struct BasicBlock;
class Disassembler;

/**
 * FunctionInfo - Recovered function information
 */
struct FunctionInfo {
    uint64_t entry_address;
    uint64_t end_address;       // Address after last instruction (0 if unknown)
    uint32_t size;              // Function size in bytes (0 if unknown)

    std::string name;           // Demangled name if available (from RTTI/symbols)
    std::string mangled_name;   // Mangled name if available

    // Recovery metadata
    enum class Source {
        Prologue,       // Found via prologue pattern
        CallTarget,     // Target of a CALL instruction
        ExceptionData,  // From .pdata (PE exception handling)
        RTTI,           // From RTTI vtable
        Export,         // Exported function
        Symbol,         // From debug symbols
        UserDefined     // Manually specified
    };
    Source source = Source::Prologue;
    float confidence = 0.0f;    // 0.0 - 1.0 confidence score

    // Analysis results (populated after full analysis)
    uint32_t instruction_count = 0;
    uint32_t basic_block_count = 0;
    std::vector<uint64_t> callees;      // Functions this function calls
    std::vector<uint64_t> callers;      // Functions that call this

    // Flags
    bool is_leaf = false;               // No calls to other functions
    bool is_thunk = false;              // Just a jump to another function
    bool has_unwind_info = false;       // Has exception handling data
    bool is_virtual_method = false;     // From vtable

    // For sorting/display
    std::string GetSourceString() const {
        switch (source) {
            case Source::Prologue: return "prologue";
            case Source::CallTarget: return "call_target";
            case Source::ExceptionData: return "pdata";
            case Source::RTTI: return "rtti";
            case Source::Export: return "export";
            case Source::Symbol: return "symbol";
            case Source::UserDefined: return "user";
        }
        return "unknown";
    }
};

/**
 * FunctionRecoveryOptions - Configuration for function recovery
 */
struct FunctionRecoveryOptions {
    bool use_prologues = true;          // Scan for function prologues
    bool follow_calls = true;           // Mark CALL targets as functions
    bool use_exception_data = true;     // Parse .pdata for x64 PE
    bool use_rtti = true;               // Include RTTI vtable methods
    bool use_exports = true;            // Include exported functions

    bool analyze_callgraph = true;      // Build caller/callee relationships
    bool detect_thunks = true;          // Identify thunk functions

    size_t max_functions = 100000;      // Limit for large modules
    size_t max_prologue_scan = 64 * 1024 * 1024;  // Max bytes to scan for prologues

    float min_confidence = 0.5f;        // Minimum confidence to include
};

/**
 * RUNTIME_FUNCTION - PE x64 exception data structure
 */
struct RuntimeFunction {
    uint32_t begin_address;     // RVA of function start
    uint32_t end_address;       // RVA of function end
    uint32_t unwind_info;       // RVA of unwind info
};

/**
 * FunctionRecovery - Automatic function detection and analysis
 *
 * Combines multiple techniques:
 * - Prologue pattern matching
 * - CALL target following
 * - PE exception data parsing
 * - RTTI vtable method extraction
 */
class FunctionRecovery {
public:
    using ReadMemoryFn = std::function<std::vector<uint8_t>(uint64_t addr, size_t size)>;
    using ProgressCallback = std::function<void(const std::string& stage, float progress)>;

    /**
     * Create function recovery instance
     * @param read_memory Function to read process memory
     * @param module_base Base address of module being analyzed
     * @param module_size Size of module
     * @param is_64bit True for x64, false for x86
     */
    FunctionRecovery(ReadMemoryFn read_memory,
                     uint64_t module_base,
                     uint32_t module_size,
                     bool is_64bit = true);

    ~FunctionRecovery();

    /**
     * Recover functions from the module
     * @param options Recovery options
     * @param progress Optional progress callback
     * @return Map of entry_address -> FunctionInfo
     */
    std::map<uint64_t, FunctionInfo> RecoverFunctions(
        const FunctionRecoveryOptions& options = {},
        ProgressCallback progress = nullptr);

    /**
     * Add functions from RTTI vtables
     * @param vtable_methods Map of vtable_addr -> list of method addresses
     */
    void AddRTTIFunctions(const std::map<uint64_t, std::vector<uint64_t>>& vtable_methods);

    /**
     * Add exported functions
     * @param exports Map of address -> name
     */
    void AddExportedFunctions(const std::map<uint64_t, std::string>& exports);

    /**
     * Add a user-defined function
     */
    void AddFunction(uint64_t address, const std::string& name = "",
                     FunctionInfo::Source source = FunctionInfo::Source::UserDefined);

    /**
     * Get recovered functions (after RecoverFunctions called)
     */
    const std::map<uint64_t, FunctionInfo>& GetFunctions() const { return functions_; }

    /**
     * Get function at address (exact match)
     */
    std::optional<FunctionInfo> GetFunctionAt(uint64_t address) const;

    /**
     * Get function containing address
     */
    std::optional<FunctionInfo> GetFunctionContaining(uint64_t address) const;

private:
    // Recovery methods
    void ScanForPrologues(const FunctionRecoveryOptions& options, ProgressCallback progress);
    void FollowCalls(const FunctionRecoveryOptions& options, ProgressCallback progress);
    void ParseExceptionData(const FunctionRecoveryOptions& options, ProgressCallback progress);
    void AnalyzeCallGraph(const FunctionRecoveryOptions& options, ProgressCallback progress);
    void DetectThunks(const FunctionRecoveryOptions& options);
    void ComputeFunctionBounds();
    void PruneInsideExceptionRanges();

    // PE parsing helpers
    std::optional<std::vector<RuntimeFunction>> GetExceptionDirectory();
    std::optional<uint64_t> GetDataDirectory(uint32_t index, uint32_t& size);

    // Pattern matching helpers
    bool IsPrologueAt(const uint8_t* data, size_t size, size_t offset);

    ReadMemoryFn read_memory_;
    uint64_t module_base_;
    uint32_t module_size_;
    bool is_64bit_;

    std::map<uint64_t, FunctionInfo> functions_;
    std::set<uint64_t> call_targets_;  // Discovered call targets

    // Cached module data for scanning
    std::vector<uint8_t> text_section_;
    uint64_t text_base_ = 0;
    uint32_t text_size_ = 0;
};

/**
 * Common x64 function prologues
 */
namespace prologues {
    // push rbp; mov rbp, rsp
    inline const uint8_t PUSH_RBP_MOV[] = { 0x55, 0x48, 0x8B, 0xEC };
    inline const size_t PUSH_RBP_MOV_LEN = 4;

    // push rbp; mov rbp, rsp (alternate encoding)
    inline const uint8_t PUSH_RBP_MOV_ALT[] = { 0x55, 0x48, 0x89, 0xE5 };
    inline const size_t PUSH_RBP_MOV_ALT_LEN = 4;

    // sub rsp, imm8
    inline const uint8_t SUB_RSP_IMM8[] = { 0x48, 0x83, 0xEC };
    inline const size_t SUB_RSP_IMM8_LEN = 3;

    // sub rsp, imm32
    inline const uint8_t SUB_RSP_IMM32[] = { 0x48, 0x81, 0xEC };
    inline const size_t SUB_RSP_IMM32_LEN = 3;

    // push rbx (common in leaf functions)
    inline const uint8_t PUSH_RBX[] = { 0x53 };
    inline const size_t PUSH_RBX_LEN = 1;

    // mov [rsp+8], rbx (shadow space save)
    inline const uint8_t MOV_RSP8_RBX[] = { 0x48, 0x89, 0x5C, 0x24 };
    inline const size_t MOV_RSP8_RBX_LEN = 4;

    // mov [rsp+XX], rcx (first arg save)
    inline const uint8_t MOV_RSP_RCX[] = { 0x48, 0x89, 0x4C, 0x24 };
    inline const size_t MOV_RSP_RCX_LEN = 4;
}

} // namespace orpheus::analysis
