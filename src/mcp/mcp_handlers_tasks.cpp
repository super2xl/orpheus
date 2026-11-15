#include "mcp_server.h"
#include "../core/task_manager.h"
#include <nlohmann/json.hpp>

namespace orpheus::mcp {

using json = nlohmann::json;
using namespace orpheus::core;

std::string MCPServer::HandleTaskStatus(const std::string& body) {
    try {
        json request = json::parse(body);

        if (!request.contains("task_id") || !request["task_id"].is_string()) {
            throw std::runtime_error("Missing required parameter: task_id");
        }

        std::string task_id = request["task_id"].get<std::string>();

        auto& tm = TaskManager::Instance();
        auto task_info = tm.GetTask(task_id);

        if (!task_info) {
            throw std::runtime_error("Task not found: " + task_id);
        }

        return task_info->ToJson().dump();
    } catch (const std::exception& e) {
        return CreateErrorResponse(e.what());
    }
}

std::string MCPServer::HandleTaskCancel(const std::string& body) {
    try {
        json request = json::parse(body);

        if (!request.contains("task_id") || !request["task_id"].is_string()) {
            throw std::runtime_error("Missing required parameter: task_id");
        }

        std::string task_id = request["task_id"].get<std::string>();

        auto& tm = TaskManager::Instance();

        // Get current state first
        auto task_info = tm.GetTask(task_id);
        if (!task_info) {
            throw std::runtime_error("Task not found: " + task_id);
        }

        bool cancelled = tm.CancelTask(task_id);

        json result;
        result["task_id"] = task_id;
        result["cancelled"] = cancelled;

        if (!cancelled) {
            result["reason"] = "Task already " + TaskStateToString(task_info->state);
        }

        return result.dump();
    } catch (const std::exception& e) {
        return CreateErrorResponse(e.what());
    }
}

std::string MCPServer::HandleTaskList(const std::string& body) {
    try {
        json request = json::parse(body);

        auto& tm = TaskManager::Instance();

        // Optional state filter
        std::optional<TaskState> state_filter;
        if (request.contains("state") && request["state"].is_string()) {
            std::string state_str = request["state"].get<std::string>();
            if (state_str == "pending") state_filter = TaskState::Pending;
            else if (state_str == "running") state_filter = TaskState::Running;
            else if (state_str == "completed") state_filter = TaskState::Completed;
            else if (state_str == "failed") state_filter = TaskState::Failed;
            else if (state_str == "cancelled") state_filter = TaskState::Cancelled;
        }

        auto tasks = tm.ListTasks(state_filter);
        auto counts = tm.GetTaskCounts();

        json result;
        result["tasks"] = json::array();
        for (const auto& task : tasks) {
            result["tasks"].push_back(task.ToJson());
        }

        result["counts"] = {
            {"pending", counts.pending},
            {"running", counts.running},
            {"completed", counts.completed},
            {"failed", counts.failed},
            {"cancelled", counts.cancelled},
            {"total", counts.total}
        };

        return result.dump();
    } catch (const std::exception& e) {
        return CreateErrorResponse(e.what());
    }
}

std::string MCPServer::HandleTaskCleanup(const std::string& body) {
    try {
        json request = json::parse(body);

        int max_age_seconds = 300;  // Default 5 minutes
        if (request.contains("max_age_seconds") && request["max_age_seconds"].is_number()) {
            max_age_seconds = request["max_age_seconds"].get<int>();
        }

        auto& tm = TaskManager::Instance();

        // Get counts before
        auto before = tm.GetTaskCounts();

        tm.CleanupTasks(std::chrono::seconds(max_age_seconds));

        // Get counts after
        auto after = tm.GetTaskCounts();

        json result;
        result["removed"] = before.total - after.total;
        result["remaining"] = after.total;

        return result.dump();
    } catch (const std::exception& e) {
        return CreateErrorResponse(e.what());
    }
}

} // namespace orpheus::mcp
