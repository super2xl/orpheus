#include "ui/application.h"
#include "ui/panel_helpers.h"
#include <imgui.h>
#include <cstdio>

namespace orpheus::ui {

void Application::RenderGotoDialog() {
    if (BeginCenteredModal("Goto Address", &show_goto_dialog_)) {
        static char goto_addr[32] = {};

        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(form_width::Normal);
        bool enter_pressed = ImGui::InputText("Address", goto_addr, sizeof(goto_addr),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();
        if (ImGui::Button("Go", button_size::Small) || enter_pressed) {
            auto addr = ParseHexAddress(goto_addr);
            if (addr) {
                NavigateToAddress(*addr);
            }
            show_goto_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", button_size::Small)) {
            show_goto_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
