#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/pattern_scanner.h"
#include "utils/logger.h"
#include <cstdio>

namespace orpheus::ui {

void Application::RenderPatternScanner() {
    ImGui::Begin("Pattern Scanner", &panels_.pattern_scanner);

    // Check for async scan completion
    if (pattern_scanning_ && pattern_scan_future_.valid()) {
        auto status = pattern_scan_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            try {
                pattern_results_ = pattern_scan_future_.get();
                pattern_scan_error_.clear();
                LOG_INFO("Pattern scan found {} results", pattern_results_.size());
            } catch (const std::exception& e) {
                pattern_scan_error_ = e.what();
                pattern_results_.clear();
                LOG_ERROR("Pattern scan failed: {}", e.what());
            }
            pattern_scanning_ = false;
            pattern_scan_progress_ = 1.0f;
            pattern_scan_progress_stage_ = "Complete";
        }
    }

    if (selected_pid_ == 0 || !dma_ || !dma_->IsConnected()) {
        EmptyState("No process selected", "Select a process and module to scan");
        ImGui::End();
        return;
    }

    // Module selection header
    ModuleHeader(selected_module_name_.c_str(), selected_module_base_, selected_module_size_);

    ImGui::SetNextItemWidth(-80.0f);
    ImGui::InputTextWithHint("##pattern", "48 8B 05 ?? ?? ?? ??", pattern_input_, sizeof(pattern_input_));
    ImGui::SameLine();

