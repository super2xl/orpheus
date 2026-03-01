#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "core/task_manager.h"
#include "utils/logger.h"
#include <cstdio>
#include <GLFW/glfw3.h>
#include <chrono>

namespace orpheus::ui {

void Application::RenderTaskManager() {
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Task Manager", &panels_.task_manager);

    auto& tm = core::TaskManager::Instance();
    auto counts = tm.GetTaskCounts();

    // Header with counts
    ImGui::TextColored(colors::Info, "Background Tasks");
    ImGui::SameLine();
    ImGui::TextDisabled("(Running: %zu, Pending: %zu, Completed: %zu, Failed: %zu)",
        counts.running, counts.pending, counts.completed, counts.failed);

    ImGui::Separator();
    ImGui::Spacing();

    // Controls
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    const char* filter_items[] = { "All", "Running", "Completed", "Failed" };
    ImGui::Combo("##task_filter", &task_filter_status_, filter_items, 4);

    ImGui::SameLine();
    ImGui::Checkbox("Auto-refresh", &task_list_auto_refresh_);

    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Refresh", "Refresh"))) {
        task_refresh_timer_ = task_refresh_interval_;
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_BROOM " Cleanup Old", "Cleanup Old"))) {
        tm.CleanupTasks(std::chrono::seconds(60));
        LOG_INFO("Cleaned up old tasks");
    }

    // Auto-refresh logic
    if (task_list_auto_refresh_) {
        task_refresh_timer_ += ImGui::GetIO().DeltaTime;
        if (task_refresh_timer_ >= task_refresh_interval_) {
            task_refresh_timer_ = 0.0f;
        }
    }

    // Get task list with optional filter
    std::optional<core::TaskState> state_filter;
    if (task_filter_status_ == 1) state_filter = core::TaskState::Running;
    else if (task_filter_status_ == 2) state_filter = core::TaskState::Completed;
    else if (task_filter_status_ == 3) state_filter = core::TaskState::Failed;

    auto tasks = tm.ListTasks(state_filter);

    ImGui::Separator();

    if (tasks.empty()) {
        EmptyState("No tasks", "Background tasks will appear here");
        ImGui::End();
        return;
    }

    ImGui::Text("Tasks: %zu", tasks.size());

    // Task table
    if (ImGui::BeginTable("##TaskTable", 6, layout::kStandardTableFlags)) {

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < tasks.size(); i++) {
            const auto& task = tasks[i];

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // ID column
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", task.id.c_str());
            HelpTooltip(("Task ID: " + task.id + "\nDescription: " + task.description).c_str());

            // Type column
            ImGui::TableNextColumn();
            ImGui::Text("%s", task.type.c_str());

            // Status column with color
            ImGui::TableNextColumn();
            ImVec4 status_color = colors::Muted;
            switch (task.state) {
                case core::TaskState::Pending:   status_color = colors::Muted;   break;
                case core::TaskState::Running:   status_color = colors::Info;    break;
                case core::TaskState::Completed: status_color = colors::Success; break;
                case core::TaskState::Failed:    status_color = colors::Error;   break;
                case core::TaskState::Cancelled: status_color = colors::Warning; break;
            }
            ImGui::TextColored(status_color, "%s", core::TaskStateToString(task.state).c_str());

            // Progress column
            ImGui::TableNextColumn();
            if (task.state == core::TaskState::Running || task.state == core::TaskState::Pending) {
                ImGui::ProgressBar(task.progress, ImVec2(-1, 0),
                    task.progress > 0 ? nullptr : "...");
            } else if (task.state == core::TaskState::Completed) {
                ImGui::ProgressBar(1.0f, ImVec2(-1, 0), "Done");
            } else if (task.state == core::TaskState::Failed) {
                ImGui::ProgressBar(task.progress, ImVec2(-1, 0), "Failed");
            } else {
                ImGui::ProgressBar(task.progress, ImVec2(-1, 0), "Cancelled");
            }

            // Message column
            ImGui::TableNextColumn();
            if (!task.status_message.empty()) {
                ImGui::Text("%s", task.status_message.c_str());
            } else if (task.error) {
                ImGui::TextColored(colors::Error, "%s", task.error->c_str());
            } else {
                ImGui::TextDisabled("-");
            }

            // Actions column
            ImGui::TableNextColumn();
            if (task.state == core::TaskState::Running || task.state == core::TaskState::Pending) {
                if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_XMARK, "Cancel"))) {
                    if (tm.CancelTask(task.id)) {
                        LOG_INFO("Cancelled task: {}", task.id);
                    }
                }
            } else {
                if (task.state == core::TaskState::Completed && task.result) {
                    if (ImGui::SmallButton("View")) {
                        ImGui::OpenPopup("Task Result");
                    }

                    if (ImGui::BeginPopup("Task Result")) {
                        ImGui::Text("Task: %s (%s)", task.type.c_str(), task.id.c_str());
                        ImGui::Separator();

                        std::string result_str = task.result->dump(2);
                        if (result_str.length() > 2000) {
                            result_str = result_str.substr(0, 2000) + "\n... (truncated)";
                        }
                        ImGui::TextWrapped("%s", result_str.c_str());

                        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy", "Copy"))) {
                            ImGui::SetClipboardText(task.result->dump(2).c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Close")) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                } else {
                    ImGui::TextDisabled("-");
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // Bottom section - quick stats
    ImGui::Separator();
    ImGui::Text("Total: %zu | Running: %zu | Pending: %zu | Completed: %zu | Failed: %zu | Cancelled: %zu",
        counts.total, counts.running, counts.pending, counts.completed, counts.failed, counts.cancelled);

    ImGui::End();
}

} // namespace orpheus::ui
