#include "write_finder.h"
#include "disassembler.h"
#include "function_recovery.h"
#include "utils/logger.h"
#include <algorithm>
#include <set>
#include <queue>

namespace orpheus::analysis {

WriteFinder::WriteFinder(ReadMemoryFn read_memory, bool is_64bit)
    : read_memory_(std::move(read_memory)), is_64bit_(is_64bit) {}

WriteFinder::~WriteFinder() = default;

std::vector<WriteInfo> WriteFinder::FindDirectWrites(
    uint64_t target_address,
    const std::map<uint64_t, FunctionInfo>& functions,
    uint64_t module_base,
    uint32_t module_size,
    ProgressCallback progress,
    std::atomic<bool>* cancel)
{
    std::vector<WriteInfo> results;
    Disassembler disasm(is_64bit_);

    if (!disasm.IsValid()) {
        LOG_ERROR("WriteFinder: Failed to initialize disassembler");
        return results;
    }

    size_t total = functions.size();
    size_t processed = 0;
    size_t total_instructions = 0;

    for (const auto& [func_addr, func] : functions) {
        if (cancel && cancel->load()) break;

        processed++;
        if (progress && (processed % 100 == 0 || processed == total)) {
            progress("Scanning function " + std::to_string(processed) + "/" + std::to_string(total),
                     static_cast<float>(processed) / static_cast<float>(total));
        }

        // Determine function size to read
        uint32_t func_size = func.size;
        if (func_size == 0) {
            // Estimate: use distance to next function or cap at 4KB
            auto next_it = functions.upper_bound(func_addr);
            if (next_it != functions.end()) {
                func_size = static_cast<uint32_t>(next_it->first - func_addr);
                if (func_size > 0x10000) func_size = 0x1000;  // Cap unreasonable sizes
            } else {
                func_size = 0x1000;
            }
        }

        // Don't read past module end
        if (func_addr + func_size > module_base + module_size) {
            if (func_addr >= module_base + module_size) continue;
            func_size = static_cast<uint32_t>((module_base + module_size) - func_addr);
        }

        auto code = read_memory_(func_addr, func_size);
        if (code.empty()) continue;

        DisassemblyOptions opts;
        opts.max_instructions = 10000;
        auto instructions = disasm.Disassemble(code, func_addr, opts);
        total_instructions += instructions.size();

        for (const auto& instr : instructions) {
            // Only interested in memory write instructions
            if (!instr.is_memory_write) continue;

            // Check if the write targets our address
            if (instr.memory_address.has_value() && *instr.memory_address == target_address) {
                WriteInfo info;
                info.instruction_address = instr.address;
                info.instruction_length = instr.length;
                info.mnemonic = instr.mnemonic;
                info.operands = instr.operands;
                info.full_text = instr.full_text;
                info.target_address = target_address;
                info.target_is_static = true;
                info.function_address = func_addr;
                info.function_name = func.name.empty() ?
                    "sub_" + disasm::FormatAddress(func_addr, is_64bit_) : func.name;
                info.bytes = instr.bytes;
                results.push_back(std::move(info));
            }

            // Also check for writes via displacement that could match
            // For non-RIP-relative, the address may not be calculable statically,
            // but we can detect patterns like mov [rip+disp], reg where rip+disp == target
            // This is already handled by ZydisCalcAbsoluteAddress above for RIP-relative
        }
    }

    LOG_INFO("WriteFinder: Scanned {} functions ({} instructions), found {} writes to 0x{:X}",
             processed, total_instructions, results.size(), target_address);

    return results;
}

std::map<uint64_t, CallGraphNode> WriteFinder::BuildReverseCallGraph(
    const std::vector<uint64_t>& writer_functions,
    const std::map<uint64_t, FunctionInfo>& all_functions,
    int max_depth)
{
    std::map<uint64_t, CallGraphNode> graph;

    // Build reverse call map: callee -> set of callers
    std::map<uint64_t, std::set<uint64_t>> reverse_calls;
    for (const auto& [addr, func] : all_functions) {
        for (uint64_t callee : func.callees) {
            reverse_calls[callee].insert(addr);
        }
    }

    // BFS from writer functions upward through callers
    struct WorkItem {
        uint64_t address;
        int depth;
    };

    std::queue<WorkItem> queue;
    std::set<uint64_t> visited;

    // Seed with direct writer functions
    for (uint64_t writer : writer_functions) {
        if (visited.count(writer)) continue;
        visited.insert(writer);

        CallGraphNode node;
        node.address = writer;
        node.depth = 0;
        node.type = CallGraphNode::NodeType::DirectWriter;

        auto it = all_functions.find(writer);
        if (it != all_functions.end()) {
            node.name = it->second.name.empty() ?
                "sub_" + disasm::FormatAddress(writer, is_64bit_) : it->second.name;
        } else {
            node.name = "sub_" + disasm::FormatAddress(writer, is_64bit_);
        }

        // Queue callers of this writer
        auto callers_it = reverse_calls.find(writer);
        if (callers_it != reverse_calls.end()) {
            for (uint64_t caller : callers_it->second) {
                node.callers.push_back(caller);
                if (!visited.count(caller)) {
                    queue.push({caller, 1});
                }
            }
        }

        graph[writer] = std::move(node);
    }

    // BFS expansion
    while (!queue.empty()) {
        auto [addr, depth] = queue.front();
        queue.pop();

        if (visited.count(addr)) continue;
        if (depth > max_depth) continue;
        visited.insert(addr);

        CallGraphNode node;
        node.address = addr;
        node.depth = depth;
        node.type = CallGraphNode::NodeType::Caller;

        auto it = all_functions.find(addr);
        if (it != all_functions.end()) {
            node.name = it->second.name.empty() ?
                "sub_" + disasm::FormatAddress(addr, is_64bit_) : it->second.name;
        } else {
            node.name = "sub_" + disasm::FormatAddress(addr, is_64bit_);
        }

        // Find callers at next depth
        auto callers_it = reverse_calls.find(addr);
        if (callers_it != reverse_calls.end()) {
            for (uint64_t caller : callers_it->second) {
                node.callers.push_back(caller);
                if (!visited.count(caller) && depth + 1 <= max_depth) {
                    queue.push({caller, depth + 1});
                }
            }
        }

        // Track which of its callees are in the graph
        if (it != all_functions.end()) {
            for (uint64_t callee : it->second.callees) {
                if (graph.count(callee) || visited.count(callee)) {
                    node.callees_in_graph.push_back(callee);
                }
            }
        }

        graph[addr] = std::move(node);
    }

    // Second pass: update callees_in_graph for all nodes
    for (auto& [addr, node] : graph) {
        node.callees_in_graph.clear();
        auto it = all_functions.find(addr);
        if (it != all_functions.end()) {
            for (uint64_t callee : it->second.callees) {
                if (graph.count(callee)) {
                    node.callees_in_graph.push_back(callee);
                }
            }
        }
    }

    LOG_INFO("WriteFinder: Built reverse call graph with {} nodes (max depth {})",
             graph.size(), max_depth);

    return graph;
}

WriteTraceResult WriteFinder::TraceWrites(
    uint64_t target_address,
    const std::map<uint64_t, FunctionInfo>& functions,
    uint64_t module_base,
    uint32_t module_size,
    const std::string& module_name,
    int max_depth,
    ProgressCallback progress,
    std::atomic<bool>* cancel)
{
    WriteTraceResult result;
    result.target_address = target_address;
    result.module_base = module_base;
    result.module_size = module_size;
    result.module_name = module_name;
    result.functions_scanned = functions.size();
    result.max_depth = max_depth;

    // Phase 1: Find direct writes
    if (progress) progress("Finding direct writes...", 0.0f);

    result.direct_writes = FindDirectWrites(
        target_address, functions, module_base, module_size,
        [&](const std::string& stage, float p) {
            if (progress) progress(stage, p * 0.7f);  // 70% for write finding
        }, cancel);

    if (cancel && cancel->load()) return result;

    // Collect unique writer functions
    std::set<uint64_t> writer_set;
    for (const auto& w : result.direct_writes) {
        if (w.function_address != 0) {
            writer_set.insert(w.function_address);
        }
    }
    std::vector<uint64_t> writers(writer_set.begin(), writer_set.end());

    // Phase 2: Build reverse call graph
    if (progress) progress("Building reverse call graph...", 0.7f);

    result.call_graph = BuildReverseCallGraph(writers, functions, max_depth);

    // Attach write info to graph nodes
    for (const auto& w : result.direct_writes) {
        auto it = result.call_graph.find(w.function_address);
        if (it != result.call_graph.end()) {
            it->second.writes.push_back(w);
        }
    }

    if (progress) progress("Complete", 1.0f);

    LOG_INFO("WriteFinder: Trace complete - {} writes in {} functions, {} nodes in call graph",
             result.direct_writes.size(), writers.size(), result.call_graph.size());

    return result;
}

} // namespace orpheus::analysis
