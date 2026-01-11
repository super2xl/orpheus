/**
 * MCP Handlers - Analysis
 *
 * Code analysis handlers:
 * - HandleDisassemble
 * - HandleDecompile
 * - HandleDumpModule
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "core/runtime_manager.h"
#include "analysis/disassembler.h"
#include "analysis/signature.h"
#include "decompiler/decompiler.hh"
#include "dumper/cs2_schema.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <regex>
#include <functional>

using json = nlohmann::json;

namespace {

// Post-process decompiler output for better readability
// Converts raw addresses to module+offset format
std::string PostProcessDecompilerOutput(
    const std::string& c_code,
    const std::vector<orpheus::ModuleInfo>& modules)
{
    std::string result = c_code;

    // Helper to find module for address
    auto find_module = [&modules](uint64_t addr) -> std::pair<std::string, uint64_t> {
        for (const auto& mod : modules) {
            if (addr >= mod.base_address && addr < mod.base_address + mod.size) {
                return {mod.name, addr - mod.base_address};
            }
        }
        return {"", 0};
    };

    // Helper to format hex
    auto format_hex = [](uint64_t val) -> std::string {
        std::ostringstream ss;
        ss << "0x" << std::hex << val;
        return ss.str();
    };

    // Helper to apply regex replacements
    auto apply_regex = [](const std::string& input, const std::regex& re,
                          std::function<std::string(const std::smatch&)> replacer) -> std::string {
        std::string output;
        std::sregex_iterator it(input.begin(), input.end(), re);
        std::sregex_iterator end;
        size_t last_pos = 0;

        for (; it != end; ++it) {
            output += input.substr(last_pos, it->position() - last_pos);
            output += replacer(*it);
            last_pos = it->position() + it->length();
        }
        output += input.substr(last_pos);
        return output;
    };

    // 1. Fix function names: func_<decimal> -> func_<hex_with_context>
    //    Pattern: func_(\d{10,}) - decimal addresses 10+ digits
    std::regex func_decimal_re(R"(func_(\d{10,}))");
    result = apply_regex(result, func_decimal_re, [&](const std::smatch& m) -> std::string {
        uint64_t addr = std::stoull(m[1].str());
        auto [mod_name, offset] = find_module(addr);

        if (!mod_name.empty()) {
            std::ostringstream ss;
            ss << "func_" << mod_name << "_0x" << std::hex << offset;
            return ss.str();
        }
        return "func_" + format_hex(addr);
    });

    // 2. Fix RAM references: iRam/pcRam/uRam followed by hex address
    //    Pattern: ([ipcufb]Ram)([0-9a-fA-F]{8,16})
    std::regex ram_re(R"(([ipcufb]Ram)([0-9a-fA-F]{8,16}))");
    result = apply_regex(result, ram_re, [&](const std::smatch& m) -> std::string {
        std::string prefix = m[1].str();
        uint64_t addr = std::stoull(m[2].str(), nullptr, 16);
        auto [mod_name, offset] = find_module(addr);

        if (!mod_name.empty()) {
            std::ostringstream ss;
            ss << prefix << "_" << mod_name << "_0x" << std::hex << offset;
            return ss.str();
        }
        return prefix + "_0x" + m[2].str();
    });

    // 3. Fix function calls: func_0x<hex>() -> func_<module>_0x<offset>()
    //    Pattern: func_0x([0-9a-fA-F]{8,16})
    std::regex func_hex_re(R"(func_0x([0-9a-fA-F]{8,16}))");
    result = apply_regex(result, func_hex_re, [&](const std::smatch& m) -> std::string {
        uint64_t addr = std::stoull(m[1].str(), nullptr, 16);
        auto [mod_name, offset] = find_module(addr);

        if (!mod_name.empty()) {
            std::ostringstream ss;
            ss << "func_" << mod_name << "_0x" << std::hex << offset;
            return ss.str();
        }
        return m.str();
    });

    return result;
}

} // anonymous namespace

namespace orpheus::mcp {

std::string MCPServer::HandleDisassemble(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        int count = req.value("count", 20);

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: cannot disassemble NULL (0x0)");
        }
        if (count <= 0) {
            return CreateErrorResponse("Invalid count: must be at least 1");
        }
        if (count > 1000) {
            return CreateErrorResponse("Count too large: maximum is 1000 instructions");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        auto data = dma->ReadMemory(pid, address, count * 16);
        if (data.empty()) {
            std::stringstream err;
            err << "Failed to read code at " << FormatAddress(address)
                << " in process " << proc_info->name
                << " - address may point to invalid, unmapped, or non-executable memory";
            return CreateErrorResponse(err.str());
        }

        analysis::Disassembler disasm(true);  // x64
        auto instructions = disasm.Disassemble(data, address);

        json result;
        result["address"] = FormatAddress(address);
        result["context"] = FormatAddressWithContext(pid, address);
        result["count"] = std::min((int)instructions.size(), count);

        json instrs = json::array();
        for (size_t i = 0; i < std::min((size_t)count, instructions.size()); i++) {
            const auto& instr = instructions[i];
            json inst;

            // Compact format: addr, bytes, text, and optional target
            inst["addr"] = FormatAddress(instr.address);
            inst["bytes"] = analysis::disasm::FormatBytes(instr.bytes);
            inst["text"] = instr.mnemonic + (instr.operands.empty() ? "" : " " + instr.operands);

            // Only add type marker for control flow instructions
            if (instr.is_call) inst["type"] = "call";
            else if (instr.is_ret) inst["type"] = "ret";
            else if (instr.is_jump) inst["type"] = instr.is_conditional ? "jcc" : "jmp";

            // Resolve call/jump targets with context
            if ((instr.is_call || instr.is_jump) && instr.branch_target.has_value()) {
                inst["target"] = FormatAddressWithContext(pid, *instr.branch_target);
            }

            instrs.push_back(inst);
        }
        result["instructions"] = instrs;

        // Summary for agent convenience
        int calls = 0, jumps = 0, rets = 0;
        for (const auto& instr : instructions) {
            if (instr.is_call) calls++;
            if (instr.is_jump) jumps++;
            if (instr.is_ret) rets++;
        }
        result["summary"] = {
            {"total", result["count"]},
            {"calls", calls},
            {"jumps", jumps},
            {"returns", rets}
        };

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleDecompile(const std::string& body) {
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        std::string func_name = req.value("function_name", "");

        // New parameter for Level 3 integration: inject CS2 schema types
        bool inject_schema = req.value("inject_schema", false);

        // Optional: specify 'this' type for field name resolution
        std::string this_type = req.value("this_type", "");

        // Optional: max_instructions limit for flow analysis
        // Increase for large functions that hit "Flow exceeded maximum allowable instructions"
        // Default 0 = use library default (100000)
        uint32_t max_instructions = req.value("max_instructions", 0);

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: cannot decompile NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // Initialize decompiler if needed
        static std::unique_ptr<Decompiler> decompiler;
        static bool decompiler_init = false;
        static bool schema_types_injected = false;

        if (!decompiler_init) {
            decompiler = std::make_unique<Decompiler>();
            DecompilerConfig config;

            // Get SLEIGH specs from RuntimeManager (extracted to AppData)
            auto sleigh_dir = RuntimeManager::Instance().GetSleighDirectory();
            if (!sleigh_dir.empty() && std::filesystem::exists(sleigh_dir)) {
                config.sleigh_spec_path = sleigh_dir.string();
            } else {
                return CreateErrorResponse("SLEIGH specs not found - decompiler unavailable");
            }

            config.processor = "x86";
            config.address_size = 64;
            config.little_endian = true;
            config.compiler_spec = "windows";

            if (!decompiler->Initialize(config)) {
                return CreateErrorResponse("Failed to initialize decompiler: " + decompiler->GetLastError());
            }
            decompiler_init = true;
            schema_types_injected = false;
        }

        // Set up memory callback for this request
        uint32_t capture_pid = pid;
        decompiler->SetMemoryCallback([dma, capture_pid](uint64_t addr, size_t size, uint8_t* buffer) -> bool {
            auto data = dma->ReadMemory(capture_pid, addr, static_cast<uint32_t>(size));
            if (data.size() >= size) {
                memcpy(buffer, data.data(), size);
                return true;
            }
            return false;
        });

        // Level 3 Integration: Inject CS2 schema types if requested
        int types_injected = 0;
        if (inject_schema && !schema_types_injected) {
            if (cs2_schema_) {
                auto* schema_dumper = static_cast<orpheus::dumper::CS2SchemaDumper*>(cs2_schema_);
                if (schema_dumper->IsInitialized()) {
                    auto schema_classes = schema_dumper->DumpAllDeduplicated();
                    if (!schema_classes.empty()) {
                        types_injected = decompiler->InjectSchemaTypes(schema_classes);
                        if (types_injected > 0) {
                            schema_types_injected = true;
                        }
                    }
                }
            }
        }

        // Decompile the function
        auto result = decompiler->DecompileFunction(address, func_name, this_type, max_instructions);

        json response;
        response["success"] = result.success;
        response["address"] = FormatAddress(address);
        response["context"] = FormatAddressWithContext(pid, address);
        response["function_name"] = result.function_name;

        // Include schema injection info
        if (inject_schema) {
            response["schema_injected"] = schema_types_injected;
            response["types_injected"] = decompiler->GetInjectedTypeCount();
        }

        // Include this_type info if specified
        if (!this_type.empty()) {
            response["this_type"] = this_type;
        }

        // Include max_instructions if specified
        if (max_instructions > 0) {
            response["max_instructions"] = max_instructions;
        }

        if (result.success) {
            // Post-process the decompiler output for better readability
            // Get cached modules for address context resolution
            std::vector<ModuleInfo> modules;
            {
                std::lock_guard<std::mutex> lock(modules_mutex_);
                if (cached_modules_pid_ != pid) {
                    cached_modules_ = dma->GetModuleList(pid);
                    cached_modules_pid_ = pid;
                }
                modules = cached_modules_;
            }

            // Apply post-processing to convert addresses to module+offset format
            std::string processed_code = PostProcessDecompilerOutput(result.c_code, modules);
            response["c_code"] = processed_code;
            response["warnings"] = result.warnings;
        } else {
            response["error"] = result.error;
        }

        return CreateSuccessResponse(response.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
#else
    return CreateErrorResponse("Decompiler not available - build with -DORPHEUS_BUILD_DECOMPILER=ON");
#endif
}

std::string MCPServer::HandleDumpModule(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        std::string module_name = req["module"];
        std::string output_path = req.value("output", module_name + ".dump");

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto mod_opt = dma->GetModuleByName(pid, module_name);
        if (!mod_opt) {
            return CreateErrorResponse("Module not found: " + module_name);
        }

        auto data = dma->ReadMemory(pid, mod_opt->base_address, mod_opt->size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read module memory");
        }

        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) {
            return CreateErrorResponse("Failed to open output file: " + output_path);
        }

        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        file.close();

        json result;
        result["module"] = module_name;
        result["base"] = FormatAddress(mod_opt->base_address);
        result["size"] = mod_opt->size;
        result["output"] = output_path;
        result["bytes_written"] = data.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGenerateSignature(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        std::string address_str = req["address"];
        uint64_t address = std::stoull(address_str, nullptr, 16);

        // Optional parameters
        size_t size = req.value("size", 64);
        size_t instruction_count = req.value("instruction_count", 0);

        // Signature options
        analysis::SignatureOptions options;
        options.wildcard_rip_relative = req.value("wildcard_rip_relative", true);
        options.wildcard_calls = req.value("wildcard_calls", true);
        options.wildcard_jumps = req.value("wildcard_jumps", true);
        options.wildcard_large_immediates = req.value("wildcard_large_immediates", true);
        options.min_unique_bytes = req.value("min_unique_bytes", 8);
        options.max_length = req.value("max_length", 64);

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Read bytes
        size_t read_size = instruction_count > 0 ? std::max(size, instruction_count * 15) : size;
        read_size = std::min(read_size, size_t(256)); // Cap at 256 bytes
        auto data = dma->ReadMemory(pid, address, read_size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read memory at " + address_str);
        }

        // Generate signature
        analysis::SignatureGenerator generator;
        analysis::SignatureResult sig;

        if (instruction_count > 0) {
            sig = generator.GenerateFromInstructions(data, address, instruction_count, options);
        } else {
            sig = generator.Generate(data, address, options);
        }

        // Build response
        json result;
        result["address"] = FormatAddress(address);
        result["pattern"] = sig.pattern;
        result["pattern_ida"] = analysis::SignatureGenerator::FormatIDA(sig);
        result["pattern_ce"] = analysis::SignatureGenerator::FormatCE(sig);
        result["mask"] = sig.pattern_mask;
        result["length"] = sig.length;
        result["instruction_count"] = sig.instruction_count;
        result["unique_bytes"] = sig.unique_bytes;
        result["uniqueness_ratio"] = sig.uniqueness_ratio;
        result["is_valid"] = sig.is_valid;

        if (!sig.is_valid) {
            result["warning"] = "Signature may not be unique enough (less than " +
                               std::to_string(options.min_unique_bytes) + " unique bytes)";
        }

        // Add module context if available
        auto ctx = ResolveAddressContext(pid, address);
        if (ctx.resolved) {
            result["module"] = ctx.module_name;
            result["module_offset"] = FormatAddress(ctx.offset);
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
