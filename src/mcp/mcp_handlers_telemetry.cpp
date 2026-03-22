/**
 * MCP Handlers - Telemetry
 *
 * Telemetry control handlers:
 * - HandleTelemetryStatus
 * - HandleTelemetryConfig
 */

#include "mcp_server.h"
#include "utils/telemetry.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleTelemetryStatus(const std::string&) {
    try {
        auto& telemetry = orpheus::Telemetry::Instance();
        json result;
        result["enabled"] = telemetry.IsEnabled();
        result["endpoint"] = "https://orpheus-telemetry.sdhaf8.workers.dev";
        result["data_sent"] = "version, platform, build type, session duration";
        result["data_not_sent"] = "no user data, no process names, no memory contents, no IPs, no machine identifiers";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleTelemetryConfig(const std::string& body) {
    try {
        auto req = json::parse(body);
        if (!req.contains("enabled") || !req["enabled"].is_boolean()) {
            return CreateErrorResponse("Missing or invalid 'enabled' field (must be boolean)");
        }

        bool enabled = req["enabled"].get<bool>();
        auto& telemetry = orpheus::Telemetry::Instance();
        telemetry.SetEnabled(enabled);

        json result;
        result["enabled"] = telemetry.IsEnabled();
        result["message"] = enabled ? "Telemetry enabled" : "Telemetry disabled";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
