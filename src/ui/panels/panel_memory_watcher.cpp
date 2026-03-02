#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/memory_watcher.h"
#include "utils/logger.h"
#include <cstdio>
#include <algorithm>
#include <chrono>

namespace orpheus::ui {

void Application::RenderMemoryWatcher() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory Watcher", &panels_.memory_watcher);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to watch memory");
        ImGui::End();
        return;
    }

    // Initialize or recreate memory watcher for current process
    if (!memory_watcher_ || watcher_pid_ != selected_pid_) {
        if (memory_watcher_) {
            memory_watcher_->StopAutoScan();
        }
        memory_watcher_ = std::make_unique<analysis::MemoryWatcher>(
            [this](uint64_t addr, size_t size) -> std::vector<uint8_t> {
                return dma_->ReadMemory(selected_pid_, addr, size);
            }
        );
        watcher_pid_ = selected_pid_;
        LOG_INFO("Memory watcher initialized for PID {}", selected_pid_);
    }

    // Status header
    ImGui::BeginChild("WatcherHeader", ImVec2(0, 80), true);
    {
        bool is_scanning = memory_watcher_->IsScanning();
        if (is_scanning) {
            StatusBadge("SCANNING", colors::Success);
        } else {
            StatusBadge("STOPPED", colors::Muted);
        }
        ImGui::SameLine(100);
        ImGui::Text("Process: %s (PID: %u)", selected_process_name_.c_str(), selected_pid_);
        ImGui::SameLine(400);
        ImGui::Text("Total Changes: %zu", memory_watcher_->GetTotalChangeCount());

        ImGui::Spacing();

        // Scan controls
        if (is_scanning) {
            if (DangerButton("Stop Auto-Scan", ImVec2(120, 0))) {
                memory_watcher_->StopAutoScan();
            }
        } else {
            if (SuccessButton("Start Auto-Scan", ImVec2(120, 0))) {
                memory_watcher_->StartAutoScan(watch_scan_interval_);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Single Scan", ImVec2(100, 0))) {
            memory_watcher_->Scan();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("##Interval", &watch_scan_interval_, 10, 100)) {
            watch_scan_interval_ = std::max(10, std::min(10000, watch_scan_interval_));
        }
        ImGui::SameLine();
        ImGui::Text("ms");
        ImGui::SameLine(500);
        if (ImGui::Button("Clear History", ImVec2(100, 0))) {
            memory_watcher_->ClearHistory();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Two-column layout: Watches on left, History on right
    float panel_width = ImGui::GetContentRegionAvail().x;

    // Left panel: Watch list and Add form
    ImGui::BeginChild("WatchesPanel", ImVec2(panel_width * 0.5f - 5, 0), true);
    {
        GroupLabel("Add Watch");

        ImGui::SetNextItemWidth(form_width::Short);
        ImGui::InputText("Address##WatchAddr", watch_addr_input_, sizeof(watch_addr_input_),
                         ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputText("Size##WatchSize", watch_size_input_, sizeof(watch_size_input_),
                         ImGuiInputTextFlags_CharsDecimal);

        ImGui::SetNextItemWidth(form_width::Short);
        ImGui::InputText("Name##WatchName", watch_name_input_, sizeof(watch_name_input_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        const char* watch_types[] = { "Read", "Write", "ReadWrite", "Value" };
        ImGui::Combo("Type##WatchType", &watch_type_index_, watch_types, IM_ARRAYSIZE(watch_types));

        if (AccentButton("Add Watch", ImVec2(100, 0))) {
            auto parsed = ParseHexAddress(watch_addr_input_);
            size_t size = static_cast<size_t>(atoi(watch_size_input_));
            if (parsed && size > 0 && size <= 1024) {
                uint64_t addr = *parsed;
                analysis::WatchType type = static_cast<analysis::WatchType>(watch_type_index_);
                std::string name = strlen(watch_name_input_) > 0 ? watch_name_input_ : "";
                memory_watcher_->AddWatch(addr, size, type, name);
                watch_addr_input_[0] = '\0';
                watch_name_input_[0] = '\0';
                LOG_INFO("Added watch at 0x{:X} ({} bytes)", addr, size);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All", ImVec2(80, 0))) {
            memory_watcher_->ClearAllWatches();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Watch list
        auto watches = memory_watcher_->GetWatches();
        ImGui::Text("Watches (%zu)", watches.size());
        ImGui::Separator();

        if (ImGui::BeginTable("WatchTable", 6, layout::kStandardTableFlags, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("En", ImGuiTableColumnFlags_WidthFixed, 25);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Changes", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();

            uint32_t watch_idx = 1;
            for (const auto& watch : watches) {
                ImGui::TableNextRow();
                ImGui::PushID(watch_idx);

                // Enabled checkbox
                ImGui::TableNextColumn();
                bool enabled = watch.enabled;
                if (ImGui::Checkbox("##En", &enabled)) {
                    memory_watcher_->SetWatchEnabled(watch_idx, enabled);
                }

                // Address
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                ImGui::Text("%s", FormatAddress(watch.address));
                if (font_mono_) ImGui::PopFont();

                // Context menu
                if (ImGui::BeginPopupContextItem("##WatchContext")) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = watch.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)watch.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, watch.address, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        char addr_str[32];
                        FormatAddressBuf(addr_str, sizeof(addr_str), watch.address);
                        ImGui::SetClipboardText(addr_str);
                    }
                    ImGui::EndPopup();
                }

                // Size
                ImGui::TableNextColumn();
                ImGui::Text("%zu", watch.size);

                // Name
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", watch.name.c_str());

                // Change count
                ImGui::TableNextColumn();
                if (watch.change_count > 0) {
                    ImGui::TextColored(colors::Warning, "%u", watch.change_count);
                } else {
                    ImGui::TextDisabled("0");
                }

                // Actions
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X##Remove")) {
                    memory_watcher_->RemoveWatch(watch_idx);
                }
                HelpTooltip("Remove watch");

                ImGui::PopID();
                watch_idx++;
            }

            ImGui::EndTable();
        }

        // Current values display
        if (!watches.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            GroupLabel("Current Values");

            for (size_t i = 0; i < watches.size() && i < 5; i++) {
                const auto& watch = watches[i];
                if (!watch.last_value.empty()) {
                    ImGui::Text("%s: ", watch.name.c_str());
                    ImGui::SameLine();
                    if (font_mono_) ImGui::PushFont(font_mono_);

                    ImGui::TextWrapped("%s", FormatHexBytes(watch.last_value));

                    // Show as common types if size matches
                    if (watch.size == 4 && watch.last_value.size() >= 4) {
                        int32_t val32 = *reinterpret_cast<const int32_t*>(watch.last_value.data());
                        float valf = *reinterpret_cast<const float*>(watch.last_value.data());
                        ImGui::SameLine();
                        ImGui::TextDisabled("(i32: %d, f32: %.3f)", val32, valf);
                    } else if (watch.size == 8 && watch.last_value.size() >= 8) {
                        int64_t val64 = *reinterpret_cast<const int64_t*>(watch.last_value.data());
                        double vald = *reinterpret_cast<const double*>(watch.last_value.data());
                        ImGui::SameLine();
                        ImGui::TextDisabled("(i64: %lld, f64: %.3f)", (long long)val64, vald);
                    }

                    if (font_mono_) ImGui::PopFont();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: Change history
    ImGui::BeginChild("HistoryPanel", ImVec2(0, 0), true);
    {
        auto recent_changes = memory_watcher_->GetRecentChanges(100);

        GroupLabel("Change History");
        ImGui::Text("(%zu entries)", recent_changes.size());
        ImGui::Separator();

        if (recent_changes.empty()) {
            EmptyState("No changes detected", "Start scanning to track memory changes");
        } else if (ImGui::BeginTable("ChangeTable", 4, layout::kStandardTableFlags, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Old Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("New Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // Show most recent changes first
            for (auto it = recent_changes.rbegin(); it != recent_changes.rend(); ++it) {
                const auto& change = *it;
                ImGui::TableNextRow();

                // Timestamp
                ImGui::TableNextColumn();
                auto time_t = std::chrono::system_clock::to_time_t(change.timestamp);
                std::tm tm;
#ifdef _WIN32
                localtime_s(&tm, &time_t);
#else
                localtime_r(&time_t, &tm);
#endif
                ImGui::Text("%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

                // Address
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                ImGui::Text("%s", FormatAddress(change.address));
                if (font_mono_) ImGui::PopFont();

                // Context menu
                ImGui::PushID(static_cast<int>(std::distance(recent_changes.rbegin(), it)));
                if (ImGui::BeginPopupContextItem("##ChangeContext")) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = change.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)change.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, change.address, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_EYE " Add Watch Here", "Add Watch Here"))) {
                        snprintf(watch_addr_input_, sizeof(watch_addr_input_), "%llX", (unsigned long long)change.address);
                        snprintf(watch_size_input_, sizeof(watch_size_input_), "%zu", change.new_value.size());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                // Old value
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                ImGui::TextWrapped("%s", FormatHexBytes(change.old_value, 8));
                if (font_mono_) ImGui::PopFont();

                // New value (highlighted green)
                ImGui::TableNextColumn();
                if (font_mono_) ImGui::PushFont(font_mono_);
                ImGui::TextColored(colors::Success, "%s", FormatHexBytes(change.new_value, 8));
                if (font_mono_) ImGui::PopFont();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace orpheus::ui
