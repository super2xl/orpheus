/**
 * MCP Handlers - Memory Diff Engine
 *
 * Memory snapshot and diff handlers:
 * - HandleMemorySnapshot: Take a snapshot of a memory region
 * - HandleMemorySnapshotList: List all snapshots
 * - HandleMemorySnapshotDelete: Delete a snapshot
 * - HandleMemoryDiff: Compare two snapshots or snapshot vs current memory
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "utils/limits.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleMemorySnapshot(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        std::string address_str = req["address"];
        uint64_t address = std::stoull(address_str, nullptr, 16);
        size_t size = req["size"];
        std::string name = req.value("name", "");

        // Validate size
        if (size > limits::MAX_MEMORY_SNAPSHOT) {
            return CreateErrorResponse("Snapshot size exceeds maximum (16 MB)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected");
        }

        // Read memory
        auto data = dma->ReadMemory(pid, address, size);
        if (data.empty()) {
            return CreateErrorResponse("Failed to read memory at " + address_str);
        }

        // Generate name if not provided
        if (name.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << "snapshot_" << std::hex << address << "_" << time_t;
            name = ss.str();
        }

        // Store snapshot
        MemorySnapshot snapshot;
        snapshot.name = name;
        snapshot.pid = pid;
        snapshot.address = address;
        snapshot.data = std::move(data);
        snapshot.timestamp = std::chrono::system_clock::now();

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            snapshots_[name] = std::move(snapshot);
        }

        json result;
        result["name"] = name;
        result["address"] = FormatAddress(address);
        result["size"] = size;
        result["pid"] = pid;
        result["message"] = "Snapshot created successfully";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleMemorySnapshotList(const std::string& body) {
    try {
        json result;
        json snapshots_array = json::array();

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            for (const auto& [name, snap] : snapshots_) {
                json snap_info;
                snap_info["name"] = name;
                snap_info["address"] = FormatAddress(snap.address);
                snap_info["size"] = snap.data.size();
                snap_info["pid"] = snap.pid;

                // Format timestamp
                auto time_t = std::chrono::system_clock::to_time_t(snap.timestamp);
                std::stringstream ss;
                ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
                snap_info["timestamp"] = ss.str();

                snapshots_array.push_back(snap_info);
            }
        }

        result["snapshots"] = snapshots_array;
        result["count"] = snapshots_array.size();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleMemorySnapshotDelete(const std::string& body) {
    try {
        auto req = json::parse(body);
        std::string name = req["name"];

        bool deleted = false;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            auto it = snapshots_.find(name);
            if (it != snapshots_.end()) {
                snapshots_.erase(it);
                deleted = true;
            }
        }

        if (!deleted) {
            return CreateErrorResponse("Snapshot not found: " + name);
        }

        json result;
        result["name"] = name;
        result["message"] = "Snapshot deleted";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleMemoryDiff(const std::string& body) {
    try {
        auto req = json::parse(body);

        // Two modes:
        // 1. Compare two snapshots: snapshot_a, snapshot_b
        // 2. Compare snapshot vs current: snapshot, pid (optional, uses snapshot's pid)

        std::string mode = req.value("mode", "snapshot_vs_current");
        std::string filter = req.value("filter", "all"); // all, changed, increased, decreased, unchanged
        size_t max_results = req.value("max_results", 1000);
        size_t value_size = req.value("value_size", 4); // 1, 2, 4, or 8 bytes

        if (value_size != 1 && value_size != 2 && value_size != 4 && value_size != 8) {
            return CreateErrorResponse("value_size must be 1, 2, 4, or 8");
        }

        std::vector<uint8_t> data_a, data_b;
        uint64_t base_address = 0;
        uint32_t pid = 0;
        std::string snapshot_a_name, snapshot_b_name;

        if (mode == "snapshot_vs_snapshot") {
            // Compare two snapshots
            snapshot_a_name = req["snapshot_a"];
            snapshot_b_name = req["snapshot_b"];

            std::lock_guard<std::mutex> lock(snapshot_mutex_);

            auto it_a = snapshots_.find(snapshot_a_name);
            if (it_a == snapshots_.end()) {
                return CreateErrorResponse("Snapshot not found: " + snapshot_a_name);
            }

            auto it_b = snapshots_.find(snapshot_b_name);
            if (it_b == snapshots_.end()) {
                return CreateErrorResponse("Snapshot not found: " + snapshot_b_name);
            }

            if (it_a->second.address != it_b->second.address) {
                return CreateErrorResponse("Snapshots have different base addresses");
            }

            if (it_a->second.data.size() != it_b->second.data.size()) {
                return CreateErrorResponse("Snapshots have different sizes");
            }

            data_a = it_a->second.data;
            data_b = it_b->second.data;
            base_address = it_a->second.address;
            pid = it_a->second.pid;

        } else {
            // Compare snapshot vs current memory
            std::string snapshot_name = req["snapshot"];

            {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                auto it = snapshots_.find(snapshot_name);
                if (it == snapshots_.end()) {
                    return CreateErrorResponse("Snapshot not found: " + snapshot_name);
                }

                data_a = it->second.data;
                base_address = it->second.address;
                pid = req.value("pid", static_cast<int>(it->second.pid));
                snapshot_a_name = snapshot_name;
            }

            // Read current memory
            auto* dma = app_->GetDMA();
            if (!dma || !dma->IsConnected()) {
                return CreateErrorResponse("DMA not connected");
            }

            data_b = dma->ReadMemory(pid, base_address, data_a.size());
            if (data_b.empty()) {
                return CreateErrorResponse("Failed to read current memory");
            }

            snapshot_b_name = "current";
        }

        // Perform diff
        json differences = json::array();
        size_t result_count = 0;
        size_t total_checked = 0;
        size_t total_changed = 0;

        for (size_t i = 0; i + value_size <= data_a.size() && result_count < max_results; i += value_size) {
            total_checked++;

            // Read values based on size
            int64_t val_a = 0, val_b = 0;

            switch (value_size) {
                case 1:
                    val_a = data_a[i];
                    val_b = data_b[i];
                    break;
                case 2:
                    val_a = *reinterpret_cast<int16_t*>(&data_a[i]);
                    val_b = *reinterpret_cast<int16_t*>(&data_b[i]);
                    break;
                case 4:
                    val_a = *reinterpret_cast<int32_t*>(&data_a[i]);
                    val_b = *reinterpret_cast<int32_t*>(&data_b[i]);
                    break;
                case 8:
                    val_a = *reinterpret_cast<int64_t*>(&data_a[i]);
                    val_b = *reinterpret_cast<int64_t*>(&data_b[i]);
                    break;
            }

            bool changed = (val_a != val_b);
            bool increased = (val_b > val_a);
            bool decreased = (val_b < val_a);

            if (changed) total_changed++;

            // Apply filter
            bool include = false;
            std::string change_type;

            if (filter == "all") {
                include = true;
                change_type = changed ? (increased ? "increased" : "decreased") : "unchanged";
            } else if (filter == "changed" && changed) {
                include = true;
                change_type = increased ? "increased" : "decreased";
            } else if (filter == "increased" && increased) {
                include = true;
                change_type = "increased";
            } else if (filter == "decreased" && decreased) {
                include = true;
                change_type = "decreased";
            } else if (filter == "unchanged" && !changed) {
                include = true;
                change_type = "unchanged";
            }

            if (include) {
                json diff_entry;
                diff_entry["address"] = FormatAddress(base_address + i);
                diff_entry["offset"] = i;
                diff_entry["old_value"] = val_a;
                diff_entry["new_value"] = val_b;
                diff_entry["change"] = change_type;

                if (changed) {
                    diff_entry["delta"] = val_b - val_a;
                }

                differences.push_back(diff_entry);
                result_count++;
            }
        }

        json result;
        result["base_address"] = FormatAddress(base_address);
        result["snapshot_a"] = snapshot_a_name;
        result["snapshot_b"] = snapshot_b_name;
        result["value_size"] = value_size;
        result["filter"] = filter;
        result["total_values_checked"] = total_checked;
        result["total_changed"] = total_changed;
        result["results_returned"] = result_count;
        result["differences"] = differences;

        if (result_count >= max_results) {
            result["truncated"] = true;
            result["message"] = "Results truncated at " + std::to_string(max_results);
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
