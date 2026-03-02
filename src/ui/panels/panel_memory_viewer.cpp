#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "utils/logger.h"
#include "utils/search_history.h"
#include <cstdio>
#include <cstring>

namespace orpheus::ui {

// Helper: render the data inspector sidebar
static void RenderDataInspector(const uint8_t* data, size_t data_size, uint64_t base_addr) {
    ImGui::TextColored(colors::Info, "Data Inspector");
    ImGui::TextDisabled("@ %s", FormatAddress(base_addr));
    ImGui::Separator();

    if (data_size == 0) {
        ImGui::TextDisabled("No data");
        return;
    }

    auto readVal = [&](size_t size) -> bool { return data_size >= size; };

    if (ImGui::BeginTable("##Inspector", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        auto Row = [](const char* label, const char* fmt, ...) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            va_list args;
            va_start(args, fmt);
            ImGui::TextV(fmt, args);
            va_end(args);
        };

        if (readVal(1)) {
            Row("Int8",   "%d", (int)(int8_t)data[0]);
            Row("UInt8",  "%u (0x%02X)", data[0], data[0]);
        }
        if (readVal(2)) {
            int16_t i16; uint16_t u16;
            memcpy(&i16, data, 2); memcpy(&u16, data, 2);
            Row("Int16",  "%d", (int)i16);
            Row("UInt16", "%u (0x%04X)", u16, u16);
        }
        if (readVal(4)) {
            int32_t i32; uint32_t u32; float f32;
            memcpy(&i32, data, 4); memcpy(&u32, data, 4); memcpy(&f32, data, 4);
            Row("Int32",  "%d", i32);
            Row("UInt32", "%u (0x%08X)", u32, u32);
            Row("Float",  "%.6g", f32);
        }
        if (readVal(8)) {
            int64_t i64; uint64_t u64; double f64;
            memcpy(&i64, data, 8); memcpy(&u64, data, 8); memcpy(&f64, data, 8);
            Row("Int64",  "%lld", (long long)i64);
            Row("UInt64", "%llu", (unsigned long long)u64);
            Row("Hex64",  "0x%llX", (unsigned long long)u64);
            Row("Double", "%.6g", f64);

            // Show as pointer if it looks like a valid usermode address
            if (u64 > 0x10000 && u64 < 0x00007FFFFFFFFFFF) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(colors::Info, "Ptr");
                ImGui::TableNextColumn();
                ImGui::TextColored(colors::Info, "0x%llX", (unsigned long long)u64);
            }
        }

        // ASCII string preview
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("ASCII");
        ImGui::TableNextColumn();
        {
            char str_buf[65];
            size_t len = std::min(data_size, (size_t)64);
            size_t str_len = 0;
            for (size_t i = 0; i < len; i++) {
                if (data[i] == 0) break;
                if (data[i] >= 32 && data[i] < 127) {
                    str_buf[str_len++] = (char)data[i];
                } else break;
            }
            str_buf[str_len] = '\0';
            if (str_len > 0) {
                ImGui::Text("\"%s\"", str_buf);
            } else {
                ImGui::TextDisabled("(none)");
            }
        }

        ImGui::EndTable();
    }
}

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
        if (auto parsed = ParseHexAddress(address_input_)) {
            if (search_history_) search_history_->Add("address", address_input_);
            memory_address_ = *parsed;
            memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 256);
        }
    }

    ImGui::SameLine();
    if (search_history_) {
        if (HistoryDropdown("mem_addr", address_input_, sizeof(address_input_),
                            search_history_->Get("address"))) {
            if (auto parsed = ParseHexAddress(address_input_)) {
                memory_address_ = *parsed;
                memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 256);
            }
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Read")) {
        if (auto parsed = ParseHexAddress(address_input_)) {
            if (search_history_) search_history_->Add("address", address_input_);
            memory_address_ = *parsed;
            memory_data_ = dma_->ReadMemory(selected_pid_, memory_address_, 512);
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("ASCII", &show_ascii_);

    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_CROSSHAIRS " Find Writers", "Find Writers"))) {
        snprintf(write_target_input_, sizeof(write_target_input_), "0x%llX",
                 (unsigned long long)memory_address_);
        panels_.write_tracer = true;
    }
    HelpTooltip("Open Write Tracer to find code that writes to this address");

    ImGui::Separator();

    if (!memory_data_.empty()) {
        // Split view: hex dump on left, data inspector on right
        float avail_width = ImGui::GetContentRegionAvail().x;
        float inspector_width = 200.0f;
        float hex_width = avail_width - inspector_width - 8.0f;
        if (hex_width < 400.0f) hex_width = avail_width;  // Too narrow, skip inspector

        if (font_mono_) ImGui::PushFont(font_mono_);

        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        ImGui::BeginChild("##HexView", ImVec2(hex_width, 0), true);

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

        // Data inspector sidebar
        if (hex_width < avail_width) {
            ImGui::SameLine();
            ImGui::BeginChild("##DataInspector", ImVec2(0, 0), true);
            RenderDataInspector(memory_data_.data(), memory_data_.size(), memory_address_);
            ImGui::EndChild();
        }
    } else {
        EmptyState("Enter an address and click Read");
    }

    ImGui::End();
}

} // namespace orpheus::ui
