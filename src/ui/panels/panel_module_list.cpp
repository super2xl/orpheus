#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "utils/logger.h"
#include <algorithm>

namespace orpheus::ui {

void Application::RenderModuleList() {
    ImGui::Begin("Modules", &panels_.module_list);

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to view its modules");
        ImGui::End();
        return;
    }

    // Auto-refresh modules
    if (auto_refresh_enabled_ && dma_ && dma_->IsConnected()) {
        double current_time = glfwGetTime();
        if (current_time - last_module_refresh_ >= module_refresh_interval_) {
            RefreshModules();
            last_module_refresh_ = current_time;
        }
    }

    ImGui::Text("Process: %s (%zu modules)", selected_process_name_.c_str(), cached_modules_.size());

    // Search filter
    static char filter[256] = {};
    FilterBar("##modfilter", filter, sizeof(filter));
    ImGui::Separator();

    if (cached_modules_.empty()) {
        EmptyState("No modules loaded", "Check DMA connection or refresh");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("##ModuleTable", 3, layout::kSortableTableFlags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Base", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, layout::kColumnSize);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                module_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                module_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Build filtered list using shared helper
        auto filtered_indices = BuildFilteredIndices<ModuleInfo>(
            cached_modules_, filter,
            [](const ModuleInfo& m) { return m.name; });

        // Sort filtered indices
        auto& mods = cached_modules_;
        int sort_col = module_sort_column_;
        bool ascending = module_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&mods, sort_col, ascending](size_t a, size_t b) {
                const auto& ma = mods[a];
                const auto& mb = mods[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0: cmp = ma.name.compare(mb.name); break;
                    case 1: cmp = (ma.base_address < mb.base_address) ? -1 : (ma.base_address > mb.base_address) ? 1 : 0; break;
                    case 2: cmp = (ma.size < mb.size) ? -1 : (ma.size > mb.size) ? 1 : 0; break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Render sorted list with clipper
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& mod = cached_modules_[filtered_indices[row]];

                ImGui::TableNextRow();
                bool is_selected = (mod.name == selected_module_name_);

                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);
                if (ImGui::Selectable(mod.name.c_str(), is_selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_module_name_ = mod.name;
                    selected_module_base_ = mod.base_address;
                    selected_module_size_ = mod.size;
                    NavigateToAddress(mod.base_address);
                    LOG_INFO("Selected module: {} @ 0x{:X}", mod.name, mod.base_address);
                }

                // Context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = mod.base_address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)mod.base_address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, mod.base_address, 512);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                        disasm_address_ = mod.base_address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)mod.base_address);
                        auto data = dma_->ReadMemory(selected_pid_, mod.base_address, 4096);
                        if (!data.empty() && disassembler_) {
                            disasm_instructions_ = disassembler_->Disassemble(data, mod.base_address);
                        }
                        panels_.disassembly = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_SITEMAP " Scan RTTI", "Scan RTTI"))) {
                        selected_module_name_ = mod.name;
                        selected_module_base_ = mod.base_address;
                        selected_module_size_ = mod.size;
                        panels_.rtti_scanner = true;
                        rtti_scanning_ = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_EXPORT " Dump Module...", "Dump Module..."))) {
                        selected_module_name_ = mod.name;
                        selected_module_base_ = mod.base_address;
                        selected_module_size_ = mod.size;
                        show_dump_dialog_ = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        char addr_buf[32];
                        FormatAddressBuf(addr_buf, sizeof(addr_buf), mod.base_address);
                        ImGui::SetClipboardText(addr_buf);
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatAddress(mod.base_address));

                ImGui::TableNextColumn();
                ImGui::Text("0x%X", mod.size);
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace orpheus::ui
