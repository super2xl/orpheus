/**
 * MCP Handlers - Emulation
 *
 * Unicorn emulator handlers:
 * - HandleEmuCreate
 * - HandleEmuDestroy
 * - HandleEmuMapModule
 * - HandleEmuMapRegion
 * - HandleEmuSetRegisters
 * - HandleEmuGetRegisters
 * - HandleEmuRun
 * - HandleEmuRunInstructions
 * - HandleEmuReset
 */

#include "mcp_server.h"
#include "core/orpheus_core.h"
#include "core/dma_interface.h"
#include "emulation/emulator.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleEmuCreate(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];

        // Optional config
        emulation::EmulatorConfig config;
        if (req.contains("stack_base")) {
            config.stack_base = std::stoull(req["stack_base"].get<std::string>(), nullptr, 16);
        }
        if (req.contains("stack_size")) {
            config.stack_size = req["stack_size"];
        }
        if (req.contains("max_instructions")) {
            config.max_instructions = req["max_instructions"];
        }
        if (req.contains("timeout_us")) {
            config.timeout_us = req["timeout_us"];
        }
        if (req.contains("lazy_mapping")) {
            config.lazy_mapping = req["lazy_mapping"];
        }

        auto* dma = core_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Destroy existing emulator if any
        emulator_.reset();

        // Create new emulator
        emulator_ = std::make_unique<emulation::Emulator>();
        if (!emulator_->Initialize(dma, pid, config)) {
            std::string error = emulator_->GetLastError();
            emulator_.reset();
            return CreateErrorResponse("Failed to initialize emulator: " + error);
        }

        emulator_pid_ = pid;

        json result;
        result["pid"] = pid;
        result["stack_base"] = FormatAddress(config.stack_base);
        result["stack_size"] = config.stack_size;
        result["max_instructions"] = config.max_instructions;
        result["timeout_us"] = config.timeout_us;
        result["lazy_mapping"] = config.lazy_mapping;
        result["status"] = "initialized";

        LOG_INFO("Emulator created for PID {}", pid);
        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuDestroy(const std::string&) {
    try {
        if (!emulator_) {
            return CreateErrorResponse("No emulator active");
        }

        uint32_t pid = emulator_pid_;
        emulator_.reset();
        emulator_pid_ = 0;

        json result;
        result["pid"] = pid;
        result["status"] = "destroyed";

        LOG_INFO("Emulator destroyed for PID {}", pid);
        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuMapModule(const std::string& body) {
    try {
        auto req = json::parse(body);
        std::string module_name = req["module"];

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        if (!emulator_->MapModule(module_name)) {
            return CreateErrorResponse("Failed to map module: " + emulator_->GetLastError());
        }

        json result;
        result["module"] = module_name;
        result["status"] = "mapped";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuMapRegion(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        size_t size = req["size"];

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        if (!emulator_->MapRegion(address, size)) {
            return CreateErrorResponse("Failed to map region: " + emulator_->GetLastError());
        }

        json result;
        result["address"] = FormatAddress(address);
        result["size"] = size;
        result["status"] = "mapped";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuSetRegisters(const std::string& body) {
    try {
        auto req = json::parse(body);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        json set_regs = json::object();

        // Process register values from request
        if (req.contains("registers")) {
            for (auto& [key, value] : req["registers"].items()) {
                uint64_t reg_value;
                if (value.is_string()) {
                    reg_value = std::stoull(value.get<std::string>(), nullptr, 16);
                } else {
                    reg_value = value.get<uint64_t>();
                }

                auto reg = emulation::ParseRegister(key);
                if (!reg) {
                    return CreateErrorResponse("Unknown register: " + key);
                }

                if (!emulator_->SetRegister(*reg, reg_value)) {
                    return CreateErrorResponse("Failed to set " + key + ": " + emulator_->GetLastError());
                }

                set_regs[key] = FormatAddress(reg_value);
            }
        }

        json result;
        result["registers_set"] = set_regs;
        result["status"] = "ok";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuGetRegisters(const std::string& body) {
    try {
        auto req = json::parse(body);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        json result;
        json regs = json::object();

        // If specific registers requested, get only those
        if (req.contains("registers") && req["registers"].is_array()) {
            for (const auto& name : req["registers"]) {
                std::string reg_name = name.get<std::string>();
                auto reg = emulation::ParseRegister(reg_name);
                if (reg) {
                    auto value = emulator_->GetRegister(*reg);
                    if (value) {
                        regs[reg_name] = FormatAddress(*value);
                    }
                }
            }
        } else {
            // Get all GP registers
            const std::vector<std::string> gp_regs = {
                "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
                "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
                "rip", "rflags"
            };

            for (const auto& name : gp_regs) {
                auto reg = emulation::ParseRegister(name);
                if (reg) {
                    auto value = emulator_->GetRegister(*reg);
                    if (value) {
                        regs[name] = FormatAddress(*value);
                    }
                }
            }
        }

        result["registers"] = regs;
        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuRun(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint64_t start_address = std::stoull(req["start_address"].get<std::string>(), nullptr, 16);
        uint64_t end_address = std::stoull(req["end_address"].get<std::string>(), nullptr, 16);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        auto emu_result = emulator_->Run(start_address, end_address);

        json result;
        result["success"] = emu_result.success;
        result["start_address"] = FormatAddress(start_address);
        result["end_address"] = FormatAddress(end_address);
        result["final_rip"] = FormatAddress(emu_result.final_rip);
        result["instructions_executed"] = emu_result.instructions_executed;

        if (!emu_result.success) {
            result["error"] = emu_result.error;
        }

        // Include register state
        json regs = json::object();
        for (const auto& [name, value] : emu_result.registers) {
            regs[name] = FormatAddress(value);
        }
        result["registers"] = regs;

        // Include accessed pages info
        auto& accessed = emulator_->GetAccessedPages();
        result["pages_accessed"] = accessed.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuRunInstructions(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint64_t start_address = std::stoull(req["start_address"].get<std::string>(), nullptr, 16);
        size_t count = req["count"];

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        auto emu_result = emulator_->RunInstructions(start_address, count);

        json result;
        result["success"] = emu_result.success;
        result["start_address"] = FormatAddress(start_address);
        result["requested_count"] = count;
        result["final_rip"] = FormatAddress(emu_result.final_rip);
        result["instructions_executed"] = emu_result.instructions_executed;

        if (!emu_result.success) {
            result["error"] = emu_result.error;
        }

        // Include register state
        json regs = json::object();
        for (const auto& [name, value] : emu_result.registers) {
            regs[name] = FormatAddress(value);
        }
        result["registers"] = regs;

        // Include accessed pages info
        auto& accessed = emulator_->GetAccessedPages();
        result["pages_accessed"] = accessed.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleEmuReset(const std::string& body) {
    try {
        auto req = json::parse(body);
        bool full_reset = req.value("full", false);

        if (!emulator_) {
            return CreateErrorResponse("No emulator active - call emu_create first");
        }

        if (full_reset) {
            emulator_->Reset();
        } else {
            emulator_->ResetCPU();
        }

        json result;
        result["reset_type"] = full_reset ? "full" : "cpu_only";
        result["status"] = "ok";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
