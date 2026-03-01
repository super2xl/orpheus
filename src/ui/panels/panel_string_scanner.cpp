#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/string_scanner.h"
#include "utils/logger.h"
#include <cstdio>
#include <future>

namespace orpheus::ui {

void Application::RenderStringScanner() {
    ImGui::Begin("String Scanner", &panels_.string_scanner);

    if (selected_pid_ == 0 || !dma_ || !dma_->IsConnected()) {
        EmptyState("No process selected", "Select a process and module to scan for strings");
        ImGui::End();
        return;
    }

    // Poll async future
    if (string_scanning_ && string_scan_future_.valid()) {
        auto status = string_scan_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            try {
                string_results_ = string_scan_future_.get();
                string_scan_error_.clear();
                LOG_INFO("String scan found {} results", string_results_.size());
            } catch (const std::exception& e) {
                string_scan_error_ = e.what();
                LOG_ERROR("String scan failed: {}", e.what());
            }
            string_scanning_ = false;
            string_scan_progress_ = 1.0f;
        }
    }

    // Module selection header
    ModuleHeader(selected_module_name_.c_str(), selected_module_base_, selected_module_size_);

    // Options row
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Min Length", &string_min_length_);
    ImGui::SameLine();
    ImGui::Checkbox("ASCII", &scan_ascii_);
    ImGui::SameLine();
    ImGui::Checkbox("Unicode", &scan_unicode_);
    ImGui::SameLine();

