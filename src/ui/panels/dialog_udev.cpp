#ifdef PLATFORM_LINUX
#include "ui/application.h"
#include "ui/panel_helpers.h"
#include <imgui.h>
#include "utils/logger.h"
#include <fstream>
#include <filesystem>

namespace orpheus::ui {

void Application::RenderUdevPermissionDialog() {
    if (BeginCenteredModal("USB Permission Required", &show_udev_dialog_)) {
        ImGui::TextColored(colors::Warning, "USB Device Access Denied");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Your FPGA device was detected but Orpheus doesn't have permission to access it.\n\n"
            "This can be fixed by installing a udev rule that grants access to your user."
        );
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        KeyValue("Detected Device", (udev_vendor_id_ + ":" + udev_product_id_).c_str());

        ImGui::Spacing();
        ImGui::TextWrapped("The following udev rule will be installed:");
        ImGui::Spacing();

        std::string rule = "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"" + udev_vendor_id_ +
                          "\", ATTR{idProduct}==\"" + udev_product_id_ + "\", MODE=\"0666\"";
        ImGui::PushStyleColor(ImGuiCol_Text, colors::Muted);
        ImGui::TextWrapped("%s", rule.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static bool install_in_progress = false;
        static bool install_success = false;
        static std::string install_message;

        if (install_in_progress) {
            ImGui::TextColored(colors::Warning, "Installing...");
        } else if (!install_message.empty()) {
            if (install_success) {
                ImGui::TextColored(colors::Success, "%s", install_message.c_str());
                ImGui::Spacing();
                ImGui::TextWrapped("Please unplug and replug your device, then try connecting again.");
            } else {
                ErrorText("%s", install_message.c_str());
            }
        }

        ImGui::Spacing();

        if (!install_in_progress && install_message.empty()) {
            if (AccentButton("Install udev Rule", ImVec2(150, 0))) {
                install_in_progress = true;

                std::string rule_escaped = "SUBSYSTEM==\\\"usb\\\", ATTR{idVendor}==\\\"" + udev_vendor_id_ +
                    "\\\", ATTR{idProduct}==\\\"" + udev_product_id_ + "\\\", MODE=\\\"0666\\\"";

                std::string cmd = "pkexec sh -c 'printf \"%s\\n\" \"# Orpheus FPGA DMA device\" \"" +
                    rule_escaped + "\" > /etc/udev/rules.d/99-orpheus-fpga.rules && udevadm control --reload-rules && udevadm trigger'";

                int result = system(cmd.c_str());
                install_in_progress = false;

                if (result == 0) {
                    install_success = true;
                    install_message = "udev rule installed successfully!";
                    LOG_INFO("Installed udev rule for device {}:{}", udev_vendor_id_, udev_product_id_);
                } else {
                    install_success = false;
                    install_message = "Failed to install udev rule. You may need to install it manually.";
                    LOG_ERROR("Failed to install udev rule, exit code: {}", result);
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Close", button_size::Small)) {
            show_udev_dialog_ = false;
            install_message.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
#endif
