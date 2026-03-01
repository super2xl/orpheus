#include "ui/application.h"
#include "ui/panel_helpers.h"
#include <imgui.h>
#include "analysis/pe_dumper.h"
#include "utils/logger.h"
#include <filesystem>
#include <cstdio>

namespace orpheus::ui {

void Application::RenderDumpDialog() {
    if (BeginCenteredModal("Dump Module", &show_dump_dialog_)) {
        KeyValue("Module", selected_module_name_.c_str());
        KeyValueHex("Base", selected_module_base_);
        ImGui::Text("Size: 0x%X (%s)", selected_module_size_, FormatSize(selected_module_size_));

        ImGui::Separator();

        // Initialize filename if empty
        if (ImGui::IsWindowAppearing() && dump_filename_[0] == '\0') {
            snprintf(dump_filename_, sizeof(dump_filename_), "%s_dumped.exe", selected_module_name_.c_str());
        }

        ImGui::SetNextItemWidth(form_width::Wide);
        ImGui::InputText("Filename", dump_filename_, sizeof(dump_filename_));

        ImGui::Spacing();
        ImGui::Text("Options:");
        ImGui::Checkbox("Fix PE Headers", &dump_fix_headers_);
        ImGui::Checkbox("Rebuild IAT", &dump_rebuild_iat_);
        ImGui::Checkbox("Unmap Sections (File Alignment)", &dump_unmap_sections_);

        ImGui::Separator();

        if (dump_in_progress_) {
            ProgressBarWithText(dump_progress_, "Dumping...");
            ImGui::TextDisabled("Please wait...");
        } else {
            int action = DialogOkCancelButtons("Dump", "Cancel");
            if (action == 1) {
                DumpModule(selected_module_base_, selected_module_size_, dump_filename_);
                show_dump_dialog_ = false;
                ImGui::CloseCurrentPopup();
            } else if (action == 2) {
                show_dump_dialog_ = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
