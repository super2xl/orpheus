#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "utils/logger.h"
#include <cstdio>

namespace orpheus::ui {

void Application::RenderXRefFinder() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("XRef Finder", &panels_.xref_finder);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to find cross-references");
        ImGui::End();
        return;
    }

    // Target address input
    ImGui::Text("Find cross-references to:");
    ImGui::SetNextItemWidth(form_width::Short);
    ImGui::InputTextWithHint("##xref_target", "Target address (hex)", xref_target_input_, sizeof(xref_target_input_),
                              ImGuiInputTextFlags_CharsHexadecimal);

    ImGui::Separator();

    // Scan range selection
    ImGui::Text("Scan Range:");
    ImGui::Checkbox("Use selected module", &xref_use_module_);

    if (xref_use_module_) {
        if (selected_module_base_ != 0) {
            ImGui::SameLine();
            ImGui::TextColored(colors::Info, "%s", selected_module_name_.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("@ %s (0x%X bytes)", FormatAddress(selected_module_base_), selected_module_size_);
        } else {
            ImGui::SameLine();
            ImGui::TextColored(colors::Warning, "No module selected");
        }
    } else {
        ImGui::SetNextItemWidth(form_width::Short);
        ImGui::InputTextWithHint("##xref_base", "Base address (hex)", xref_base_input_, sizeof(xref_base_input_),
                                  ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputTextWithHint("##xref_size", "Size (hex)", xref_size_input_, sizeof(xref_size_input_),
                                  ImGuiInputTextFlags_CharsHexadecimal);
    }

    ImGui::Separator();

    // Scan button
    bool can_scan = false;
    if (xref_use_module_) {
        can_scan = selected_module_base_ != 0 && strlen(xref_target_input_) > 0;
    } else {
        can_scan = strlen(xref_target_input_) > 0 && strlen(xref_base_input_) > 0 && strlen(xref_size_input_) > 0;
    }

    if (!can_scan || xref_scanning_) ImGui::BeginDisabled();
    if (ImGui::Button("Find XRefs")) {
        xref_scanning_ = true;
        xref_results_.clear();

        uint64_t target = strtoull(xref_target_input_, nullptr, 16);
        uint64_t base = 0;
        uint32_t size = 0;

        if (xref_use_module_) {
            base = selected_module_base_;
            size = selected_module_size_;
        } else {
            base = strtoull(xref_base_input_, nullptr, 16);
            size = static_cast<uint32_t>(strtoull(xref_size_input_, nullptr, 16));
        }

        if (target != 0 && base != 0 && size != 0) {
            auto data = dma_->ReadMemory(selected_pid_, base, size);
            if (!data.empty()) {
                // Scan for direct 64-bit pointer references
                for (size_t i = 0; i + 8 <= data.size() && xref_results_.size() < 1000; i++) {
                    uint64_t val = *reinterpret_cast<uint64_t*>(&data[i]);
                    if (val == target) {
                        XRefResult ref;
                        ref.address = base + i;
                        ref.type = "ptr64";

                        bool found_module = false;
                        for (const auto& mod : cached_modules_) {
                            if (ref.address >= mod.base_address && ref.address < mod.base_address + mod.size) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s+0x%llX", mod.name.c_str(),
                                         (unsigned long long)(ref.address - mod.base_address));
                                ref.context = buf;
                                found_module = true;
                                break;
                            }
                        }
                        if (!found_module) {
                            char buf[32];
                            FormatAddressBuf(buf, sizeof(buf), ref.address);
                            ref.context = buf;
                        }

                        xref_results_.push_back(ref);
                    }
                }

                // Scan for 32-bit relative offsets (RIP-relative)
                for (size_t i = 0; i + 4 <= data.size() && xref_results_.size() < 1000; i++) {
                    int32_t rel = *reinterpret_cast<int32_t*>(&data[i]);
                    uint64_t computed = base + i + 4 + rel;
                    if (computed == target) {
                        XRefResult ref;
                        ref.address = base + i;
                        ref.type = "rel32";

                        bool found_module = false;
                        for (const auto& mod : cached_modules_) {
                            if (ref.address >= mod.base_address && ref.address < mod.base_address + mod.size) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s+0x%llX", mod.name.c_str(),
                                         (unsigned long long)(ref.address - mod.base_address));
                                ref.context = buf;
                                found_module = true;
                                break;
                            }
                        }
                        if (!found_module) {
                            char buf[32];
                            FormatAddressBuf(buf, sizeof(buf), ref.address);
                            ref.context = buf;
                        }

                        xref_results_.push_back(ref);
                    }
                }

                LOG_INFO("XRef scan found {} references to 0x{:X}", xref_results_.size(), target);
            }
        }

        xref_scanning_ = false;
    }
    if (!can_scan || xref_scanning_) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        xref_results_.clear();
    }

    // Results header
    ImGui::Separator();
    ImGui::Text("Results: %zu", xref_results_.size());

    if (xref_results_.empty()) {
        ImGui::End();
        return;
    }

    // Results table
    if (ImGui::BeginTable("##XRefResults", 3, layout::kStandardTableFlags)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, layout::kColumnProtection);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)xref_results_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& ref = xref_results_[row];

                ImGui::TableNextRow();
                ImGui::PushID(row);

                // Address column
                ImGui::TableNextColumn();
                char addr_buf[32];
                FormatAddressBuf(addr_buf, sizeof(addr_buf), ref.address);
                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(ref.address);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                        disasm_address_ = ref.address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)ref.address);
                        auto code = dma_->ReadMemory(selected_pid_, ref.address, 1024);
                        if (!code.empty() && disassembler_) {
                            disasm_instructions_ = disassembler_->Disassemble(code, ref.address);
                        }
                        panels_.disassembly = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = ref.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)ref.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, ref.address, 256);
                        panels_.memory_viewer = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Context", "Copy Context"))) {
                        ImGui::SetClipboardText(ref.context.c_str());
                    }
                    ImGui::EndPopup();
                }

                // Type column - color-coded
                ImGui::TableNextColumn();
                ImVec4 type_color = ref.type == "ptr64" ? colors::Info : colors::Warning;
                ImGui::TextColored(type_color, "%s", ref.type.c_str());

                // Context column
                ImGui::TableNextColumn();
                ImGui::Text("%s", ref.context.c_str());

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace orpheus::ui
