#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "utils/logger.h"
#include "core/runtime_manager.h"
#include <filesystem>
#include <cstdio>
#include <algorithm>

namespace orpheus::ui {

void Application::RenderCacheManager() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Cache Manager", &panels_.cache_manager);

    // Header
    ImGui::TextColored(colors::Info, "Manage cached analysis data (RTTI, Schema, Functions)");
    ImGui::Separator();
    ImGui::Spacing();

    // Filter section
    ImGui::Text("Type:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    const char* type_items[] = { "All", "RTTI", "Schema", "Functions" };
    if (ImGui::Combo("##cache_type", &cache_selected_type_, type_items, 4)) {
        cache_needs_refresh_ = true;
    }

    ImGui::SameLine();
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputTextWithHint("##cache_filter", "Search...", cache_filter_, sizeof(cache_filter_))) {
        cache_needs_refresh_ = true;
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Refresh", "Refresh"))) {
        cache_needs_refresh_ = true;
    }

    // Refresh cache list if needed
    if (cache_needs_refresh_) {
        cache_entries_.clear();
        cache_needs_refresh_ = false;

        auto cache_base = RuntimeManager::Instance().GetCacheDirectory();

        // Helper to scan a cache directory
        auto scan_cache_dir = [&](const std::string& subdir, const std::string& type) {
            auto dir_path = cache_base / subdir;
            if (!std::filesystem::exists(dir_path)) return;

            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                if (!entry.is_regular_file()) continue;

                std::string name = entry.path().filename().string();
                std::string filter_lower = ToLower(cache_filter_);
                std::string name_lower = ToLower(name);

                // Apply type filter
                if (cache_selected_type_ != 0) {
                    if ((cache_selected_type_ == 1 && type != "rtti") ||
                        (cache_selected_type_ == 2 && type != "schema") ||
                        (cache_selected_type_ == 3 && type != "functions")) {
                        continue;
                    }
                }

                // Apply text filter
                if (!filter_lower.empty() && name_lower.find(filter_lower) == std::string::npos) {
                    continue;
                }

                CacheEntry ce;
                ce.name = name;
                ce.path = entry.path().string();
                ce.size = entry.file_size();
                ce.type = type;

                // Format modification time
                auto ftime = std::filesystem::last_write_time(entry);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
                std::tm tm_val;
                #ifdef _WIN32
                localtime_s(&tm_val, &time_t_val);
                #else
                localtime_r(&time_t_val, &tm_val);
                #endif
                char time_buf[64];
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", &tm_val);
                ce.modified = time_buf;

                cache_entries_.push_back(ce);
            }
        };

        // Scan all cache directories
        scan_cache_dir("rtti", "rtti");
        scan_cache_dir("cs2_schema", "schema");
        scan_cache_dir("functions", "functions");

        // Sort by modification time (newest first)
        std::sort(cache_entries_.begin(), cache_entries_.end(),
            [](const CacheEntry& a, const CacheEntry& b) {
                return a.modified > b.modified;
            });
    }

    // Stats
    size_t total_size = 0;
    for (const auto& entry : cache_entries_) {
        total_size += entry.size;
    }
    ImGui::Text("Files: %zu | Total: %.2f MB", cache_entries_.size(), total_size / (1024.0 * 1024.0));

    ImGui::Separator();

    if (cache_entries_.empty()) {
        EmptyState("No cache files", "Analysis cache files will appear here after scanning");
        ImGui::End();
        return;
    }

    // Cache table
    if (ImGui::BeginTable("##CacheEntries", 5, layout::kSortableTableFlags)) {

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, layout::kColumnSize);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)cache_entries_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& entry = cache_entries_[row];

                ImGui::TableNextRow();
                ImGui::PushID(row);

                // Name column
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.name.c_str());
                HelpTooltip(entry.path.c_str());

                // Type column with color coding
                ImGui::TableNextColumn();
                ImVec4 type_color = colors::Warning;  // Orange for functions
                if (entry.type == "rtti") {
                    type_color = colors::Info;
                } else if (entry.type == "schema") {
                    type_color = colors::Success;
                }
                ImGui::TextColored(type_color, "%s", entry.type.c_str());

                // Size column
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatSize(entry.size));

                // Modified column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", entry.modified.c_str());

                // Actions column
                ImGui::TableNextColumn();
                if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_XMARK " Del", "Delete"))) {
                    ImGui::OpenPopup("Confirm Delete");
                }

                // Confirm delete popup
                if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete cache file '%s'?", entry.name.c_str());
                    ImGui::Spacing();

                    if (DangerButton("Delete", ImVec2(80, 0))) {
                        std::error_code ec;
                        std::filesystem::remove(entry.path, ec);
                        if (!ec) {
                            LOG_INFO("Deleted cache file: {}", entry.path);
                            cache_needs_refresh_ = true;
                        } else {
                            LOG_ERROR("Failed to delete: {}", ec.message());
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    // Bulk actions
    ImGui::Separator();
    if (DangerButton("Clear RTTI Cache")) {
        auto dir_path = RuntimeManager::Instance().GetCacheDirectory() / "rtti";
        if (std::filesystem::exists(dir_path)) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) count++;
            }
            LOG_INFO("Cleared {} RTTI cache files", count);
            cache_needs_refresh_ = true;
        }
    }
    ImGui::SameLine();
    if (DangerButton("Clear Schema Cache")) {
        auto dir_path = RuntimeManager::Instance().GetCacheDirectory() / "cs2_schema";
        if (std::filesystem::exists(dir_path)) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) count++;
            }
            LOG_INFO("Cleared {} Schema cache files", count);
            cache_needs_refresh_ = true;
        }
    }
    ImGui::SameLine();
    if (DangerButton("Clear Function Cache")) {
        auto dir_path = RuntimeManager::Instance().GetCacheDirectory() / "functions";
        if (std::filesystem::exists(dir_path)) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                std::filesystem::remove(entry.path(), ec);
                if (!ec) count++;
            }
            LOG_INFO("Cleared {} Function cache files", count);
            cache_needs_refresh_ = true;
        }
    }

    ImGui::End();
}

} // namespace orpheus::ui