    bool can_scan = selected_module_base_ != 0 && !pattern_scanning_;
    if (!can_scan) ImGui::BeginDisabled();
    if (ImGui::Button("Scan")) {
        auto compiled_pattern = analysis::PatternScanner::Compile(pattern_input_);
        if (compiled_pattern) {
            pattern_scanning_ = true;
            pattern_scan_cancel_requested_ = false;
            pattern_results_.clear();
            pattern_scan_error_.clear();
            pattern_scan_progress_ = 0.0f;
            pattern_scan_progress_stage_ = "Starting...";

            uint32_t pid = selected_pid_;
            uint64_t module_base = selected_module_base_;
            uint32_t module_size = selected_module_size_;
            analysis::Pattern pattern = *compiled_pattern;
            auto* dma = dma_.get();

            pattern_scan_future_ = std::async(std::launch::async,
                [pid, module_base, module_size, pattern, dma,
                 &cancel_flag = pattern_scan_cancel_requested_,
                 &progress_stage = pattern_scan_progress_stage_,
                 &progress = pattern_scan_progress_]() -> std::vector<uint64_t> {

                std::vector<uint64_t> results;

                constexpr size_t CHUNK_SIZE = 2 * 1024 * 1024;
                const size_t pattern_len = pattern.bytes.size();
                const size_t overlap_size = pattern_len > 0 ? pattern_len - 1 : 0;

                size_t total_chunks = (module_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
                if (total_chunks == 0) total_chunks = 1;

                std::vector<uint8_t> overlap_buffer;
                overlap_buffer.reserve(overlap_size);

                size_t bytes_processed = 0;
                size_t chunk_index = 0;

                while (bytes_processed < module_size) {
                    if (cancel_flag.load()) {
                        progress_stage = "Cancelled";
                        return results;
                    }

                    size_t chunk_offset = bytes_processed;
                    size_t remaining = module_size - bytes_processed;
                    size_t chunk_size = std::min(CHUNK_SIZE, remaining);

                    chunk_index++;
                    progress_stage = "Reading chunk " + std::to_string(chunk_index) + "/" + std::to_string(total_chunks) + "...";
                    progress = static_cast<float>(chunk_index - 1) / static_cast<float>(total_chunks * 2);

                    auto chunk_data = dma->ReadMemory(pid, module_base + chunk_offset, chunk_size);
                    if (chunk_data.empty()) {
                        bytes_processed += chunk_size;
                        overlap_buffer.clear();
                        continue;
                    }

                    if (cancel_flag.load()) {
                        progress_stage = "Cancelled";
                        return results;
                    }

                    progress_stage = "Scanning chunk " + std::to_string(chunk_index) + "/" + std::to_string(total_chunks) + "...";
                    progress = static_cast<float>(chunk_index * 2 - 1) / static_cast<float>(total_chunks * 2);

                    // Handle boundary overlap
                    if (!overlap_buffer.empty() && overlap_buffer.size() == overlap_size) {
                        std::vector<uint8_t> boundary_buffer;
                        boundary_buffer.reserve(overlap_size * 2);
                        boundary_buffer.insert(boundary_buffer.end(), overlap_buffer.begin(), overlap_buffer.end());
                        size_t bytes_to_add = std::min(overlap_size, chunk_data.size());
                        boundary_buffer.insert(boundary_buffer.end(), chunk_data.begin(), chunk_data.begin() + bytes_to_add);

                        uint64_t boundary_base = module_base + chunk_offset - overlap_size;
                        auto boundary_matches = analysis::PatternScanner::Scan(boundary_buffer, pattern, boundary_base);

                        for (uint64_t match_addr : boundary_matches) {
                            uint64_t match_end = match_addr + pattern_len;
                            uint64_t chunk_start_addr = module_base + chunk_offset;
                            if (match_addr < chunk_start_addr && match_end > chunk_start_addr) {
                                results.push_back(match_addr);
                            }
                        }
                    }

                    // Scan main chunk
                    uint64_t chunk_base = module_base + chunk_offset;
                    auto chunk_matches = analysis::PatternScanner::Scan(chunk_data, pattern, chunk_base);
                    results.insert(results.end(), chunk_matches.begin(), chunk_matches.end());

                    // Save overlap bytes
                    overlap_buffer.clear();
                    if (chunk_data.size() >= overlap_size) {
                        overlap_buffer.insert(overlap_buffer.end(),
                            chunk_data.end() - overlap_size, chunk_data.end());
                    } else {
                        overlap_buffer = chunk_data;
                    }

                    bytes_processed += chunk_size;
                }

                std::sort(results.begin(), results.end());
                results.erase(std::unique(results.begin(), results.end()), results.end());

                progress_stage = "Complete";
                progress = 1.0f;
                return results;
            });
        } else {
            pattern_scan_error_ = "Invalid pattern syntax";
        }
    }
    if (!can_scan) ImGui::EndDisabled();

    // Cancel button
    if (pattern_scanning_) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pattern_scan_cancel_requested_ = true;
        }
    }

    // Progress indicator
    if (pattern_scanning_) {
        ImGui::TextColored(colors::Warning, "%s", pattern_scan_progress_stage_.c_str());
        ProgressBarWithText(pattern_scan_progress_);
    }

    // Error display
    if (!pattern_scan_error_.empty()) {
        ErrorText("Error: %s", pattern_scan_error_.c_str());
    }

    ImGui::Separator();

    // Results header
    ImGui::Text("Results: %zu", pattern_results_.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        pattern_results_.clear();
        pattern_scan_error_.clear();
    }

    if (pattern_results_.empty()) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("##PatternResults", 2, layout::kStandardTableFlags)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)pattern_results_.size());

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                uint64_t addr = pattern_results_[i];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::PushID(i);

                char addr_buf[32];
                FormatAddressBuf(addr_buf, sizeof(addr_buf), addr);

                if (ImGui::Selectable(addr_buf, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        NavigateToAddress(addr);
                    }
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("##ResultContext")) {
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = addr;
                        snprintf(address_input_, sizeof(address_input_), "0x%llX", (unsigned long long)addr);
                        memory_data_ = dma_->ReadMemory(selected_pid_, addr, 256);
                        panels_.memory_viewer = true;
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                        disasm_address_ = addr;
                        snprintf(disasm_address_input_, sizeof(disasm_address_input_), "0x%llX", (unsigned long long)addr);
                        if (disassembler_) {
                            auto data = dma_->ReadMemory(selected_pid_, addr, 512);
                            if (!data.empty()) {
                                disasm_instructions_ = disassembler_->Disassemble(data, addr);
                            }
                        }
                        panels_.disassembly = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    ImGui::EndPopup();
                }

                // Tooltip with module offset
                if (ImGui::IsItemHovered()) {
                    uint64_t offset = addr - selected_module_base_;
                    ImGui::SetTooltip("%s+0x%llX\nRight-click for options",
                        selected_module_name_.c_str(), (unsigned long long)offset);
                }

                ImGui::PopID();

                // Context column (module+offset)
                ImGui::TableNextColumn();
                uint64_t offset = addr - selected_module_base_;
                ImGui::TextDisabled("%s+0x%llX", selected_module_name_.c_str(), (unsigned long long)offset);
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace orpheus::ui
