#include "ui/application.h"
#include "ui/panel_helpers.h"
#include <imgui.h>

namespace orpheus::ui {

void Application::RenderAboutDialog() {
    if (BeginCenteredModal("About Orpheus", &show_about_)) {
        ImGui::Text("Orpheus - DMA Reversing Framework");
        ImGui::Separator();
        ImGui::Text("Version: 0.1.7");
        ImGui::Spacing();
        ImGui::TextWrapped("A comprehensive DMA-based reverse engineering tool for security research.");
        ImGui::Spacing();
        ImGui::TextDisabled("For educational and authorized security research only.");

        if (DialogCloseButton()) {
            show_about_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
