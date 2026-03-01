#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/disassembler.h"
#include "utils/bookmarks.h"
#include "utils/logger.h"
#include <cstdio>

namespace orpheus::ui {

void Application::RenderBookmarks() {
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bookmarks", &panels_.bookmarks);

    if (!bookmarks_) {
        EmptyState("Bookmark manager not initialized");
        ImGui::End();
        return;
    }

    // Header with count and actions
    ImGui::Text("Bookmarks: %zu", bookmarks_->Count());
    ImGui::SameLine(ImGui::GetWindowWidth() - 180);

    if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_BOOKMARK " Add Current", "Add Current"))) {
        if (memory_address_ != 0) {
            show_add_bookmark_popup_ = true;
            snprintf(bookmark_label_, sizeof(bookmark_label_), "Bookmark_%zu", bookmarks_->Count() + 1);
            bookmark_notes_[0] = '\0';
            strncpy(bookmark_category_, "General", sizeof(bookmark_category_) - 1);
        }
    }
    HelpTooltip(("Add bookmark at current memory address (" + std::string(FormatAddress(memory_address_)) + ")").c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_DOWNLOAD " Save", "Save"))) {
        bookmarks_->Save();
        status_message_ = "Bookmarks saved";
        status_timer_ = 2.0f;
    }

    // Filter
    FilterBar("##bmfilter", bookmark_filter_, sizeof(bookmark_filter_));

    ImGui::Separator();

    // Add bookmark popup
    if (show_add_bookmark_popup_) {
        ImGui::OpenPopup("Add Bookmark");
    }

    if (BeginCenteredModal("Add Bookmark", &show_add_bookmark_popup_)) {
        uint64_t addr = (bookmark_edit_index_ >= 0) ?
            bookmarks_->GetAll()[bookmark_edit_index_].address : memory_address_;

        KeyValueHex("Address", addr);
        if (!selected_module_name_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", selected_module_name_.c_str());
        }

        ImGui::Spacing();

        ImGui::SetNextItemWidth(form_width::Normal);
        ImGui::InputText("Label", bookmark_label_, sizeof(bookmark_label_));

        ImGui::SetNextItemWidth(form_width::Normal);
        ImGui::InputText("Category", bookmark_category_, sizeof(bookmark_category_));

        ImGui::SetNextItemWidth(form_width::Normal);
        ImGui::InputTextMultiline("Notes", bookmark_notes_, sizeof(bookmark_notes_),
            ImVec2(250, 60));

        int action = DialogOkCancelButtons("Save", "Cancel");
        if (action == 1) {
            if (bookmark_edit_index_ >= 0) {
                Bookmark bm;
                bm.address = addr;
                bm.label = bookmark_label_;
                bm.notes = bookmark_notes_;
                bm.category = bookmark_category_;
                bm.module = selected_module_name_;
                bookmarks_->Update(bookmark_edit_index_, bm);
            } else {
                bookmarks_->Add(addr, bookmark_label_, bookmark_notes_,
                               bookmark_category_, selected_module_name_);
            }
            bookmark_edit_index_ = -1;
            show_add_bookmark_popup_ = false;
            ImGui::CloseCurrentPopup();
        } else if (action == 2) {
            bookmark_edit_index_ = -1;
            show_add_bookmark_popup_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Bookmark list
    if (bookmarks_->Count() == 0) {
        EmptyState("No bookmarks", "Right-click addresses to add bookmarks");
        if (bookmarks_->IsDirty()) {
            ImGui::Separator();
            ImGui::TextColored(colors::Warning, "Unsaved changes");
        }
        ImGui::End();
        return;
    }

    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginTable("##BookmarkTable", 4, layout::kSortableTableFlags)) {

        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 50.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Filter bookmarks (custom: searches label, category, and notes)
        std::string filter_str = ToLower(bookmark_filter_);
        const auto& all_bookmarks = bookmarks_->GetAll();
        std::vector<size_t> filtered_indices;

        for (size_t i = 0; i < all_bookmarks.size(); i++) {
            const auto& bm = all_bookmarks[i];
            if (filter_str.empty() ||
                MatchesFilter(bm.label, filter_str) ||
                MatchesFilter(bm.category, filter_str) ||
                MatchesFilter(bm.notes, filter_str)) {
                filtered_indices.push_back(i);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)filtered_indices.size(), row_height);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                size_t idx = filtered_indices[row];
                const auto& bm = all_bookmarks[idx];

                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                ImGui::PushID((int)idx);

                // Address column
                ImGui::TableNextColumn();
                char addr_buf[32];
                FormatAddressBuf(addr_buf, sizeof(addr_buf), bm.address);

                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(bm.address);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##BookmarkCtx")) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_ARROW_RIGHT " Go to Address", "Go to Address"))) {
                        NavigateToAddress(bm.address);
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = bm.address;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)bm.address);
                        if (dma_ && dma_->IsConnected() && selected_pid_ != 0) {
                            memory_data_ = dma_->ReadMemory(selected_pid_, bm.address, 512);
                        }
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                        disasm_address_ = bm.address;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX",
                                (unsigned long long)bm.address);
                        if (dma_ && dma_->IsConnected() && selected_pid_ != 0 && disassembler_) {
                            auto code = dma_->ReadMemory(selected_pid_, bm.address, 1024);
                            if (!code.empty()) {
                                disasm_instructions_ = disassembler_->Disassemble(code, bm.address);
                            }
                        }
                        panels_.disassembly = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_GEAR " Edit", "Edit"))) {
                        bookmark_edit_index_ = (int)idx;
                        strncpy(bookmark_label_, bm.label.c_str(), sizeof(bookmark_label_) - 1);
                        strncpy(bookmark_notes_, bm.notes.c_str(), sizeof(bookmark_notes_) - 1);
                        strncpy(bookmark_category_, bm.category.c_str(), sizeof(bookmark_category_) - 1);
                        show_add_bookmark_popup_ = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_XMARK " Delete", "Delete"))) {
                        bookmarks_->Remove(idx);
                    }
                    ImGui::EndPopup();
                }

                // Tooltip
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", bm.label.c_str());
                    ImGui::TextDisabled("%s", FormatAddress(bm.address));
                    if (!bm.module.empty()) {
                        ImGui::TextDisabled("Module: %s", bm.module.c_str());
                    }
                    if (!bm.notes.empty()) {
                        ImGui::Separator();
                        ImGui::TextWrapped("%s", bm.notes.c_str());
                    }
                    ImGui::TextDisabled("Double-click to navigate, right-click for options");
                    ImGui::EndTooltip();
                }

                // Label column
                ImGui::TableNextColumn();
                ImGui::Text("%s", bm.label.c_str());

                // Category column
                ImGui::TableNextColumn();
                if (!bm.category.empty()) {
                    ImGui::TextColored(colors::Info, "%s", bm.category.c_str());
                }

                // Actions column
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X")) {
                    bookmarks_->Remove(idx);
                }
                HelpTooltip("Delete bookmark");

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    // Unsaved changes indicator
    if (bookmarks_->IsDirty()) {
        ImGui::Separator();
        ImGui::TextColored(colors::Warning, "Unsaved changes");
    }

    ImGui::End();
}

} // namespace orpheus::ui
