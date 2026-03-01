#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/rtti_parser.h"
#include "utils/logger.h"
#include <cstdio>

namespace orpheus::ui {

void Application::RenderVTableReader() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("VTable Reader", &panels_.vtable_reader);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to read vtables");
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored(colors::Info, "Read VTable entries and identify classes via RTTI");
    ImGui::Separator();
    ImGui::Spacing();

    // Input section
    ImGui::Text("VTable Address:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(200);
    bool enter_pressed = ImGui::InputTextWithHint("##vtable_addr", "e.g. 7FF600123456",
        vtable_address_input_, sizeof(vtable_address_input_),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Text("Entry Count:");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("##vtable_count", &vtable_entry_count_);
    vtable_entry_count_ = std::max(1, std::min(vtable_entry_count_, 500));

    ImGui::SameLine();
    ImGui::Checkbox("Disassemble", &vtable_disasm_);
    HelpTooltip("Show first instruction of each function");

    ImGui::Spacing();

    // Read button
    if (AccentButton("Read VTable", ImVec2(120, 0)) || enter_pressed) {
        vtable_entries_.clear();
        vtable_class_name_.clear();
        vtable_error_.clear();

        auto base_opt = ParseHexAddress(vtable_address_input_);
        uint64_t vtable_addr = base_opt ? *base_opt : 0;
        if (!base_opt) {
            vtable_error_ = "Invalid vtable address";
        }

        if (vtable_addr != 0) {
            // Read vtable entries
            size_t read_size = vtable_entry_count_ * 8;  // 8 bytes per pointer
            auto vtable_data = dma_->ReadMemory(selected_pid_, vtable_addr, read_size);

            if (vtable_data.empty()) {
                vtable_error_ = "Failed to read memory at vtable address";
            } else {
                // Parse entries
                for (int i = 0; i < vtable_entry_count_ && (size_t)((i + 1) * 8) <= vtable_data.size(); i++) {
                    VTableEntry entry;
                    entry.address = vtable_addr + i * 8;
                    entry.function = *reinterpret_cast<uint64_t*>(&vtable_data[i * 8]);

                    // Check if function pointer looks valid (non-zero, reasonable range)
                    entry.valid = (entry.function > 0x10000 && entry.function < 0x00007FFFFFFFFFFF);

                    // Build context (module+offset)
                    if (entry.valid) {
                        bool found_module = false;
                        for (const auto& mod : cached_modules_) {
                            if (entry.function >= mod.base_address && entry.function < mod.base_address + mod.size) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s+0x%llX", mod.name.c_str(),
                                         (unsigned long long)(entry.function - mod.base_address));
                                entry.context = buf;
                                found_module = true;
                                break;
                            }
                        }
                        if (!found_module) {
                            entry.context = "(outside modules)";
                        }

                        // Optionally disassemble first instruction
                        if (vtable_disasm_ && disassembler_) {
                            auto code = dma_->ReadMemory(selected_pid_, entry.function, 32);
                            if (!code.empty()) {
                                auto instrs = disassembler_->Disassemble(code, entry.function);
                                if (!instrs.empty()) {
                                    entry.first_instr = instrs[0].mnemonic + " " + instrs[0].operands;
                                }
                            }
                        }
                    }

                    vtable_entries_.push_back(entry);
                }

                // Try to identify class via RTTI (vtable[-1] points to RTTI type info)
                uint64_t rtti_ptr_addr = vtable_addr - 8;
                auto rtti_ptr_opt = dma_->Read<uint64_t>(selected_pid_, rtti_ptr_addr);
                if (rtti_ptr_opt && *rtti_ptr_opt != 0) {
                    uint64_t type_descriptor = *rtti_ptr_opt;

                    auto name_data = dma_->ReadMemory(selected_pid_, type_descriptor + 16, 256);
                    if (!name_data.empty()) {
                        std::string decorated;
                        for (uint8_t c : name_data) {
                            if (c == 0) break;
                            decorated += static_cast<char>(c);
                        }

                        // Simple demangling: .?AVClassName@@ -> ClassName
                        if (decorated.size() > 4 && decorated.substr(0, 4) == ".?AV") {
                            size_t end = decorated.find("@@");
                            if (end != std::string::npos) {
                                vtable_class_name_ = decorated.substr(4, end - 4);
                            }
                        } else if (decorated.size() > 4 && decorated.substr(0, 4) == ".?AU") {
                            size_t end = decorated.find("@@");
                            if (end != std::string::npos) {
                                vtable_class_name_ = decorated.substr(4, end - 4);
                            }
                        } else if (!decorated.empty() && decorated[0] != '.') {
                            vtable_class_name_ = decorated;
                        }
                    }
                }

                LOG_INFO("VTable read: {} entries from 0x{:X}, class: {}",
                         vtable_entries_.size(), vtable_addr,
                         vtable_class_name_.empty() ? "(unknown)" : vtable_class_name_);
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", button_size::Small)) {
        vtable_entries_.clear();
        vtable_class_name_.clear();
        vtable_error_.clear();
    }

    // Error display
    if (!vtable_error_.empty()) {
        ErrorText("Error: %s", vtable_error_.c_str());
    }

    // Class name display
    if (!vtable_class_name_.empty()) {
        ImGui::TextColored(colors::Success, "RTTI Class: %s", vtable_class_name_.c_str());
    }

    // Results header
    ImGui::Separator();

    if (vtable_entries_.empty() && vtable_error_.empty()) {
        EmptyState("No vtable entries", "Enter an address and click 'Read VTable'");
        ImGui::End();
        return;
    }

    ImGui::Text("Entries: %zu", vtable_entries_.size());

    // Results table
    int num_cols = vtable_disasm_ ? 5 : 4;
    if (ImGui::BeginTable("##VTableEntries", num_cols, layout::kStandardTableFlags)) {

        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
        if (vtable_disasm_) {
            ImGui::TableSetupColumn("First Instr", ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)vtable_entries_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& entry = vtable_entries_[row];

                ImGui::TableNextRow();
                ImGui::PushID(row);

                // Index column
                ImGui::TableNextColumn();
                ImGui::Text("%d", row);

                // Address column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", FormatAddress(entry.address));

                // Function column - clickable if valid
                ImGui::TableNextColumn();
                if (entry.valid) {
                    char func_buf[32];
                    FormatAddressBuf(func_buf, sizeof(func_buf), entry.function);
                    if (ImGui::Selectable(func_buf, false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            NavigateToAddress(entry.function);
                        }
                    }

                    // Context menu
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                            disasm_address_ = entry.function;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX",
                                     (unsigned long long)entry.function);
                            auto code = dma_->ReadMemory(selected_pid_, entry.function, 1024);
                            if (!code.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(code, entry.function);
                            }
                            panels_.disassembly = true;
                        }
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                            memory_address_ = entry.function;
                            snprintf(address_input_, sizeof(address_input_), "0x%llX",
                                     (unsigned long long)entry.function);
                            memory_data_ = dma_->ReadMemory(selected_pid_, entry.function, 256);
                            panels_.memory_viewer = true;
                        }
                        #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_WAND_MAGIC_SPARKLES " Decompile", "Decompile"))) {
                            decompile_address_ = entry.function;
                            snprintf(decompile_address_input_, sizeof(decompile_address_input_), "0x%llX",
                                     (unsigned long long)entry.function);
                            panels_.decompiler = true;
                        }
                        #endif
                        ImGui::Separator();
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                            ImGui::SetClipboardText(func_buf);
                        }
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_BOOKMARK " Add Bookmark", "Add Bookmark"))) {
                            std::string label = vtable_class_name_.empty() ?
                                "vtable[" + std::to_string(row) + "]" :
                                vtable_class_name_ + "::vfunc" + std::to_string(row);
                            bookmarks_->Add(entry.function, label, "", "VTable");
                        }
                        ImGui::EndPopup();
                    }
                } else {
                    ImGui::TextColored(colors::Muted, "%s (invalid)",
                                       FormatAddress(entry.function));
                }

                // Context column
                ImGui::TableNextColumn();
                if (entry.valid) {
                    ImGui::Text("%s", entry.context.c_str());
                } else {
                    ImGui::TextDisabled("-");
                }

                // First instruction column
                if (vtable_disasm_) {
                    ImGui::TableNextColumn();
                    if (!entry.first_instr.empty()) {
                        ImGui::TextColored(colors::Info, "%s", entry.first_instr.c_str());
                    } else {
                        ImGui::TextDisabled("-");
                    }
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace orpheus::ui