    bool can_scan = selected_module_base_ != 0 && !string_scanning_;
    if (!can_scan) ImGui::BeginDisabled();
    if (AccentButton("Scan")) {
        string_scanning_ = true;
        string_results_.clear();
        string_scan_error_.clear();
        string_scan_cancel_requested_ = false;
        string_scan_progress_ = 0.0f;
        string_scan_progress_stage_ = "Starting...";

        analysis::StringScanOptions opts;
        opts.min_length = string_min_length_;
        opts.scan_ascii = scan_ascii_;
        opts.scan_utf16 = scan_unicode_;

        uint32_t pid = selected_pid_;
        uint64_t module_base = selected_module_base_;
        uint32_t module_size = selected_module_size_;
        auto dma = dma_.get();

        string_scan_future_ = std::async(std::launch::async,
            [pid, module_base, module_size, opts, dma,
             &cancel_flag = string_scan_cancel_requested_,
             &progress_stage = string_scan_progress_stage_,
             &progress = string_scan_progress_]() -> std::vector<analysis::StringMatch> {

            std::vector<analysis::StringMatch> results;
            const size_t CHUNK_SIZE = 2 * 1024 * 1024; // 2 MB chunks
            // Add overlap to catch strings split across chunk boundaries
            const size_t OVERLAP = 1024;
            size_t total_chunks = (module_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
            size_t bytes_processed = 0;

            for (size_t chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++) {
                if (cancel_flag.load()) {
                    progress_stage = "Cancelled";
                    return results;
                }

                uint64_t chunk_offset = chunk_idx * CHUNK_SIZE;
                size_t read_size = std::min(CHUNK_SIZE + OVERLAP, (size_t)(module_size - chunk_offset));
                // Don't read past module end
                if (chunk_offset + read_size > module_size) {
                    read_size = module_size - chunk_offset;
                }

                // Update progress - reading phase
                char stage_buf[64];
                snprintf(stage_buf, sizeof(stage_buf), "Reading chunk %zu/%zu...",
                         chunk_idx + 1, total_chunks);
                progress_stage = stage_buf;
                progress = static_cast<float>(chunk_idx) / static_cast<float>(total_chunks);

                auto chunk_data = dma->ReadMemory(pid, module_base + chunk_offset, read_size);
                if (chunk_data.empty()) continue;

                if (cancel_flag.load()) {
                    progress_stage = "Cancelled";
                    return results;
                }

                // Update progress - scanning phase
                snprintf(stage_buf, sizeof(stage_buf), "Scanning chunk %zu/%zu...",
                         chunk_idx + 1, total_chunks);
                progress_stage = stage_buf;

                auto chunk_results = analysis::StringScanner::Scan(
                    chunk_data, opts, module_base + chunk_offset);

                // For chunks after the first, filter out strings that start in the overlap zone
                // of the previous chunk (they were already captured)
                if (chunk_idx > 0) {
                    uint64_t overlap_boundary = module_base + chunk_offset + OVERLAP;
                    // Keep only strings that start at or after the main region
                    // (some strings from the overlap will be dupes of the previous chunk)
                    chunk_results.erase(
                        std::remove_if(chunk_results.begin(), chunk_results.end(),
                            [module_base, chunk_offset](const analysis::StringMatch& m) {
                                return m.address < module_base + chunk_offset;
                            }),
                        chunk_results.end());
                }

                results.insert(results.end(), chunk_results.begin(), chunk_results.end());
                bytes_processed += CHUNK_SIZE;
            }

            // Final sort and deduplicate
            progress_stage = "Sorting results...";
            progress = 0.95f;

            std::sort(results.begin(), results.end(),
                [](const analysis::StringMatch& a, const analysis::StringMatch& b) {
                    return a.address < b.address;
                });

            results.erase(std::unique(results.begin(), results.end(),
                [](const analysis::StringMatch& a, const analysis::StringMatch& b) {
                    return a.address == b.address;
                }),
                results.end());

            progress_stage = "Done";
            progress = 1.0f;
            return results;
        });
    }
    if (!can_scan) ImGui::EndDisabled();

    // Cancel button during scan
    if (string_scanning_) {
        ImGui::SameLine();
        if (DangerButton("Cancel")) {
            string_scan_cancel_requested_ = true;
        }
    }

    // Progress display
    if (string_scanning_) {
        ImGui::TextColored(colors::Warning, "%s", string_scan_progress_stage_.c_str());
        ProgressBarWithText(string_scan_progress_);
    }

    // Error display
    if (!string_scan_error_.empty()) {
        ImGui::TextColored(colors::Error, "Error: %s", string_scan_error_.c_str());
    }

    ImGui::Separator();

    // Results header with filter
    ImGui::Text("Results: %zu", string_results_.size());
    ImGui::SameLine();

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##str_filter", "Filter strings...", string_filter_, sizeof(string_filter_));
    ImGui::SameLine();

    if (ImGui::SmallButton("Clear")) {
        string_results_.clear();
        string_filter_[0] = '\0';
    }
    ImGui::Separator();

    if (string_results_.empty() && !string_scanning_) {
        EmptyState("No results", "Configure options and click Scan");
        ImGui::End();
        return;
    }

    // Build filtered view
    bool has_filter = string_filter_[0] != '\0';
    std::string filter_lower;
    if (has_filter) {
        filter_lower = ToLower(string_filter_);
    }

    const float row_height = ImGui::GetTextLineHeightWithSpacing();

    if (ImGui::BeginTable("##StringTable", 3, layout::kStandardTableFlags)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("String", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (has_filter) {
            // Filtered mode - no clipper (must iterate all)
            for (int i = 0; i < (int)string_results_.size(); i++) {
                const auto& str = string_results_[i];

                // Case-insensitive substring match
                std::string val_lower = ToLower(str.value);
                if (val_lower.find(filter_lower) == std::string::npos) continue;

                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                ImGui::TableNextColumn();
                ImGui::PushID(i);

                char addr_buf[32];
                FormatAddressBuf(addr_buf, sizeof(addr_buf), str.address);

                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(str.address);
                    }
                }

                RenderStringContextMenu(str, addr_buf);
                RenderStringTooltip(str);

                ImGui::PopID();

                ImGui::TableNextColumn();
                const char* type_str = str.type == analysis::StringType::ASCII ? "ASCII" :
                                      str.type == analysis::StringType::UTF16_LE ? "UTF16" : "UTF8";
                ImGui::Text("%s", type_str);

                ImGui::TableNextColumn();
                if (str.value.length() > 100) {
                    ImGui::Text("%.100s...", str.value.c_str());
                } else {
                    ImGui::Text("%s", str.value.c_str());
                }
            }
        } else {
            // Unfiltered mode - use clipper for performance
            ImGuiListClipper clipper;
            clipper.Begin((int)string_results_.size(), row_height);

            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const auto& str = string_results_[i];

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);

                    char addr_buf[32];
                    FormatAddressBuf(addr_buf, sizeof(addr_buf), str.address);

                    if (ImGui::Selectable(addr_buf, false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            NavigateToAddress(str.address);
                        }
                    }

                    RenderStringContextMenu(str, addr_buf);
                    RenderStringTooltip(str);

                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    const char* type_str = str.type == analysis::StringType::ASCII ? "ASCII" :
                                          str.type == analysis::StringType::UTF16_LE ? "UTF16" : "UTF8";
                    ImGui::Text("%s", type_str);

                    ImGui::TableNextColumn();
                    if (str.value.length() > 100) {
                        ImGui::Text("%.100s...", str.value.c_str());
                    } else {
                        ImGui::Text("%s", str.value.c_str());
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void Application::RenderStringContextMenu(const analysis::StringMatch& str, const char* addr_buf) {
    if (ImGui::BeginPopupContextItem("##StringContext")) {
        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
            memory_address_ = str.address;
            snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)str.address);
            memory_data_ = dma_->ReadMemory(selected_pid_, str.address, 256);
            panels_.memory_viewer = true;
        }
        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
            disasm_address_ = str.address;
            snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)str.address);
            if (disassembler_) {
                auto data = dma_->ReadMemory(selected_pid_, str.address, 512);
                if (!data.empty()) {
                    disasm_instructions_ = disassembler_->Disassemble(data, str.address);
                }
            }
            panels_.disassembly = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
            ImGui::SetClipboardText(addr_buf);
        }
        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy String", "Copy String"))) {
            ImGui::SetClipboardText(str.value.c_str());
        }
        ImGui::EndPopup();
    }
}

void Application::RenderStringTooltip(const analysis::StringMatch& str) {
    if (ImGui::IsItemHovered()) {
        uint64_t offset = str.address - selected_module_base_;
        if (str.value.length() > 80) {
            ImGui::SetTooltip("%s+0x%llX\n\n%.500s%s\n\nRight-click for options",
                selected_module_name_.c_str(), (unsigned long long)offset,
                str.value.c_str(),
                str.value.length() > 500 ? "..." : "");
        } else {
            ImGui::SetTooltip("%s+0x%llX\nRight-click for options",
                selected_module_name_.c_str(), (unsigned long long)offset);
        }
    }
}

} // namespace orpheus::ui
