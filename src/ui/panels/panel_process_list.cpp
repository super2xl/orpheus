#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "utils/logger.h"
#include <algorithm>

namespace orpheus::ui {

void Application::RenderProcessList() {
    ImGui::Begin("Processes", &panels_.process_list);

    // Auto-refresh logic
    if (auto_refresh_enabled_ && dma_ && dma_->IsConnected()) {
        double current_time = glfwGetTime();
        if (current_time - last_process_refresh_ >= process_refresh_interval_) {
            RefreshProcesses();
            last_process_refresh_ = current_time;
        }
    }

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device to view processes");
        ImGui::End();
        return;
    }

    static char filter[256] = {};
    FilterBar("##filter", filter, sizeof(filter), layout::kFilterButtonReserve);
    ImGui::SameLine();

    // Auto-refresh toggle button
    if (auto_refresh_enabled_) {
        if (SuccessButton(auto_refresh_enabled_ ? "Live" : "Manual")) {
            auto_refresh_enabled_ = false;
        }
    } else {
        if (ImGui::Button("Manual")) {
            auto_refresh_enabled_ = true;
            last_process_refresh_ = glfwGetTime();
        }
    }
    HelpTooltip(auto_refresh_enabled_
        ? "Live refresh enabled - click to switch to manual"
        : "Manual refresh - click to enable auto-refresh");

    ImGui::Separator();

    if (cached_processes_.empty()) {
        EmptyState("No processes found", "Check DMA connection or refresh");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("##ProcessTable", 3, layout::kSortableTableFlags)) {
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, layout::kColumnPID);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Arch", ImGuiTableColumnFlags_WidthFixed, layout::kColumnArch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                process_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                process_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Build filtered list using shared helper
        auto filtered_indices = BuildFilteredIndices<ProcessInfo>(
            cached_processes_, filter,
            [](const ProcessInfo& p) { return p.name; });

        // Sort filtered indices
        auto& procs = cached_processes_;
        int sort_col = process_sort_column_;
        bool ascending = process_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&procs, sort_col, ascending](size_t a, size_t b) {
                const auto& pa = procs[a];
                const auto& pb = procs[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0: cmp = (pa.pid < pb.pid) ? -1 : (pa.pid > pb.pid) ? 1 : 0; break;
                    case 1: cmp = pa.name.compare(pb.name); break;
                    case 2: cmp = (pa.is_64bit == pb.is_64bit) ? 0 : (pa.is_64bit ? 1 : -1); break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Render sorted list
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& proc = cached_processes_[filtered_indices[row]];

                ImGui::TableNextRow();
                bool is_selected = (proc.pid == selected_pid_);

                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);
                if (ImGui::Selectable(std::to_string(proc.pid).c_str(), is_selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_pid_ = proc.pid;
                    selected_process_name_ = proc.name;

                    // Reset CS2 state on process change
                    cs2_auto_init_attempted_ = false;
                    cs2_auto_init_success_ = false;

                    RefreshModules();
                    LOG_INFO("Selected process: {} (PID: {})", proc.name, proc.pid);

                    // Auto-initialize CS2 systems if this is CS2
                    if (IsCS2Process()) {
                        cs2_auto_init_attempted_ = true;
                        InitializeCS2();
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy PID", "Copy PID"))) {
                        char pid_str[16];
                        snprintf(pid_str, sizeof(pid_str), "%u", proc.pid);
                        ImGui::SetClipboardText(pid_str);
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Name", "Copy Name"))) {
                        ImGui::SetClipboardText(proc.name.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_PUZZLE_PIECE " View Modules", "View Modules"))) {
                        selected_pid_ = proc.pid;
                        selected_process_name_ = proc.name;
                        RefreshModules();
                        panels_.module_list = true;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::Text("%s", proc.name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(proc.is_64bit ? colors::Info : colors::Warning,
                                  "%s", proc.is_64bit ? "x64" : "x86");
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace orpheus::ui
