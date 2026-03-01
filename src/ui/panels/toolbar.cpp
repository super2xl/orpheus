#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/icons.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <future>

namespace orpheus::ui {

void Application::RenderToolbar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float toolbar_height = 32.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbar_height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoDocking |
                              ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.14f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.24f, 1.0f));

    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        float btn_h = 24.0f;

        // --- DMA Connection ---
        bool connected = dma_ && dma_->IsConnected();
        if (dma_connecting_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.4f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.4f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.4f, 0.1f, 1.0f));
            ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Connecting", "Connecting..."), ImVec2(0, btn_h));
            ImGui::PopStyleColor(3);
        } else if (connected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.35f, 0.1f, 1.0f));
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_PLUG " Connected", "Connected"), ImVec2(0, btn_h))) {
                dma_->Close();
                cached_processes_.clear();
                cached_modules_.clear();
                LOG_INFO("DMA disconnected from toolbar");
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                std::string device = dma_->GetDeviceType();
                ImGui::SetTooltip("DMA: %s — click to disconnect", device.c_str());
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_PLUG " Connect", "Connect"), ImVec2(0, btn_h))) {
                if (dma_) {
                    dma_connecting_ = true;
                    LOG_INFO("Connecting to DMA device...");
                    dma_connect_future_ = std::async(std::launch::async, [this]() {
                        return dma_->Initialize("fpga");
                    });
                }
            }
            ImGui::PopStyleColor(3);
            HelpTooltip("Connect to DMA device");
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // --- Process quick-select ---
        if (connected && !cached_processes_.empty()) {
            ImGui::SetNextItemWidth(180.0f);
            const char* preview = selected_pid_ != 0 ? selected_process_name_.c_str() : "Select Process...";
            if (ImGui::BeginCombo("##ProcessSelect", preview, ImGuiComboFlags_HeightLarge)) {
                for (const auto& proc : cached_processes_) {
                    char label[128];
                    snprintf(label, sizeof(label), "%s (%u)", proc.name.c_str(), proc.pid);
                    bool selected = (proc.pid == selected_pid_);
                    if (ImGui::Selectable(label, selected)) {
                        selected_pid_ = proc.pid;
                        selected_process_name_ = proc.name;
                        RefreshModules();

                        if (IsCS2Process() && !cs2_auto_init_attempted_) {
                            InitializeCS2();
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered() && selected_pid_ != 0) {
                ImGui::SetTooltip("PID: %u", selected_pid_);
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(180.0f);
            ImGui::BeginCombo("##ProcessSelect", "No processes", ImGuiComboFlags_None);
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // --- Navigation ---
        {
            bool can_back = CanNavigateBack();
            bool can_fwd = CanNavigateForward();

            if (!can_back) ImGui::BeginDisabled();
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ARROW_LEFT "##back", "<##back"), ImVec2(btn_h, btn_h))) {
                NavigateBack();
            }
            HelpTooltip("Navigate Back (Alt+Left)");
            if (!can_back) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!can_fwd) ImGui::BeginDisabled();
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ARROW_RIGHT "##fwd", ">##fwd"), ImVec2(btn_h, btn_h))) {
                NavigateForward();
            }
            HelpTooltip("Navigate Forward (Alt+Right)");
            if (!can_fwd) ImGui::EndDisabled();
        }

        ImGui::SameLine();

        // --- Go-to address input ---
        ImGui::SetNextItemWidth(160.0f);
        static char goto_input[32] = {};
        if (ImGui::InputText("##toolbar_goto", goto_input, sizeof(goto_input),
                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            uint64_t addr = strtoull(goto_input, nullptr, 16);
            if (addr != 0) {
                NavigateToAddress(addr);
                goto_input[0] = '\0';
            }
        }
        HelpTooltip("Go to address (hex)");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // --- Refresh ---
        bool can_refresh = connected && selected_pid_ != 0;
        if (!can_refresh) ImGui::BeginDisabled();
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE "##refresh", "Refresh"), ImVec2(0, btn_h))) {
            RefreshProcesses();
            if (selected_pid_ != 0) RefreshModules();
        }
        HelpTooltip("Refresh (F5)");
        if (!can_refresh) ImGui::EndDisabled();

        ImGui::SameLine();

        // --- Bookmark current address ---
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_BOOKMARK "##bmark", "Bookmark"), ImVec2(0, btn_h))) {
            panels_.bookmarks = true;
        }
        HelpTooltip("Bookmarks (Ctrl+B)");
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    // Offset the workspace to account for the toolbar
    viewport->WorkPos.y += toolbar_height;
    viewport->WorkSize.y -= toolbar_height;
}

} // namespace orpheus::ui
