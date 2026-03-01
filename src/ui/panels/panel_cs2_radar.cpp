#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/icons.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "dumper/cs2_schema.h"
#include "utils/logger.h"
#include <GLFW/glfw3.h>
#include <cstdio>

namespace orpheus::ui {

void Application::RenderCS2Radar() {
    ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Radar", &panels_.cs2_radar);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select cs2.exe from the process list");
        ImGui::End();
        return;
    }

    // Toolbar - Row 1: Map selection
    ImGui::Text("Map:");
    ImGui::SameLine();

    ImGui::Checkbox("Auto", &radar_auto_detect_map_);
    HelpTooltip("Auto-detect current map from CS2 memory");
    ImGui::SameLine();

    static const char* maps[] = { "de_dust2", "de_mirage", "de_inferno", "de_nuke",
                                   "de_overpass", "de_ancient", "de_anubis", "de_vertigo",
                                   "cs_office", "ar_shoots" };

    int current_map_idx = 0;
    for (int i = 0; i < IM_ARRAYSIZE(maps); i++) {
        if (radar_current_map_ == maps[i]) {
            current_map_idx = i;
            break;
        }
    }

    ImGui::BeginDisabled(radar_auto_detect_map_);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("##map", &current_map_idx, maps, IM_ARRAYSIZE(maps))) {
        radar_current_map_ = maps[current_map_idx];
        LoadRadarMap(maps[current_map_idx]);
    }
    ImGui::EndDisabled();

    if (radar_auto_detect_map_ && !radar_detected_map_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(colors::Success, "(%s)", radar_detected_map_.c_str());
    }

    // Row 2: Options
    ImGui::Checkbox("Center on local", &radar_center_on_local_);
    ImGui::SameLine();
    ImGui::Checkbox("Names", &radar_show_names_);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-refresh", &radar_auto_refresh_);

    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("##radar_zoom", &radar_zoom_, 0.5f, 3.0f, "%.1fx");

    // Auto-refresh timer
    if (radar_auto_refresh_) {
        radar_refresh_timer_ += ImGui::GetIO().DeltaTime;
        if (radar_refresh_timer_ >= radar_refresh_interval_) {
            RefreshRadarData();
            radar_refresh_timer_ = 0.0f;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Refresh", "Refresh"))) {
        RefreshRadarData();
    }

    ImGui::Separator();

    // Radar canvas
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    // Make it square
    float min_dim = std::min(canvas_size.x, canvas_size.y);
    canvas_size = ImVec2(min_dim, min_dim);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background
    draw_list->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(20, 25, 30, 255));

    // Draw map image if loaded
    if (radar_map_.loaded && radar_map_.texture_id != 0) {
        float img_w = canvas_size.x * radar_zoom_;
        float img_h = canvas_size.y * radar_zoom_;

        ImVec2 img_min(canvas_pos.x + radar_scroll_x_, canvas_pos.y + radar_scroll_y_);
        ImVec2 img_max(img_min.x + img_w, img_min.y + img_h);

        draw_list->PushClipRect(canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

        draw_list->AddImage(
            (ImTextureID)(intptr_t)radar_map_.texture_id,
            img_min, img_max,
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, 200)
        );

        // Center on local player if enabled
        if (radar_center_on_local_ && !radar_players_.empty()) {
            for (const auto& player : radar_players_) {
                if (player.is_local && player.is_alive) {
                    float radar_x = (player.x - radar_map_.pos_x) / radar_map_.scale;
                    float radar_y = (radar_map_.pos_y - player.y) / radar_map_.scale;
                    float norm_x = radar_x / radar_map_.texture_width;
                    float norm_y = radar_y / radar_map_.texture_height;

                    radar_scroll_x_ = canvas_size.x / 2 - norm_x * img_w;
                    radar_scroll_y_ = canvas_size.y / 2 - norm_y * img_h;
                    break;
                }
            }
        }

        // Draw players
        for (const auto& player : radar_players_) {
            if (!player.is_alive) continue;

            ImVec2 pos = WorldToRadar(player.x, player.y, canvas_pos, canvas_size);

            if (pos.x < canvas_pos.x || pos.x > canvas_pos.x + canvas_size.x ||
                pos.y < canvas_pos.y || pos.y > canvas_pos.y + canvas_size.y) {
                continue;
            }

            ImU32 color;
            if (player.is_local) {
                color = IM_COL32(50, 200, 50, 255);
            } else if (player.team == 2) {
                color = IM_COL32(220, 150, 50, 255);
            } else if (player.team == 3) {
                color = IM_COL32(80, 150, 220, 255);
            } else {
                color = IM_COL32(150, 150, 150, 255);
            }

            float radius = player.is_local ? 8.0f : 6.0f;
            draw_list->AddCircleFilled(pos, radius * radar_zoom_, color);
            draw_list->AddCircle(pos, radius * radar_zoom_, IM_COL32(255, 255, 255, 150), 12, 1.5f);

            if (radar_show_names_) {
                ImVec2 text_pos(pos.x + 10, pos.y - 6);
                draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 200), player.name.c_str());
            }
        }

        draw_list->PopClipRect();
    } else {
        // No map loaded - show placeholder
        const char* hint = "Select a map from the dropdown";
        ImVec2 text_size = ImGui::CalcTextSize(hint);
        draw_list->AddText(
            ImVec2(canvas_pos.x + (canvas_size.x - text_size.x) / 2,
                   canvas_pos.y + (canvas_size.y - text_size.y) / 2),
            IM_COL32(100, 100, 100, 200), hint
        );

        // Still draw players without map background
        draw_list->PushClipRect(canvas_pos,
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

        for (const auto& player : radar_players_) {
            if (!player.is_alive) continue;

            float norm_x = (player.x + 4096) / 8192.0f;
            float norm_y = (4096 - player.y) / 8192.0f;
            ImVec2 pos(canvas_pos.x + norm_x * canvas_size.x,
                       canvas_pos.y + norm_y * canvas_size.y);

            ImU32 color = player.is_local ? IM_COL32(50, 200, 50, 255) :
                          (player.team == 2 ? IM_COL32(220, 150, 50, 255) :
                                              IM_COL32(80, 150, 220, 255));
            draw_list->AddCircleFilled(pos, 6.0f, color);

            if (radar_show_names_) {
                draw_list->AddText(ImVec2(pos.x + 10, pos.y - 6),
                                  IM_COL32(255, 255, 255, 200), player.name.c_str());
            }
        }

        draw_list->PopClipRect();
    }

    // Handle mouse input for panning
    ImGui::InvisibleButton("radar_canvas", canvas_size);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        radar_center_on_local_ = false;
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        radar_scroll_x_ += delta.x;
        radar_scroll_y_ += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // Handle zoom with scroll wheel
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            radar_center_on_local_ = false;
            float old_zoom = radar_zoom_;
            radar_zoom_ += wheel * 0.2f;
            radar_zoom_ = std::clamp(radar_zoom_, 0.5f, 3.0f);

            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 mouse_canvas(mouse_pos.x - canvas_pos.x - radar_scroll_x_,
                                mouse_pos.y - canvas_pos.y - radar_scroll_y_);
            float zoom_factor = radar_zoom_ / old_zoom;
            radar_scroll_x_ -= mouse_canvas.x * (zoom_factor - 1.0f);
            radar_scroll_y_ -= mouse_canvas.y * (zoom_factor - 1.0f);
        }
    }

    // Border
    draw_list->AddRect(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(60, 65, 75, 255));

    // Player count
    int alive_count = 0;
    for (const auto& p : radar_players_) if (p.is_alive) alive_count++;
    ImGui::Text("Players: %zu (%d alive)", radar_players_.size(), alive_count);

    ImGui::End();
}

} // namespace orpheus::ui
