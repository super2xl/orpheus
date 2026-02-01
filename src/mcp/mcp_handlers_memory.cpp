/**
 * MCP Handlers - Memory
 *
 * Core memory access handlers:
 * - HandleReadMemory
 * - HandleWriteMemory
 * - HandleResolvePointerChain
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "utils/limits.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleReadMemory(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        size_t size = req["size"];
        std::string format = req.value("format", "auto");  // auto, hex, bytes, hexdump

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer (0x0)");
        }
        if (size == 0) {
            return CreateErrorResponse("Invalid size: cannot read 0 bytes");
        }
        if (size > limits::MAX_MEMORY_READ) {
            return CreateErrorResponse("Size too large: maximum read is 16MB");
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

        auto data = dma->ReadMemory(pid, address, size);
        if (data.empty()) {
            std::stringstream err;
            err << "Failed to read memory at " << FormatAddress(address)
                << " (size: " << size << " bytes) in process " << proc_info->name
                << " - address may be invalid, unmapped, or protected";
            return CreateErrorResponse(err.str());
        }

        json result;
        result["address"] = FormatAddress(address);
        result["context"] = FormatAddressWithContext(pid, address);
        result["size"] = data.size();

        // Smart format selection based on size and request
        bool use_hex = (format == "hex") || (format == "auto" && data.size() <= 64);
        bool use_bytes = (format == "bytes");
        bool use_hexdump = (format == "hexdump") || (format == "auto" && data.size() > 64);

        if (use_hex || use_hexdump) {
            // Compact hex string
            std::stringstream hex_compact;
            for (uint8_t b : data) {
                hex_compact << std::hex << std::setw(2) << std::setfill('0') << (int)b;
            }
            result["hex"] = hex_compact.str();
        }

        if (use_bytes) {
            // JSON byte array
            json bytes = json::array();
            for (uint8_t b : data) {
                bytes.push_back(b);
            }
            result["bytes"] = bytes;
        }

        if (use_hexdump) {
            // IDA-style hexdump
            std::stringstream hexdump;
            const size_t bytes_per_line = 16;

            for (size_t i = 0; i < data.size(); i += bytes_per_line) {
                hexdump << std::hex << std::setw(16) << std::setfill('0')
                       << (address + i) << "  ";

                for (size_t j = 0; j < bytes_per_line; j++) {
                    if (i + j < data.size()) {
                        hexdump << std::hex << std::setw(2) << std::setfill('0')
                               << static_cast<int>(data[i + j]) << " ";
                    } else {
                        hexdump << "   ";
                    }
                    if (j == 7) hexdump << " ";
                }

                hexdump << " |";
                for (size_t j = 0; j < bytes_per_line && i + j < data.size(); j++) {
                    uint8_t byte = data[i + j];
                    hexdump << (byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '.');
                }
                hexdump << "|\n";
            }
            result["hexdump"] = hexdump.str();
        }

        // Type interpretations only for small reads
        if (data.size() <= 16) {
            if (data.size() >= 4) {
                result["as_int32"] = *reinterpret_cast<int32_t*>(data.data());
                result["as_float"] = *reinterpret_cast<float*>(data.data());
            }
            if (data.size() >= 8) {
                result["as_int64"] = *reinterpret_cast<int64_t*>(data.data());
                result["as_ptr"] = FormatAddress(*reinterpret_cast<uint64_t*>(data.data()));
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleWriteMemory(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        std::string hex_data = req["data"];

        // Validate parameters
        if (address == 0) {
            return CreateErrorResponse("Invalid address: NULL pointer (0x0)");
        }
        if (hex_data.empty()) {
            return CreateErrorResponse("Invalid data: no bytes to write");
        }
        if (hex_data.length() % 2 != 0) {
            return CreateErrorResponse("Invalid hex data: odd number of characters (must be pairs)");
        }

        // Convert hex string to bytes with validation
        std::vector<uint8_t> data;
        for (size_t i = 0; i < hex_data.length(); i += 2) {
            std::string byte_str = hex_data.substr(i, 2);
            try {
                data.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
            } catch (...) {
                return CreateErrorResponse("Invalid hex data at position " + std::to_string(i) + ": '" + byte_str + "'");
            }
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

        bool success = dma->WriteMemory(pid, address, data);
        if (!success) {
            std::stringstream err;
            err << "Failed to write " << data.size() << " bytes at " << FormatAddress(address)
                << " in process " << proc_info->name
                << " - address may be invalid or memory is protected";
            return CreateErrorResponse(err.str());
        }

        json result;
        result["address"] = req["address"];
        result["bytes_written"] = data.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleResolvePointerChain(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);

        // Offsets can be array of ints or hex strings
        std::vector<int64_t> offsets;
        if (req.contains("offsets")) {
            for (const auto& off : req["offsets"]) {
                if (off.is_string()) {
                    offsets.push_back(std::stoll(off.get<std::string>(), nullptr, 16));
                } else {
                    offsets.push_back(off.get<int64_t>());
                }
            }
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        json result;
        result["base"] = req["base"];

        json chain = json::array();

        // Store pointer values as we traverse to avoid duplicate reads
        std::vector<uint64_t> ptr_values;
        ptr_values.reserve(offsets.size());

        uint64_t current = base;
        chain.push_back({
            {"step", 0},
            {"address", FormatAddress(current)},
            {"context", FormatAddressWithContext(pid, current)},
            {"operation", "base"}
        });

        bool chain_valid = true;
        for (size_t i = 0; i < offsets.size(); i++) {
            // Read pointer at current address
            auto ptr_opt = dma->Read<uint64_t>(pid, current);
            if (!ptr_opt) {
                result["error"] = "Failed to read pointer at step " + std::to_string(i);
                result["failed_at"] = FormatAddress(current);
                result["chain"] = chain;
                chain_valid = false;
                break;
            }

            uint64_t ptr_value = *ptr_opt;
            ptr_values.push_back(ptr_value);  // Store for visualization

            chain.push_back({
                {"step", i + 1},
                {"address", FormatAddress(current)},
                {"value", FormatAddress(ptr_value)},
                {"operation", "deref"}
            });

            // Apply offset
            current = ptr_value + offsets[i];
            chain.push_back({
                {"step", i + 1},
                {"address", FormatAddress(current)},
                {"context", FormatAddressWithContext(pid, current)},
                {"operation", "offset"},
                {"offset", offsets[i]}
            });
        }

        if (!chain_valid) {
            return CreateSuccessResponse(result.dump());
        }

        result["final_address"] = FormatAddress(current);
        result["final_context"] = FormatAddressWithContext(pid, current);
        result["chain"] = chain;

        // Build compact visualization using stored pointer values (no duplicate reads)
        std::stringstream viz;
        viz << FormatAddress(base);

        uint64_t viz_current = base;
        for (size_t i = 0; i < ptr_values.size(); i++) {
            uint64_t ptr_value = ptr_values[i];
            viz << " -> [" << FormatAddress(ptr_value) << "]";

            if (offsets[i] >= 0) {
                viz << " + 0x" << std::hex << offsets[i];
            } else {
                viz << " - 0x" << std::hex << (-offsets[i]);
            }

            viz_current = ptr_value + offsets[i];
        }
        viz << " -> " << FormatAddress(current);
        result["visualization"] = viz.str();

        // Optionally read value at final address
        if (req.value("read_final", false)) {
            int read_size = req.value("read_size", 8);
            auto final_data = dma->ReadMemory(pid, current, read_size);
            if (!final_data.empty()) {
                std::stringstream hex;
                for (uint8_t b : final_data) {
                    hex << std::hex << std::setw(2) << std::setfill('0') << (int)b;
                }
                result["final_value"] = hex.str();

                if (read_size == 4 && final_data.size() == 4) {
                    result["final_as_int32"] = *reinterpret_cast<int32_t*>(final_data.data());
                    result["final_as_float"] = *reinterpret_cast<float*>(final_data.data());
                } else if (read_size == 8 && final_data.size() == 8) {
                    result["final_as_int64"] = *reinterpret_cast<int64_t*>(final_data.data());
                    result["final_as_double"] = *reinterpret_cast<double*>(final_data.data());
                }
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCacheStats(const std::string&) {
    try {
        auto* dma = app_->GetDMA();
        if (!dma) {
            return CreateErrorResponse("DMA interface not available");
        }

        auto stats = dma->GetCacheStats();
        auto config = dma->GetCacheConfig();

        json result;
        result["enabled"] = dma->IsCacheEnabled();
        result["hits"] = stats.hits;
        result["misses"] = stats.misses;
        result["hit_rate"] = stats.HitRate();
        result["evictions"] = stats.evictions;
        result["current_pages"] = stats.current_pages;
        result["current_bytes"] = stats.current_bytes;
        result["max_pages"] = config.max_pages;
        result["ttl_ms"] = config.ttl_ms;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCacheConfig(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* dma = app_->GetDMA();
        if (!dma) {
            return CreateErrorResponse("DMA interface not available");
        }

        // Get current config
        auto config = dma->GetCacheConfig();

        // Update from request
        if (req.contains("enabled")) {
            config.enabled = req["enabled"].get<bool>();
        }
        if (req.contains("max_pages")) {
            config.max_pages = req["max_pages"].get<size_t>();
        }
        if (req.contains("ttl_ms")) {
            config.ttl_ms = req["ttl_ms"].get<uint32_t>();
        }

        dma->SetCacheConfig(config);

        json result;
        result["enabled"] = config.enabled;
        result["max_pages"] = config.max_pages;
        result["ttl_ms"] = config.ttl_ms;
        result["message"] = config.enabled ? "Cache enabled" : "Cache disabled";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCacheClear(const std::string&) {
    try {
        auto* dma = app_->GetDMA();
        if (!dma) {
            return CreateErrorResponse("DMA interface not available");
        }

        auto stats_before = dma->GetCacheStats();
        dma->ClearCache();

        json result;
        result["cleared_pages"] = stats_before.current_pages;
        result["cleared_bytes"] = stats_before.current_bytes;
        result["message"] = "Cache cleared";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
