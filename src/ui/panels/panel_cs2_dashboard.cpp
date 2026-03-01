#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/icons.h"
#include <imgui.h>
#include "dumper/cs2_schema.h"
#include "utils/logger.h"
#include <cstdio>
#include <GLFW/glfw3.h>

namespace orpheus::ui {

void Application::RenderCS2Dashboard() {
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Dashboard", &panels_.cs2_dashboard);

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

    // Refresh data if empty
    if (radar_players_.empty()) {
        RefreshRadarData();
    }

    // Toolbar
    ImGui::Checkbox("Show all players", &dashboard_show_all_players_);
    ImGui::SameLine();
    ImGui::Checkbox("Show bots", &dashboard_show_bots_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Refresh", "Refresh"))) {
        RefreshRadarData();
    }

    ImGui::Separator();

    // Split into two teams
    std::vector<const RadarPlayer*> team_t, team_ct, spectators;
    const RadarPlayer* local_player = nullptr;

    for (const auto& player : radar_players_) {
        if (player.is_local) local_player = &player;
        if (player.team == 2) team_t.push_back(&player);
        else if (player.team == 3) team_ct.push_back(&player);
        else spectators.push_back(&player);
    }

    // Local player info box
    if (local_player) {
        ImGui::BeginChild("local_player", ImVec2(0, 80), true);

        // Name and team
        ImVec4 team_color = local_player->team == 2 ?
            ImVec4(0.9f, 0.6f, 0.2f, 1.0f) : ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
        ImGui::TextColored(team_color, "%s", local_player->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(You)");

        // Health bar
        float health_pct = local_player->health / 100.0f;
        ImVec4 health_color = health_pct > 0.5f ? colors::Success :
                              health_pct > 0.25f ? colors::Warning : colors::Error;

        ImGui::Text("Health:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, health_color);
        ImGui::ProgressBar(health_pct, ImVec2(150, 18),
                          std::to_string(local_player->health).c_str());
        ImGui::PopStyleColor();

        // Position
        ImGui::Text("Position: %.0f, %.0f, %.0f",
                   local_player->x, local_player->y, local_player->z);

        ImGui::EndChild();
    }

    // Team tables
    float half_width = ImGui::GetContentRegionAvail().x / 2 - 5;

    // Terrorists
    ImGui::BeginChild("team_t", ImVec2(half_width, 0), true);
    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "TERRORISTS (%zu)", team_t.size());
    ImGui::Separator();

    for (const auto* player : team_t) {
        if (!dashboard_show_all_players_ && !player->is_alive) continue;

        ImGui::PushID(player->name.c_str());

        if (player->is_alive) {
            ImGui::TextColored(colors::Success, "*");
        } else {
            ImGui::TextColored(colors::Muted, "X");
        }
        ImGui::SameLine();

        if (player->is_local) {
            ImGui::TextColored(colors::Success, "%s", player->name.c_str());
        } else {
            ImGui::Text("%s", player->name.c_str());
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        float hp = player->health / 100.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            hp > 0.5f ? colors::Success :
            hp > 0.25f ? colors::Warning : colors::Error);
        ImGui::ProgressBar(hp, ImVec2(55, 14), "");
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Counter-Terrorists
    ImGui::BeginChild("team_ct", ImVec2(half_width, 0), true);
    ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "COUNTER-TERRORISTS (%zu)", team_ct.size());
    ImGui::Separator();

    for (const auto* player : team_ct) {
        if (!dashboard_show_all_players_ && !player->is_alive) continue;

        ImGui::PushID(player->name.c_str());

        if (player->is_alive) {
            ImGui::TextColored(colors::Success, "*");
        } else {
            ImGui::TextColored(colors::Muted, "X");
        }
        ImGui::SameLine();

        if (player->is_local) {
            ImGui::TextColored(colors::Success, "%s", player->name.c_str());
        } else {
            ImGui::Text("%s", player->name.c_str());
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        float hp = player->health / 100.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            hp > 0.5f ? colors::Success :
            hp > 0.25f ? colors::Warning : colors::Error);
        ImGui::ProgressBar(hp, ImVec2(55, 14), "");
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace orpheus::ui
