#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <functional>
#include <atomic>

namespace orpheus::analysis {

struct FunctionInfo;

/**
 * WriteInfo — A code location that writes to a target memory address/range
 */
struct WriteInfo {
    uint64_t instruction_address;   // Address of the write instruction
    uint8_t instruction_length;
    std::string mnemonic;           // e.g., "MOV", "ADD", "XOR"
    std::string operands;           // Full operand string
    std::string full_text;          // Complete instruction text

    uint64_t target_address;        // Address being written to (if statically known)
    bool target_is_static;          // true if target address is fully resolved (RIP-relative)
                                    // false if register-based (needs runtime context)

    // Containing function info
    uint64_t function_address = 0;
    std::string function_name;

    // For display
    std::vector<uint8_t> bytes;     // Instruction bytes
};

/**
 * CallGraphNode — A node in the reverse call graph
 */
struct CallGraphNode {
    uint64_t address;               // Function entry address
    std::string name;               // Function name (or auto-generated)
    int depth;                      // 0 = direct writer, 1+ = callers

    enum class NodeType {
        Target,         // The target address being analyzed
        DirectWriter,   // Function containing a write instruction
        Caller          // Function that calls a writer/caller
    };
    NodeType type;

    std::vector<WriteInfo> writes;          // Write instructions in this function (depth=0 only)
    std::vector<uint64_t> callers;          // Who calls this function
    std::vector<uint64_t> callees_in_graph; // Which nodes this calls (filtered to graph members)
};

/**
 * WriteTraceResult — Complete result of a write trace analysis
 */
struct WriteTraceResult {
    uint64_t target_address;
    uint64_t module_base;
    uint32_t module_size;
    std::string module_name;

    std::vector<WriteInfo> direct_writes;                   // All write instructions found
    std::map<uint64_t, CallGraphNode> call_graph;           // Reverse call graph
    int max_depth;

    size_t functions_scanned = 0;
    size_t instructions_scanned = 0;
};

/**
 * WriteFinder — Finds code that writes to a target address and builds reverse call graphs
 *
 * Phase 1: Static write detection
 *   - Scans all recovered functions for instructions that write to the target
 *   - Handles RIP-relative and displacement-based addressing
 *
 * Phase 2: Reverse call graph
 *   - For each writer function, walks the call graph upward
 *   - Shows the full pipeline from entry points to the write
 */
class WriteFinder {
public:
    using ReadMemoryFn = std::function<std::vector<uint8_t>(uint64_t addr, size_t size)>;
    using ProgressCallback = std::function<void(const std::string& stage, float progress)>;

    WriteFinder(ReadMemoryFn read_memory, bool is_64bit = true);
    ~WriteFinder();

    /**
     * Find all instructions that write to a specific address.
     * Scans all provided functions for write instructions targeting the address.
     *
     * @param target_address The memory address to find writes to
     * @param functions Map of recovered functions to scan
     * @param module_base Module base address (for context)
     * @param module_size Module size
     * @param progress Optional progress callback
     * @param cancel Optional cancellation flag
     * @return Vector of write locations
     */
    std::vector<WriteInfo> FindDirectWrites(
        uint64_t target_address,
        const std::map<uint64_t, FunctionInfo>& functions,
        uint64_t module_base,
        uint32_t module_size,
        ProgressCallback progress = nullptr,
        std::atomic<bool>* cancel = nullptr);

    /**
     * Build reverse call graph from writer functions upward.
     *
     * @param writer_functions Entry addresses of functions that write to the target
     * @param all_functions All recovered functions (for caller info)
     * @param max_depth Maximum depth of call graph traversal
     * @return Map of function_address -> CallGraphNode
     */
    std::map<uint64_t, CallGraphNode> BuildReverseCallGraph(
        const std::vector<uint64_t>& writer_functions,
        const std::map<uint64_t, FunctionInfo>& all_functions,
        int max_depth = 5);

    /**
     * Complete analysis: find writes + build call graph
     */
    WriteTraceResult TraceWrites(
        uint64_t target_address,
        const std::map<uint64_t, FunctionInfo>& functions,
        uint64_t module_base,
        uint32_t module_size,
        const std::string& module_name = "",
        int max_depth = 5,
        ProgressCallback progress = nullptr,
        std::atomic<bool>* cancel = nullptr);

private:
    ReadMemoryFn read_memory_;
    bool is_64bit_;
};

} // namespace orpheus::analysis
