#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/rtti_parser.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace orpheus::ui {

void Application::RenderRTTIScanner() {
    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("RTTI Scanner", &panels_.rtti_scanner);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process first");
        ImGui::End();
        return;
    }

    if (selected_module_base_ == 0) {
        EmptyState("No module selected", "Right-click a module and select 'Scan RTTI'");
        ImGui::End();
        return;
    }

    // Header with module info and scan button
    ImGui::Text("Module: %s @ %s (%s)",
        selected_module_name_.c_str(),
        FormatAddress(selected_module_base_),
        FormatSize(selected_module_size_));
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);

    if (rtti_scanning_) {
        ImGui::TextColored(colors::Warning, "Scanning...");
    } else {
        if (ImGui::Button("Scan Module", ImVec2(100, 0))) {
            rtti_scanning_ = true;
        }
    }

    // Perform scan if requested
    if (rtti_scanning_) {
        rtti_results_.clear();
        rtti_scanned_module_base_ = selected_module_base_;
        rtti_scanned_module_name_ = selected_module_name_;

        auto read_func = [this](uint64_t addr, size_t size) -> std::vector<uint8_t> {
            return dma_->ReadMemory(selected_pid_, addr, size);
        };

        analysis::RTTIParser parser(read_func, selected_module_base_);

        size_t found = parser.ScanModule(selected_module_base_, [this](const analysis::RTTIClassInfo& info) {
            rtti_results_.push_back(info);
        });

        LOG_INFO("RTTI scan found {} classes in {}", found, selected_module_name_);
        rtti_scanning_ = false;
    }

    // Filter input
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##RTTIFilter", "Filter by name...", rtti_filter_, sizeof(rtti_filter_));

    // Build filtered list using shared helper
    auto filtered_indices = BuildFilteredIndices<analysis::RTTIClassInfo>(
        rtti_results_, rtti_filter_,
        [](const analysis::RTTIClassInfo& info) { return info.demangled_name; });

    // Results summary
    ImGui::Text("Classes: %zu / %zu", filtered_indices.size(), rtti_results_.size());
    if (!rtti_scanned_module_name_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(scanned: %s)", rtti_scanned_module_name_.c_str());
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        rtti_results_.clear();
        rtti_scanned_module_name_.clear();
        rtti_scanned_module_base_ = 0;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_EXPORT " Export CSV", "Export CSV"))) {
        std::string filename = rtti_scanned_module_name_ + "_rtti.csv";
        std::ofstream out(filename);
        if (out.is_open()) {
            out << "Vtable,Methods,Flags,Type,Hierarchy\n";
            for (const auto& info : rtti_results_) {
                std::string type_name = info.demangled_name;
                if (type_name.substr(0, 6) == "class ") type_name = type_name.substr(6);
                else if (type_name.substr(0, 7) == "struct ") type_name = type_name.substr(7);

                out << "0x" << std::hex << info.vtable_address << ","
                    << std::dec << info.method_count << ","
                    << info.GetFlags() << ","
                    << "\"" << type_name << "\","
                    << "\"" << info.GetHierarchyString() << "\"\n";
            }
            out.close();
            LOG_INFO("Exported RTTI to {}", filename);
        }
    }

    ImGui::Separator();

    if (rtti_results_.empty()) {
        EmptyState("No RTTI results", "Click 'Scan Module' to discover classes");
        ImGui::End();
        return;
    }

    // Results table - IDA Class Informer style
    const float row_height = ImGui::GetTextLineHeightWithSpacing();

    if (ImGui::BeginTable("##RTTITable", 5,
        layout::kSortableTableFlags | ImGuiTableFlags_SizingFixedFit)) {

        ImGui::TableSetupColumn("Vftable", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Methods", ImGuiTableColumnFlags_WidthFixed, layout::kColumnMethods);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, layout::kColumnFlags);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                rtti_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                rtti_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Sort filtered indices
        auto& results = rtti_results_;
        int sort_col = rtti_sort_column_;
        bool ascending = rtti_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&results, sort_col, ascending](size_t a, size_t b) {
                const auto& info_a = results[a];
                const auto& info_b = results[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0:
                        cmp = (info_a.vtable_address < info_b.vtable_address) ? -1 :
                              (info_a.vtable_address > info_b.vtable_address) ? 1 : 0;
                        break;
                    case 1:
                        cmp = (info_a.method_count < info_b.method_count) ? -1 :
                              (info_a.method_count > info_b.method_count) ? 1 : 0;
                        break;
                    case 2: cmp = info_a.GetFlags().compare(info_b.GetFlags()); break;
                    case 3: cmp = info_a.demangled_name.compare(info_b.demangled_name); break;
                    case 4: cmp = info_a.GetHierarchyString().compare(info_b.GetHierarchyString()); break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Render with clipper
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size(), row_height);

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& info = rtti_results_[filtered_indices[row]];

                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);

                // Vtable address column
                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);

                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "%llX", (unsigned long long)info.vtable_address);

                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(info.vtable_address);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##RTTIContext")) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View Vtable in Memory", "View Vtable in Memory"))) {
                        memory_address_ = info.vtable_address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)info.vtable_address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, info.vtable_address, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View Vtable in Disassembly", "View Vtable in Disassembly"))) {
                        auto vtable_data = dma_->ReadMemory(selected_pid_, info.vtable_address, 8);
                        if (vtable_data.size() >= 8) {
                            uint64_t first_func = *reinterpret_cast<uint64_t*>(vtable_data.data());
                            disasm_address_ = first_func;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)first_func);
                            auto code = dma_->ReadMemory(selected_pid_, first_func, 4096);
                            if (!code.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(code, first_func);
                            }
                            panels_.disassembly = true;
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Vtable Address", "Copy Vtable Address"))) {
                        char buf[32];
                        FormatAddressBuf(buf, sizeof(buf), info.vtable_address);
                        ImGui::SetClipboardText(buf);
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Class Name", "Copy Class Name"))) {
                        ImGui::SetClipboardText(info.demangled_name.c_str());
                    }
                    if (ImGui::MenuItem("Copy Mangled Name")) {
                        ImGui::SetClipboardText(info.mangled_name.c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();

                // Methods column
                ImGui::TableNextColumn();
                ImGui::Text("%u", info.method_count);

                // Flags column (M=Multiple, V=Virtual)
                ImGui::TableNextColumn();
                std::string flags = info.GetFlags();
                if (!flags.empty()) {
                    ImVec4 flag_color;
                    if (flags.find('M') != std::string::npos && flags.find('V') != std::string::npos) {
                        flag_color = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);  // Magenta for MV
                    } else if (flags.find('M') != std::string::npos) {
                        flag_color = colors::Warning;  // Yellow for M
                    } else {
                        flag_color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan for V
                    }
                    ImGui::TextColored(flag_color, "%s", flags.c_str());
                } else {
                    ImGui::TextDisabled("-");
                }

                // Type column
                ImGui::TableNextColumn();
                std::string type_name = info.demangled_name;
                if (type_name.substr(0, 6) == "class ") type_name = type_name.substr(6);
                else if (type_name.substr(0, 7) == "struct ") type_name = type_name.substr(7);
                ImGui::TextUnformatted(type_name.c_str());

                // Hierarchy column
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(info.GetHierarchyString().c_str());
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace orpheus::ui
