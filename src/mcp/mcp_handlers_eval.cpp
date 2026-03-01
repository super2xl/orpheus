/**
 * MCP Handlers - Expression Evaluation
 *
 * Expression evaluation handlers:
 * - HandleEvaluateExpression
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "utils/expression_evaluator.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleEvaluateExpression(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        std::string expression = req["expression"];

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid));
        }

        // Get module list for resolution
        std::vector<ModuleInfo> modules;
        {
            std::lock_guard<std::mutex> lock(modules_mutex_);
            if (cached_modules_pid_ != pid) {
                cached_modules_ = dma->GetModuleList(pid);
                cached_modules_pid_ = pid;
            }
            modules = cached_modules_;
        }

        // Create expression evaluator
        utils::ExpressionEvaluator evaluator(
            // Module resolver
            [&modules](const std::string& name) -> std::optional<uint64_t> {
                std::string lower_name = name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                for (const auto& mod : modules) {
                    std::string lower_mod = mod.name;
                    std::transform(lower_mod.begin(), lower_mod.end(), lower_mod.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                    if (lower_mod == lower_name) {
                        return mod.base_address;
                    }
                }
                return std::nullopt;
            },
            // Memory reader (for dereference)
            [dma, pid](uint64_t address) -> std::optional<uint64_t> {
                auto data = dma->ReadMemory(pid, address, 8);
                if (data.size() < 8) {
                    return std::nullopt;
                }
                uint64_t value = 0;
                memcpy(&value, data.data(), 8);
                return value;
            },
            // Register resolver (not available in this context)
            nullptr
        );

        // Evaluate the expression
        auto result = evaluator.Evaluate(expression);

        if (!result) {
            return CreateErrorResponse("Evaluation failed: " + evaluator.GetError());
        }

        // Find containing module for context
        std::string module_context;
        uint64_t module_offset = 0;
        for (const auto& mod : modules) {
            if (*result >= mod.base_address && *result < mod.base_address + mod.size) {
                module_context = mod.name;
                module_offset = *result - mod.base_address;
                break;
            }
        }

        json response;
        response["expression"] = expression;
        response["address"] = FormatAddress(*result);
        response["decimal"] = *result;

        if (!module_context.empty()) {
            response["context"] = {
                {"module", module_context},
                {"offset", module_offset},
                {"formatted", module_context + "+0x" + FormatAddress(module_offset).substr(2)}
            };
        }

        LOG_INFO("Evaluated '{}' = 0x{:X}", expression, *result);

        return CreateSuccessResponse(response.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
