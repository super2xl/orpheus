#include "task_manager.h"
#include <iomanip>
#include <sstream>
#include <random>

namespace orpheus::core {

nlohmann::json TaskInfo::ToJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["type"] = type;
    j["description"] = description;
    j["state"] = TaskStateToString(state);
    j["progress"] = progress;
    j["status_message"] = status_message;

    // Timestamps as ISO strings
    auto format_time = [](std::chrono::steady_clock::time_point tp) -> std::string {
        if (tp == std::chrono::steady_clock::time_point{}) return "";
        auto sys_time = std::chrono::system_clock::now() +
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                tp - std::chrono::steady_clock::now());
        auto time_t = std::chrono::system_clock::to_time_t(sys_time);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
        return ss.str();
    };

    j["created_at"] = format_time(created_at);
    if (started_at != std::chrono::steady_clock::time_point{}) {
        j["started_at"] = format_time(started_at);
    }
    if (completed_at != std::chrono::steady_clock::time_point{}) {
        j["completed_at"] = format_time(completed_at);

        // Add duration
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            completed_at - started_at);
        j["duration_ms"] = duration.count();
    } else if (started_at != std::chrono::steady_clock::time_point{}) {
        // Running task - show elapsed time
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        j["elapsed_ms"] = elapsed.count();
    }

    if (result.has_value()) {
        j["result"] = *result;
    }
    if (error.has_value()) {
        j["error"] = *error;
    }

    return j;
}

TaskManager& TaskManager::Instance() {
    static TaskManager instance;
    return instance;
}

TaskManager::TaskManager() = default;

TaskManager::~TaskManager() {
    // Wait for all running tasks to complete
    std::vector<std::shared_ptr<Task>> running_tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, task] : tasks_) {
            if (task->info.state == TaskState::Running ||
                task->info.state == TaskState::Pending) {
                task->cancel_token->Cancel();
                running_tasks.push_back(task);
            }
        }
    }

    for (auto& task : running_tasks) {
        if (task->future.valid()) {
            task->future.wait_for(std::chrono::seconds(5));
        }
    }
}

std::string TaskManager::GenerateTaskId() {
    uint64_t counter = task_counter_.fetch_add(1);

    // Generate random suffix
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::ostringstream ss;
    ss << "task_" << std::hex << std::setfill('0')
       << std::setw(4) << (counter & 0xFFFF)
       << "_" << std::setw(8) << dist(gen);
    return ss.str();
}

std::string TaskManager::StartTask(const std::string& type,
                                    const std::string& description,
                                    TaskFunction function) {
    auto task = std::make_shared<Task>();
    task->info.id = GenerateTaskId();
    task->info.type = type;
    task->info.description = description;
    task->info.state = TaskState::Pending;
    task->info.progress = 0.0f;
    task->info.created_at = std::chrono::steady_clock::now();
    task->function = std::move(function);
    task->cancel_token = std::make_shared<CancellationToken>();

    std::string task_id = task->info.id;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_[task_id] = task;
    }

    // Launch task in a detached thread
    task->future = std::async(std::launch::async, [this, task]() {
        ExecuteTask(task);
    });

    return task_id;
}

void TaskManager::ExecuteTask(std::shared_ptr<Task> task) {
    // Update state to running
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task->info.state = TaskState::Running;
        task->info.started_at = std::chrono::steady_clock::now();
    }

    // Progress callback that updates task info
    auto progress_cb = [this, task](float progress, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        task->info.progress = std::clamp(progress, 0.0f, 1.0f);
        if (!message.empty()) {
            task->info.status_message = message;
        }
    };

    try {
        // Check for early cancellation
        if (task->cancel_token->IsCancelled()) {
            std::lock_guard<std::mutex> lock(mutex_);
            task->info.state = TaskState::Cancelled;
            task->info.completed_at = std::chrono::steady_clock::now();
            task->info.status_message = "Cancelled before start";
            return;
        }

        // Execute the task function
        nlohmann::json result = task->function(task->cancel_token, progress_cb);

        // Check if cancelled during execution
        if (task->cancel_token->IsCancelled()) {
            std::lock_guard<std::mutex> lock(mutex_);
            task->info.state = TaskState::Cancelled;
            task->info.completed_at = std::chrono::steady_clock::now();
            task->info.status_message = "Cancelled";
            return;
        }

        // Success
        std::lock_guard<std::mutex> lock(mutex_);
        task->info.state = TaskState::Completed;
        task->info.progress = 1.0f;
        task->info.completed_at = std::chrono::steady_clock::now();
        task->info.result = std::move(result);
        task->info.status_message = "Completed";

    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (task->cancel_token->IsCancelled()) {
            task->info.state = TaskState::Cancelled;
            task->info.status_message = "Cancelled";
        } else {
            task->info.state = TaskState::Failed;
            task->info.error = e.what();
            task->info.status_message = "Failed: " + std::string(e.what());
        }
        task->info.completed_at = std::chrono::steady_clock::now();
    }
}

std::optional<TaskInfo> TaskManager::GetTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    return it->second->info;
}

bool TaskManager::CancelTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }

    auto& task = it->second;
    if (task->info.state == TaskState::Pending ||
        task->info.state == TaskState::Running) {
        task->cancel_token->Cancel();
        return true;
    }
    return false;  // Already completed/failed/cancelled
}

std::vector<TaskInfo> TaskManager::ListTasks(std::optional<TaskState> state_filter) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TaskInfo> result;
    for (const auto& [id, task] : tasks_) {
        if (!state_filter.has_value() || task->info.state == *state_filter) {
            result.push_back(task->info);
        }
    }

    // Sort by creation time (newest first)
    std::sort(result.begin(), result.end(),
        [](const TaskInfo& a, const TaskInfo& b) {
            return a.created_at > b.created_at;
        });

    return result;
}

void TaskManager::CleanupTasks(std::chrono::seconds max_age) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = tasks_.begin(); it != tasks_.end(); ) {
        const auto& task = it->second;
        bool is_finished = task->info.state == TaskState::Completed ||
                          task->info.state == TaskState::Failed ||
                          task->info.state == TaskState::Cancelled;

        if (is_finished) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - task->info.completed_at);
            if (age > max_age) {
                it = tasks_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

TaskManager::TaskCounts TaskManager::GetTaskCounts() {
    std::lock_guard<std::mutex> lock(mutex_);

    TaskCounts counts;
    for (const auto& [id, task] : tasks_) {
        counts.total++;
        switch (task->info.state) {
            case TaskState::Pending: counts.pending++; break;
            case TaskState::Running: counts.running++; break;
            case TaskState::Completed: counts.completed++; break;
            case TaskState::Failed: counts.failed++; break;
            case TaskState::Cancelled: counts.cancelled++; break;
        }
    }
    return counts;
}

} // namespace orpheus::core
