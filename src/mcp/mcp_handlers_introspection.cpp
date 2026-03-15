/**
 * MCP Handlers - Introspection
 *
 * Process and memory metadata handlers:
 * - HandleGetProcesses
 * - HandleGetModules
 * - HandleGetMemoryRegions
 */

#include "mcp_server.h"
#include "core/orpheus_core.h"
#include "core/dma_interface.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleGetProcesses(const std::string&) {
    try {
        auto* dma = core_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto processes = dma->GetProcessList();

        json result;
        result["count"] = processes.size();

        json procs = json::array();
        for (const auto& proc : processes) {
            json p;
            p["pid"] = proc.pid;
            p["name"] = proc.name;
            p["is_64bit"] = proc.is_64bit;

            std::stringstream ss;
            ss << "0x" << std::hex << proc.base_address;
            p["base"] = ss.str();

            procs.push_back(p);
        }
        result["processes"] = procs;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetModules(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];

        auto* dma = core_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto modules = dma->GetModuleList(pid);

        json result;
        result["pid"] = pid;
        result["count"] = modules.size();

        json mods = json::array();
        for (const auto& mod : modules) {
            json m;
            m["name"] = mod.name;

            std::stringstream ss_base, ss_entry;
            ss_base << "0x" << std::hex << mod.base_address;
            ss_entry << "0x" << std::hex << mod.entry_point;

            m["base"] = ss_base.str();
            m["size"] = mod.size;
            m["entry"] = ss_entry.str();

            mods.push_back(m);
        }
        result["modules"] = mods;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleGetMemoryRegions(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];

        auto* dma = core_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        auto regions = dma->GetMemoryRegions(pid);

        json result;
        result["pid"] = pid;
        result["count"] = regions.size();

        json regs = json::array();
        for (const auto& reg : regions) {
            json r;
            r["base"] = FormatAddress(reg.base_address);
            r["size"] = reg.size;
            r["size_hex"] = FormatAddress(reg.size);
            r["protection"] = reg.protection;
            r["type"] = reg.type;
            r["info"] = reg.info;
            regs.push_back(r);
        }
        result["regions"] = regs;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
