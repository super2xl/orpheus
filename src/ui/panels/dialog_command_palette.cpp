#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include <imgui.h>

namespace orpheus::ui {

void Application::RenderCommandPalette() {
    // Command palette uses special positioning (upper-center, like VS Code)
    ImGui::OpenPopup("Command Palette");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(layout::kDialogMedium);

    if (ImGui::BeginPopupModal("Command Palette", &show_command_palette_,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##cmd_search", "Type a command...", command_search_, sizeof(command_search_));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("##CmdList", ImVec2(0, 0), false);

        std::string search_lower = ToLower(command_search_);
        float desc_col = ImGui::GetContentRegionAvail().x * 0.55f;

        for (const auto& kb : keybinds_) {
            if (MatchesFilter(kb.name, search_lower)) {
                if (ImGui::Selectable(kb.name.c_str())) {
                    if (kb.action) kb.action();
                    show_command_palette_ = false;
                }
                ImGui::SameLine(desc_col);
                ImGui::TextDisabled("%s", kb.description.c_str());
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
