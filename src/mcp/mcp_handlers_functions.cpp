/**
 * MCP Handlers - Function Recovery
 *
 * Function recovery and analysis handlers:
 * - HandleRecoverFunctions
 * - HandleGetFunctionAt
 * - HandleGetFunctionContaining
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "analysis/function_recovery.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleRecoverFunctions(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t module_base = std::stoull(req["module_base"].get<std::string>(), nullptr, 16);
        bool force_rescan = req.value("force_rescan", false);

        // Optional parameters
        bool use_prologues = req.value("use_prologues", true);
        bool follow_calls = req.value("follow_calls", true);
        bool use_exception_data = req.value("use_exception_data", true);
        size_t max_functions = req.value("max_functions", 100000);

        // Validate
        if (module_base == 0) {
            return CreateErrorResponse("Invalid module_base: cannot recover functions from NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid));
        }

        // Find module info
        auto modules = dma->GetModuleList(pid);
        std::string module_name;
        uint32_t module_size = 0;
        bool is_64bit = true;

        for (const auto& mod : modules) {
            if (mod.base_address == module_base) {
                module_name = mod.name;
                module_size = mod.size;
                is_64bit = mod.is_64bit;
                break;
            }
        }

        if (module_name.empty()) {
            return CreateErrorResponse("Module not found at specified base address");
        }

        // Check cache first
        if (!force_rescan && function_cache_.Exists(module_name, module_size)) {
            std::string cached = function_cache_.Load(module_name, module_size);
            if (!cached.empty()) {
                json cache_data = json::parse(cached);

                json result;
                result["status"] = "cached";
                result["module"] = module_name;
                result["module_base"] = FormatAddress(module_base);
                result["module_size"] = module_size;
                result["count"] = cache_data.value("count", 0);
                result["cache_file"] = function_cache_.GetFilePath(module_name, module_size);
                result["summary"] = cache_data.value("summary", json::object());
                result["hint"] = "Use get_function_at or get_function_containing to query functions";

                LOG_INFO("Function cache hit for {} ({} functions)", module_name,
                         cache_data.value("count", 0));

                return CreateSuccessResponse(result.dump());
            }
        }

        // Perform recovery
        LOG_INFO("Recovering functions from {} at 0x{:X}...", module_name, module_base);

        analysis::FunctionRecovery recovery(
            [dma, pid](uint64_t addr, size_t size) {
                return dma->ReadMemory(pid, addr, size);
            },
            module_base,
            module_size,
            is_64bit
        );

        analysis::FunctionRecoveryOptions opts;
        opts.use_prologues = use_prologues;
        opts.follow_calls = follow_calls;
        opts.use_exception_data = use_exception_data;
        opts.max_functions = max_functions;

        auto functions = recovery.RecoverFunctions(opts);

        // Build cache data
        json cache_data;
        cache_data["module"] = module_name;
        cache_data["module_base"] = FormatAddress(module_base);
        cache_data["module_size"] = module_size;
        cache_data["count"] = functions.size();

        // Summary stats
        int pdata_count = 0, prologue_count = 0, call_count = 0;
        int thunk_count = 0, leaf_count = 0;
        for (const auto& [addr, func] : functions) {
            switch (func.source) {
                case analysis::FunctionInfo::Source::ExceptionData: pdata_count++; break;
                case analysis::FunctionInfo::Source::Prologue: prologue_count++; break;
                case analysis::FunctionInfo::Source::CallTarget: call_count++; break;
                default: break;
            }
            if (func.is_thunk) thunk_count++;
            if (func.is_leaf) leaf_count++;
        }

        cache_data["summary"] = {
            {"from_pdata", pdata_count},
            {"from_prologue", prologue_count},
            {"from_call_target", call_count},
            {"thunks", thunk_count},
            {"leaf_functions", leaf_count}
        };

        // Store functions with RVAs
        json funcs_array = json::array();
        for (const auto& [addr, func] : functions) {
            json f;
            f["rva"] = addr - module_base;
            f["size"] = func.size;
            f["source"] = func.GetSourceString();
            f["confidence"] = func.confidence;
            if (!func.name.empty()) {
                f["name"] = func.name;
            }
            f["is_thunk"] = func.is_thunk;
            f["is_leaf"] = func.is_leaf;
            f["instruction_count"] = func.instruction_count;
            f["basic_block_count"] = func.basic_block_count;
            funcs_array.push_back(f);
        }
        cache_data["functions"] = funcs_array;

        // Save to cache
        function_cache_.Save(module_name, module_size, cache_data.dump(2));

        // Return summary
        json result;
        result["status"] = "recovered";
        result["module"] = module_name;
        result["module_base"] = FormatAddress(module_base);
        result["module_size"] = module_size;
        result["count"] = functions.size();
        result["summary"] = cache_data["summary"];
        result["cache_file"] = function_cache_.GetFilePath(module_name, module_size);
        result["hint"] = "Use get_function_at or get_function_containing to query functions";

        LOG_INFO("Recovered {} functions from {}", functions.size(), module_name);

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetFunctionAt(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Find containing module
        auto modules = dma->GetModuleList(pid);
        std::string module_name;
        uint64_t module_base = 0;
        uint32_t module_size = 0;

        for (const auto& mod : modules) {
            if (address >= mod.base_address && address < mod.base_address + mod.size) {
                module_name = mod.name;
                module_base = mod.base_address;
                module_size = mod.size;
                break;
            }
        }

        if (module_name.empty()) {
            return CreateErrorResponse("Address not within any loaded module");
        }

        // Load from cache
        if (!function_cache_.Exists(module_name, module_size)) {
            return CreateErrorResponse("Functions not recovered for " + module_name +
                                       " - run recover_functions first");
        }

        std::string cached = function_cache_.Load(module_name, module_size);
        if (cached.empty()) {
            return CreateErrorResponse("Failed to load function cache");
        }

        json cache_data = json::parse(cached);
        uint64_t target_rva = address - module_base;

        // Exact match search
        for (const auto& func : cache_data["functions"]) {
            uint64_t rva = func["rva"].get<uint64_t>();
            if (rva == target_rva) {
                json result;
                result["found"] = true;
                result["address"] = FormatAddress(address);
                result["rva"] = rva;
                result["module"] = module_name;
                result["size"] = func.value("size", 0);
                result["source"] = func.value("source", "");
                result["confidence"] = func.value("confidence", 0.0);
                result["name"] = func.value("name", "");
                result["is_thunk"] = func.value("is_thunk", false);
                result["is_leaf"] = func.value("is_leaf", false);
                result["instruction_count"] = func.value("instruction_count", 0);
                result["basic_block_count"] = func.value("basic_block_count", 0);

                return CreateSuccessResponse(result.dump());
            }
        }

        json result;
        result["found"] = false;
        result["address"] = FormatAddress(address);
        result["module"] = module_name;
        result["hint"] = "No function starts at this exact address";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetFunctionContaining(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Find containing module
        auto modules = dma->GetModuleList(pid);
        std::string module_name;
        uint64_t module_base = 0;
        uint32_t module_size = 0;

        for (const auto& mod : modules) {
            if (address >= mod.base_address && address < mod.base_address + mod.size) {
                module_name = mod.name;
                module_base = mod.base_address;
                module_size = mod.size;
                break;
            }
        }

        if (module_name.empty()) {
            return CreateErrorResponse("Address not within any loaded module");
        }

        // Load from cache
        if (!function_cache_.Exists(module_name, module_size)) {
            return CreateErrorResponse("Functions not recovered for " + module_name +
                                       " - run recover_functions first");
        }

        std::string cached = function_cache_.Load(module_name, module_size);
        if (cached.empty()) {
            return CreateErrorResponse("Failed to load function cache");
        }

        json cache_data = json::parse(cached);
        uint64_t target_rva = address - module_base;

        // Find function containing address (largest RVA <= target)
        json best_match;
        uint64_t best_rva = 0;

        for (const auto& func : cache_data["functions"]) {
            uint64_t rva = func["rva"].get<uint64_t>();
            uint32_t size = func.value("size", 0);

            if (rva <= target_rva && rva > best_rva) {
                // Check if address is within function bounds
                if (size > 0 && target_rva >= rva + size) {
                    continue;  // Address is past function end
                }
                best_match = func;
                best_rva = rva;
            }
        }

        if (!best_match.empty()) {
            json result;
            result["found"] = true;
            result["address"] = FormatAddress(address);
            result["function_start"] = FormatAddress(module_base + best_rva);
            result["offset_in_function"] = target_rva - best_rva;
            result["rva"] = best_rva;
            result["module"] = module_name;
            result["size"] = best_match.value("size", 0);
            result["source"] = best_match.value("source", "");
            result["confidence"] = best_match.value("confidence", 0.0);
            result["name"] = best_match.value("name", "");
            result["is_thunk"] = best_match.value("is_thunk", false);
            result["is_leaf"] = best_match.value("is_leaf", false);

            return CreateSuccessResponse(result.dump());
        }

        json result;
        result["found"] = false;
        result["address"] = FormatAddress(address);
        result["module"] = module_name;
        result["hint"] = "No function found containing this address";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleFindFunctionBounds(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        int max_search_up = req.value("max_search_up", 4096);    // Max bytes to search backwards
        int max_search_down = req.value("max_search_down", 8192); // Max bytes to search forwards

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: cannot find function at NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid));
        }

        // Read memory region around the address
        uint64_t search_start = address > (uint64_t)max_search_up ? address - max_search_up : 0;
        size_t total_size = max_search_up + max_search_down;
        auto data = dma->ReadMemory(pid, search_start, total_size);

        if (data.empty()) {
            return CreateErrorResponse("Failed to read memory around address " + FormatAddress(address));
        }

        size_t offset_in_data = address - search_start;
        uint64_t function_start = 0;
        uint64_t function_end = 0;
        std::string start_reason;
        std::string end_reason;

        // Common x64 function prologues (scan backwards)
        // Pattern: push rbp; mov rbp, rsp = 55 48 89 E5 or 55 48 8B EC
        // Pattern: sub rsp, imm8/imm32 = 48 83 EC xx or 48 81 EC xx xx xx xx
        // Pattern: After int3 padding (CC CC CC ...)
        // Pattern: After ret (C3) followed by int3 or NOP padding

        // Scan backwards for function start
        for (size_t i = offset_in_data; i >= 4; i--) {
            // Check for int3 padding before a prologue
            if (i >= 1 && data[i - 1] == 0xCC) {
                // Look for push rbp or sub rsp at current position
                if (data[i] == 0x55 ||  // push rbp
                    (data[i] == 0x48 && i + 1 < data.size() && (data[i + 1] == 0x83 || data[i + 1] == 0x81))) {
                    function_start = search_start + i;
                    start_reason = "int3_padding";
                    break;
                }
            }

            // Check for common prologues at position i
            // push rbp
            if (data[i] == 0x55) {
                // Verify this looks like a real prologue
                if (i + 3 < data.size() && data[i + 1] == 0x48 && data[i + 2] == 0x89 && data[i + 3] == 0xE5) {
                    // push rbp; mov rbp, rsp
                    function_start = search_start + i;
                    start_reason = "push_rbp_mov_rbp_rsp";
                    break;
                }
                if (i + 3 < data.size() && data[i + 1] == 0x48 && data[i + 2] == 0x8B && data[i + 3] == 0xEC) {
                    // push rbp; mov rbp, rsp (alternate encoding)
                    function_start = search_start + i;
                    start_reason = "push_rbp_mov_rbp_rsp_alt";
                    break;
                }
            }

            // sub rsp, imm (common in functions without frame pointer)
            if (data[i] == 0x48 && i + 2 < data.size()) {
                if (data[i + 1] == 0x83 && data[i + 2] == 0xEC) {
                    // sub rsp, imm8
                    // Verify previous byte isn't code (check for int3, ret, or call/jmp target)
                    if (i >= 1 && (data[i - 1] == 0xCC || data[i - 1] == 0xC3 || data[i - 1] == 0x90)) {
                        function_start = search_start + i;
                        start_reason = "sub_rsp_imm8";
                        break;
                    }
                }
                if (data[i + 1] == 0x81 && data[i + 2] == 0xEC) {
                    // sub rsp, imm32
                    if (i >= 1 && (data[i - 1] == 0xCC || data[i - 1] == 0xC3 || data[i - 1] == 0x90)) {
                        function_start = search_start + i;
                        start_reason = "sub_rsp_imm32";
                        break;
                    }
                }
            }

            // ret followed by start
            if (i >= 1 && data[i - 1] == 0xC3) {
                function_start = search_start + i;
                start_reason = "after_ret";
                break;
            }
        }

        // If no start found, use conservative estimate
        if (function_start == 0) {
            // Find nearest int3 sequence or ret going backwards
            for (size_t i = offset_in_data; i > 0; i--) {
                if (data[i] == 0xCC) {
                    // Count consecutive int3s
                    size_t count = 0;
                    while (i + count < data.size() && data[i + count] == 0xCC) count++;
                    if (count >= 2) {  // At least 2 int3s = padding
                        function_start = search_start + i + count;
                        start_reason = "int3_sequence";
                        break;
                    }
                }
            }
        }

        // Scan forwards for function end
        for (size_t i = offset_in_data; i < data.size() - 1; i++) {
            // ret instruction
            if (data[i] == 0xC3) {
                // Check if followed by int3 padding or another function
                if (i + 1 < data.size()) {
                    uint8_t next = data[i + 1];
                    if (next == 0xCC || next == 0x90 ||  // int3 or nop
                        next == 0x55 ||                   // push rbp (next function)
                        next == 0x48 ||                   // Possible sub rsp or mov
                        next == 0x40) {                   // rex prefix
                        function_end = search_start + i + 1;
                        end_reason = "ret_instruction";
                        break;
                    }
                }
                // If followed by meaningful code, might be a conditional return
                // Continue searching
            }

            // int3 after instruction indicates function end
            if (data[i] == 0xC3 || (data[i] == 0xC2 && i + 2 < data.size())) {
                // ret or ret imm16
                function_end = search_start + i + (data[i] == 0xC2 ? 3 : 1);
                end_reason = data[i] == 0xC2 ? "ret_imm16" : "ret";
                break;
            }

            // jmp rax/rcx/rdx etc (tail call or switch) can indicate function end
            if (data[i] == 0xFF && i + 1 < data.size()) {
                uint8_t modrm = data[i + 1];
                if ((modrm & 0xF8) == 0xE0) {  // jmp reg
                    // Check if followed by int3
                    if (i + 2 < data.size() && data[i + 2] == 0xCC) {
                        function_end = search_start + i + 2;
                        end_reason = "jmp_reg_tail_call";
                        break;
                    }
                }
            }
        }

        json result;
        result["address"] = FormatAddress(address);
        result["context"] = FormatAddressWithContext(pid, address);

        if (function_start != 0) {
            result["function_start"] = FormatAddress(function_start);
            result["start_context"] = FormatAddressWithContext(pid, function_start);
            result["start_reason"] = start_reason;
            result["offset_in_function"] = address - function_start;
        } else {
            result["function_start_found"] = false;
            result["hint_start"] = "Could not detect function start - consider using recover_functions for more accurate results";
        }

        if (function_end != 0) {
            result["function_end"] = FormatAddress(function_end);
            result["end_context"] = FormatAddressWithContext(pid, function_end);
            result["end_reason"] = end_reason;
            if (function_start != 0) {
                result["estimated_size"] = function_end - function_start;
            }
        } else {
            result["function_end_found"] = false;
            result["hint_end"] = "Could not detect function end - function may be very large or use unusual control flow";
        }

        result["confidence"] = (function_start != 0 && function_end != 0) ? "high" :
                               (function_start != 0 || function_end != 0) ? "medium" : "low";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
