#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "utils/logger.h"
#include <cstdio>

namespace orpheus::ui {

void Application::RenderMemoryViewer() {
    ImGui::Begin("Memory", &panels_.memory_viewer);

    if (!dma_ || !dma_->IsConnected() || selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to view memory");
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(form_width::Short);
    if (ImGui::InputText("Address", address_input_, sizeof(address_input_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        memory_address_ = strtoull(address_input_, nullptr, 16);
        memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 256);
    }

    ImGui::SameLine();
    if (ImGui::Button("Read")) {
        memory_address_ = strtoull(address_input_, nullptr, 16);
        memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 512);
    }

    ImGui::SameLine();
    ImGui::Checkbox("ASCII", &show_ascii_);

    ImGui::Separator();

    if (!memory_data_.empty()) {
        if (font_mono_) ImGui::PushFont(font_mono_);

        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        ImGui::BeginChild("##HexView", ImVec2(0, 0), true);

        ImGuiListClipper clipper;
        int total_rows = (int)(memory_data_.size() + bytes_per_row_ - 1) / bytes_per_row_;
        clipper.Begin(total_rows, row_height);

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                size_t offset = row * bytes_per_row_;
                uint64_t addr = memory_address_ + offset;

                char line_buf[256];
                int pos = 0;

                // Address
                pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%016llX  ", (unsigned long long)addr);

                // Hex bytes with grouping
                for (int col = 0; col < bytes_per_row_; col++) {
                    size_t idx = offset + col;
                    if (idx < memory_data_.size()) {
                        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%02X ", memory_data_[idx]);
                    } else {
                        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "   ");
                    }
                    if (col == 7) {
                        line_buf[pos++] = ' ';
                    }
                }

                // ASCII section
                if (show_ascii_) {
                    line_buf[pos++] = ' ';
                    line_buf[pos++] = '|';
                    for (int col = 0; col < bytes_per_row_; col++) {
                        size_t idx = offset + col;
                        if (idx < memory_data_.size()) {
                            char c = static_cast<char>(memory_data_[idx]);
                            line_buf[pos++] = (c >= 32 && c < 127) ? c : '.';
                        } else {
                            line_buf[pos++] = ' ';
                        }
                    }
                    line_buf[pos++] = '|';
                }
                line_buf[pos] = '\0';

                // Color the address portion differently
                ImGui::PushStyleColor(ImGuiCol_Text, colors::Muted);
                ImGui::TextUnformatted(line_buf, line_buf + 18);
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(line_buf + 18);
            }
        }

        ImGui::EndChild();
        if (font_mono_) ImGui::PopFont();
    } else {
        EmptyState("Enter an address and click Read");
    }

    ImGui::End();
}

} // namespace orpheus::ui
