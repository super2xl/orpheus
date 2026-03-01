#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/disassembler.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>

namespace orpheus::ui {

void Application::RenderMemoryRegions() {
    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory Regions", &panels_.memory_regions);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to view memory regions");
        ImGui::End();
        return;
    }

    // Refresh regions if PID changed
    if (memory_regions_pid_ != selected_pid_) {
        cached_memory_regions_ = dma_->GetMemoryRegions(selected_pid_);
        memory_regions_pid_ = selected_pid_;
    }

    // Toolbar
    ImGui::Text("Process: %s (%zu regions)", selected_process_name_.c_str(), cached_memory_regions_.size());
    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Refresh", "Refresh"))) {
        cached_memory_regions_ = dma_->GetMemoryRegions(selected_pid_);
    }

    // Filter input
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##regionfilter", "Filter by protection, type, or info...",
                             memory_regions_filter_, sizeof(memory_regions_filter_));

    ImGui::Separator();

    if (cached_memory_regions_.empty()) {
        EmptyState("No memory regions found");
        ImGui::End();
        return;
    }

    // Table
    if (ImGui::BeginTable("##MemoryRegionsTable", 5, layout::kSortableTableFlags)) {

        ImGui::TableSetupColumn("Base Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 150.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, layout::kColumnSize);
        ImGui::TableSetupColumn("Protection", ImGuiTableColumnFlags_WidthFixed, layout::kColumnProtection);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, layout::kColumnProtection);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                memory_regions_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                memory_regions_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        // Build filtered list (custom: searches protection, type, and info)
        std::string filter_str = ToLower(memory_regions_filter_);

        std::vector<size_t> filtered_indices;
        for (size_t i = 0; i < cached_memory_regions_.size(); i++) {
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                const auto& region = cached_memory_regions_[i];
                if (MatchesFilter(region.protection, filter_str) ||
                    MatchesFilter(region.type, filter_str) ||
                    MatchesFilter(region.info, filter_str)) {
                    filtered_indices.push_back(i);
                }
            }
        }

        // Sort filtered indices
        auto& regions = cached_memory_regions_;
        int sort_col = memory_regions_sort_column_;
        bool ascending = memory_regions_sort_ascending_;
        std::sort(filtered_indices.begin(), filtered_indices.end(),
            [&regions, sort_col, ascending](size_t a, size_t b) {
                const auto& ra = regions[a];
                const auto& rb = regions[b];
                int cmp = 0;
                switch (sort_col) {
                    case 0: cmp = (ra.base_address < rb.base_address) ? -1 : (ra.base_address > rb.base_address) ? 1 : 0; break;
                    case 1: cmp = (ra.size < rb.size) ? -1 : (ra.size > rb.size) ? 1 : 0; break;
                    case 2: cmp = ra.protection.compare(rb.protection); break;
                    case 3: cmp = ra.type.compare(rb.type); break;
                    case 4: cmp = ra.info.compare(rb.info); break;
                }
                return ascending ? (cmp < 0) : (cmp > 0);
            });

        // Render
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& region = cached_memory_regions_[filtered_indices[row]];

                ImGui::TableNextRow();

                // Base Address column
                ImGui::TableNextColumn();
                ImGui::PushID((int)filtered_indices[row]);
                char addr_buf[32];
                FormatAddressBuf(addr_buf, sizeof(addr_buf), region.base_address);
                if (ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_SpanAllColumns)) {
                    NavigateToAddress(region.base_address);
                }

                // Context menu
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = region.base_address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)region.base_address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, region.base_address, 512);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                        disasm_address_ = region.base_address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)region.base_address);
                        auto data = dma_->ReadMemory(selected_pid_, region.base_address, 4096);
                        if (!data.empty() && disassembler_) {
                            disasm_instructions_ = disassembler_->Disassemble(data, region.base_address);
                        }
                        panels_.disassembly = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Size", "Copy Size"))) {
                        char size_buf[32];
                        snprintf(size_buf, sizeof(size_buf), "0x%llX", (unsigned long long)region.size);
                        ImGui::SetClipboardText(size_buf);
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                // Size column
                ImGui::TableNextColumn();
                ImGui::Text("%s", FormatSize(region.size));

                // Protection column with color coding
                ImGui::TableNextColumn();
                ImVec4 prot_color = colors::Muted;
                if (region.protection.find("RWX") != std::string::npos ||
                    region.protection.find("EXECUTE_READWRITE") != std::string::npos) {
                    prot_color = colors::Error;  // Red for RWX (suspicious)
                } else if (region.protection.find("RX") != std::string::npos ||
                           region.protection.find("EXECUTE") != std::string::npos) {
                    prot_color = colors::Success;  // Green for executable
                } else if (region.protection.find("RW") != std::string::npos ||
                           region.protection.find("READWRITE") != std::string::npos) {
                    prot_color = colors::Info;  // Blue for read-write
                }
                ImGui::TextColored(prot_color, "%s", region.protection.c_str());

                // Type column
                ImGui::TableNextColumn();
                ImGui::Text("%s", region.type.c_str());

                // Info column
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", region.info.c_str());
            }
        }

        ImGui::EndTable();
    }

    // Summary statistics
    ImGui::Separator();
    uint64_t total_size = 0, executable_size = 0, writable_size = 0;
    for (const auto& region : cached_memory_regions_) {
        total_size += region.size;
        if (region.protection.find("X") != std::string::npos ||
            region.protection.find("EXECUTE") != std::string::npos) {
            executable_size += region.size;
        }
        if (region.protection.find("W") != std::string::npos ||
            region.protection.find("WRITE") != std::string::npos) {
            writable_size += region.size;
        }
    }
    ImGui::Text("Total: %s | Executable: %s | Writable: %s",
                FormatSize(total_size), FormatSize(executable_size), FormatSize(writable_size));

    ImGui::End();
}

} // namespace orpheus::ui
