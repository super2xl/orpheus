/**
 * MCP Handlers - Control Flow Graph
 *
 * CFG building and visualization handlers:
 * - HandleBuildCFG
 * - HandleGetCFGNode
 */

#include "mcp_server.h"
#include "core/orpheus_core.h"
#include "core/dma_interface.h"
#include "analysis/cfg_builder.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleBuildCFG(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        size_t max_size = req.value("max_size", 0x10000);  // Default 64KB
        bool compute_layout = req.value("compute_layout", true);

        // Validate
        if (address == 0) {
            return CreateErrorResponse("Invalid address: cannot build CFG from NULL (0x0)");
        }

        if (max_size > 0x100000) {  // 1MB limit
            return CreateErrorResponse("max_size too large (max 1MB)");
        }

        auto* dma = core_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid));
        }

        // Determine architecture
        auto modules = dma->GetModuleList(pid);
        bool is_64bit = true;
        std::string module_name = "unknown";

        for (const auto& mod : modules) {
            if (address >= mod.base_address && address < mod.base_address + mod.size) {
                is_64bit = mod.is_64bit;
                module_name = mod.name;
                break;
            }
        }

        LOG_INFO("Building CFG at 0x{:X} (max_size: {})", address, static_cast<uint64_t>(max_size));

        // Create CFG builder with memory read callback
        analysis::CFGBuilder builder(
            [dma, pid](uint64_t addr, size_t size) {
                return dma->ReadMemory(pid, addr, size);
            },
            is_64bit
        );

        // Build CFG
        auto cfg = builder.BuildCFG(address, max_size);

        if (cfg.nodes.empty()) {
            return CreateErrorResponse("Failed to build CFG - no valid instructions found");
        }

        // Compute layout for visualization
        if (compute_layout) {
            builder.ComputeLayout(cfg);
        }

        // Build response
        json result;
        result["function_address"] = FormatAddress(cfg.function_address);
        result["function_end"] = FormatAddress(cfg.function_end);
        result["module"] = module_name;
        result["node_count"] = cfg.node_count;
        result["edge_count"] = cfg.edge_count;
        result["max_depth"] = cfg.max_depth;
        result["has_loops"] = cfg.has_loops;

        // Nodes array
        json nodes_array = json::array();
        for (const auto& [addr, node] : cfg.nodes) {
            json n;
            n["address"] = FormatAddress(addr);
            n["end_address"] = FormatAddress(node.end_address);
            n["size"] = node.size;
            n["instruction_count"] = node.instructions.size();

            // Type string
            switch (node.type) {
                case analysis::CFGNode::Type::Entry: n["type"] = "entry"; break;
                case analysis::CFGNode::Type::Exit: n["type"] = "exit"; break;
                case analysis::CFGNode::Type::Call: n["type"] = "call"; break;
                case analysis::CFGNode::Type::ConditionalJump: n["type"] = "conditional"; break;
                case analysis::CFGNode::Type::UnconditionalJump: n["type"] = "unconditional"; break;
                case analysis::CFGNode::Type::Switch: n["type"] = "switch"; break;
                default: n["type"] = "normal"; break;
            }

            // Successors and predecessors
            json succs = json::array();
            for (uint64_t succ : node.successors) {
                succs.push_back(FormatAddress(succ));
            }
            n["successors"] = succs;

            json preds = json::array();
            for (uint64_t pred : node.predecessors) {
                preds.push_back(FormatAddress(pred));
            }
            n["predecessors"] = preds;

            // Layout info
            if (compute_layout) {
                n["layout"] = {
                    {"x", node.x},
                    {"y", node.y},
                    {"width", node.width},
                    {"height", node.height},
                    {"row", node.row},
                    {"column", node.column}
                };
            }

            // Loop info
            n["is_loop_header"] = node.is_loop_header;
            n["is_loop_body"] = node.is_loop_body;

            // First and last instruction for context
            if (!node.instructions.empty()) {
                n["first_instruction"] = node.instructions.front().full_text;
                n["last_instruction"] = node.instructions.back().full_text;
            }

            nodes_array.push_back(n);
        }
        result["nodes"] = nodes_array;

        // Edges array
        json edges_array = json::array();
        for (const auto& edge : cfg.edges) {
            json e;
            e["from"] = FormatAddress(edge.from);
            e["to"] = FormatAddress(edge.to);
            e["is_back_edge"] = edge.is_back_edge;

            switch (edge.type) {
                case analysis::CFGEdge::Type::FallThrough: e["type"] = "fall_through"; break;
                case analysis::CFGEdge::Type::Branch: e["type"] = "branch"; break;
                case analysis::CFGEdge::Type::Unconditional: e["type"] = "unconditional"; break;
                case analysis::CFGEdge::Type::Call: e["type"] = "call"; break;
                case analysis::CFGEdge::Type::Return: e["type"] = "return"; break;
            }

            edges_array.push_back(e);
        }
        result["edges"] = edges_array;

        LOG_INFO("Built CFG: {} nodes, {} edges, depth {}",
                 cfg.node_count, cfg.edge_count, cfg.max_depth);

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetCFGNode(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t function_addr = std::stoull(req["function_address"].get<std::string>(), nullptr, 16);
        uint64_t node_addr = std::stoull(req["node_address"].get<std::string>(), nullptr, 16);

        auto* dma = core_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Determine architecture
        auto modules = dma->GetModuleList(pid);
        bool is_64bit = true;

        for (const auto& mod : modules) {
            if (function_addr >= mod.base_address && function_addr < mod.base_address + mod.size) {
                is_64bit = mod.is_64bit;
                break;
            }
        }

        // Build CFG
        analysis::CFGBuilder builder(
            [dma, pid](uint64_t addr, size_t size) {
                return dma->ReadMemory(pid, addr, size);
            },
            is_64bit
        );

        auto cfg = builder.BuildCFG(function_addr);

        // Find the requested node
        auto it = cfg.nodes.find(node_addr);
        if (it == cfg.nodes.end()) {
            return CreateErrorResponse("Node not found in CFG");
        }

        const auto& node = it->second;

        // Build detailed response
        json result;
        result["address"] = FormatAddress(node_addr);
        result["end_address"] = FormatAddress(node.end_address);
        result["size"] = node.size;

        // Type
        switch (node.type) {
            case analysis::CFGNode::Type::Entry: result["type"] = "entry"; break;
            case analysis::CFGNode::Type::Exit: result["type"] = "exit"; break;
            case analysis::CFGNode::Type::Call: result["type"] = "call"; break;
            case analysis::CFGNode::Type::ConditionalJump: result["type"] = "conditional"; break;
            case analysis::CFGNode::Type::UnconditionalJump: result["type"] = "unconditional"; break;
            case analysis::CFGNode::Type::Switch: result["type"] = "switch"; break;
            default: result["type"] = "normal"; break;
        }

        // Full disassembly
        json instructions = json::array();
        for (const auto& instr : node.instructions) {
            json i;
            i["address"] = FormatAddress(instr.address);
            i["text"] = instr.full_text;
            i["mnemonic"] = instr.mnemonic;
            i["length"] = instr.length;

            if (instr.branch_target) {
                i["branch_target"] = FormatAddress(*instr.branch_target);
            }

            i["is_call"] = instr.is_call;
            i["is_jump"] = instr.is_jump;
            i["is_ret"] = instr.is_ret;
            i["is_conditional"] = instr.is_conditional;

            instructions.push_back(i);
        }
        result["instructions"] = instructions;

        // Successors with edge type
        json succs = json::array();
        for (uint64_t succ : node.successors) {
            json s;
            s["address"] = FormatAddress(succ);

            // Find edge type
            for (const auto& edge : cfg.edges) {
                if (edge.from == node_addr && edge.to == succ) {
                    switch (edge.type) {
                        case analysis::CFGEdge::Type::FallThrough: s["edge_type"] = "fall_through"; break;
                        case analysis::CFGEdge::Type::Branch: s["edge_type"] = "branch"; break;
                        case analysis::CFGEdge::Type::Unconditional: s["edge_type"] = "unconditional"; break;
                        default: s["edge_type"] = "unknown"; break;
                    }
                    s["is_back_edge"] = edge.is_back_edge;
                    break;
                }
            }
            succs.push_back(s);
        }
        result["successors"] = succs;

        // Predecessors
        json preds = json::array();
        for (uint64_t pred : node.predecessors) {
            preds.push_back(FormatAddress(pred));
        }
        result["predecessors"] = preds;

        result["is_loop_header"] = node.is_loop_header;
        result["is_loop_body"] = node.is_loop_body;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
