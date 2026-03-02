#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/write_finder.h"
#include "analysis/disassembler.h"
#include "utils/logger.h"
#include "utils/search_history.h"
#include <cstdio>
#include <future>
#include <mutex>
#include <set>
#include <map>

namespace orpheus::ui {

void Application::RenderWriteTracer() {
    ImGui::Begin("Write Tracer", &panels_.write_tracer);

    if (selected_pid_ == 0 || !dma_ || !dma_->IsConnected()) {
        EmptyState("No process selected", "Select a process and module to trace memory writes");
        ImGui::End();
        return;
    }

    // Poll async result
    if (write_tracing_ && write_trace_future_.valid()) {
        auto status = write_trace_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            try {
                write_trace_result_ = write_trace_future_.get();
                write_trace_error_.clear();
                LOG_INFO("Write trace found {} direct writes, {} call graph nodes",
                         write_trace_result_.direct_writes.size(),
                         write_trace_result_.call_graph.size());
                AddToast("Write trace: " + std::to_string(write_trace_result_.direct_writes.size()) + " writes found", 1);
            } catch (const std::exception& e) {
                write_trace_error_ = e.what();
                LOG_ERROR("Write trace failed: {}", e.what());
                AddToast("Write trace failed: " + std::string(e.what()), 2);
            }
            write_tracing_ = false;
            write_trace_progress_ = 1.0f;
        }
    }

    // Module header
    ModuleHeader(selected_module_name_.c_str(), selected_module_base_, selected_module_size_);

    // Input row
    ImGui::Text("Target Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    bool enter_pressed = ImGui::InputText("##write_target", write_target_input_, sizeof(write_target_input_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (search_history_) {
        if (HistoryDropdown("write_target", write_target_input_, sizeof(write_target_input_),
                            search_history_->Get("write_target"))) {
            // Selected from history
        }
        ImGui::SameLine();
    }

    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    ImGui::InputInt("##depth", &write_trace_depth_);
    write_trace_depth_ = std::clamp(write_trace_depth_, 1, 10);
    ImGui::SameLine();

    // Require function recovery to be done
    bool has_functions = !recovered_functions_.empty();
    bool can_trace = selected_module_base_ != 0 && !write_tracing_ && has_functions;

    if (!can_trace) ImGui::BeginDisabled();
    if (AccentButton("Trace") || (enter_pressed && can_trace)) {
        auto parsed = ParseHexAddress(write_target_input_);
        if (parsed) {
            if (search_history_) search_history_->Add("write_target", write_target_input_);

            write_tracing_ = true;
            write_trace_cancel_ = false;
            write_trace_error_.clear();
            write_trace_progress_ = 0.0f;
            write_trace_progress_stage_ = "Starting...";

            uint64_t target = *parsed;
            uint32_t pid = selected_pid_;
            uint64_t module_base = selected_module_base_;
            uint32_t module_size = selected_module_size_;
            std::string module_name = selected_module_name_;
            int depth = write_trace_depth_;
            auto* dma = dma_.get();

            // Build map from vector for background thread
            std::map<uint64_t, analysis::FunctionInfo> functions;
            for (const auto& f : recovered_functions_) {
                functions[f.entry_address] = f;
            }

            write_trace_future_ = std::async(std::launch::async,
                [target, pid, module_base, module_size, module_name, depth,
                 dma, functions = std::move(functions),
                 &cancel = write_trace_cancel_,
                 &progress_stage = write_trace_progress_stage_,
                 &progress = write_trace_progress_,
                 &progress_mutex = progress_mutex_]() -> analysis::WriteTraceResult {

                analysis::WriteFinder finder(
                    [pid, dma](uint64_t addr, size_t size) {
                        return dma->ReadMemory(pid, addr, size);
                    }, true);

                return finder.TraceWrites(target, functions, module_base, module_size,
                    module_name, depth,
                    [&](const std::string& stage, float p) {
                        { std::lock_guard<std::mutex> lock(progress_mutex); progress_stage = stage; }
                        progress = p;
                    }, &cancel);
            });
        }
    }
    if (!can_trace) ImGui::EndDisabled();

    if (!has_functions && !write_tracing_) {
        ImGui::SameLine();
        ImGui::TextColored(colors::Warning, "(Requires function recovery)");
    }

    // Cancel button
    if (write_tracing_) {
        ImGui::SameLine();
        if (DangerButton("Cancel")) {
            write_trace_cancel_ = true;
        }
    }

    // Progress
    if (write_tracing_) {
        std::string stage_text;
        { std::lock_guard<std::mutex> lock(progress_mutex_); stage_text = write_trace_progress_stage_; }
        ImGui::TextColored(colors::Warning, "%s", stage_text.c_str());
        ProgressBarWithText(write_trace_progress_);
    }

    // Error
    if (!write_trace_error_.empty()) {
        ErrorText("Error: %s", write_trace_error_.c_str());
    }

    ImGui::Separator();

    // Results
    const auto& result = write_trace_result_;
    if (result.direct_writes.empty() && !write_tracing_) {
        EmptyState("No results", "Enter a target address and click Trace to find code that writes to it");
        ImGui::End();
        return;
    }

    // Summary
    ImGui::TextColored(colors::Info, "Target: %s", FormatAddress(result.target_address));
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu writes in %zu functions | %zu call graph nodes",
                       result.direct_writes.size(),
                       [&]() {
                           std::set<uint64_t> unique_funcs;
                           for (const auto& w : result.direct_writes) unique_funcs.insert(w.function_address);
                           return unique_funcs.size();
                       }(),
                       result.call_graph.size());

    // Tab bar: Direct Writes | Call Graph
    if (ImGui::BeginTabBar("##WriteTracerTabs")) {
        // ==================== Direct Writes Tab ====================
        if (ImGui::BeginTabItem("Direct Writes")) {
            if (ImGui::BeginTable("##WriteResults", 4, layout::kStandardTableFlags)) {
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, layout::kColumnAddress);
                ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin((int)result.direct_writes.size());

                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                        const auto& w = result.direct_writes[i];

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(i);

                        char addr_buf[32];
                        FormatAddressBuf(addr_buf, sizeof(addr_buf), w.instruction_address);

                        if (ImGui::Selectable(addr_buf, false,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                NavigateToAddress(w.instruction_address);
                            }
                        }

                        // Context menu
                        if (ImGui::BeginPopupContextItem("##WriteCtx")) {
                            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                                disasm_address_ = w.instruction_address;
                                snprintf(disasm_address_input_, sizeof(disasm_address_input_),
                                         "0x%llX", (unsigned long long)w.instruction_address);
                                if (disassembler_) {
                                    auto data = dma_->ReadMemory(selected_pid_, w.instruction_address, 512);
                                    if (!data.empty()) {
                                        disasm_instructions_ = disassembler_->Disassemble(data, w.instruction_address);
                                    }
                                }
                                panels_.disassembly = true;
                            }
                            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                                memory_address_ = w.instruction_address;
                                snprintf(address_input_, sizeof(address_input_),
                                         "0x%llX", (unsigned long long)w.instruction_address);
                                memory_data_ = dma_->ReadMemory(selected_pid_, w.instruction_address, 256);
                                panels_.memory_viewer = true;
                            }
                            if (w.function_address != 0) {
                                if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_DIAGRAM_PROJECT " View Function", "View Function"))) {
                                    disasm_address_ = w.function_address;
                                    snprintf(disasm_address_input_, sizeof(disasm_address_input_),
                                             "0x%llX", (unsigned long long)w.function_address);
                                    if (disassembler_) {
                                        auto data = dma_->ReadMemory(selected_pid_, w.function_address, 1024);
                                        if (!data.empty()) {
                                            disasm_instructions_ = disassembler_->Disassemble(data, w.function_address);
                                        }
                                    }
                                    panels_.disassembly = true;
                                }
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                                ImGui::SetClipboardText(addr_buf);
                            }
                            ImGui::EndPopup();
                        }

                        // Tooltip
                        if (ImGui::IsItemHovered()) {
                            uint64_t offset = w.instruction_address - selected_module_base_;
                            ImGui::SetTooltip("%s+0x%llX\n%s\nRight-click for options",
                                selected_module_name_.c_str(), (unsigned long long)offset,
                                w.full_text.c_str());
                        }

                        ImGui::PopID();

                        // Function column
                        ImGui::TableNextColumn();
                        if (w.function_address != 0) {
                            ImGui::TextColored(colors::Info, "%s", w.function_name.c_str());
                        }

                        // Instruction column
                        ImGui::TableNextColumn();
                        ImGui::Text("%s %s", w.mnemonic.c_str(), w.operands.c_str());

                        // Bytes column
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", FormatHexBytes(w.bytes));
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ==================== Call Graph Tab ====================
        if (ImGui::BeginTabItem("Call Graph")) {
            if (result.call_graph.empty()) {
                EmptyState("No call graph data", "Run a trace to build the reverse call graph");
            } else {
                // Organize by depth
                std::map<int, std::vector<const analysis::CallGraphNode*>> by_depth;
                for (const auto& [addr, node] : result.call_graph) {
                    by_depth[node.depth].push_back(&node);
                }

                for (const auto& [depth, nodes] : by_depth) {
                    const char* depth_label = depth == 0 ? "Direct Writers" : "Callers";
                    char section_label[64];
                    if (depth == 0)
                        snprintf(section_label, sizeof(section_label), "Direct Writers (%zu)", nodes.size());
                    else
                        snprintf(section_label, sizeof(section_label), "Depth %d Callers (%zu)", depth, nodes.size());

                    ImVec4 color = depth == 0 ? ImVec4(1.0f, 0.5f, 0.2f, 1.0f) :   // Orange for writers
                                   depth == 1 ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f) :   // Blue for depth 1
                                                ImVec4(0.5f, 0.5f, 0.8f, 1.0f);     // Lighter blue

                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    bool open = ImGui::TreeNodeEx(section_label, ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::PopStyleColor();

                    if (open) {
                        for (const auto* node : nodes) {
                            char node_label[256];
                            snprintf(node_label, sizeof(node_label), "%s (0x%llX)",
                                     node->name.c_str(), (unsigned long long)node->address);

                            ImGui::PushID((int)node->address);

                            if (ImGui::TreeNode(node_label)) {
                                // Write instructions (for direct writers)
                                if (!node->writes.empty()) {
                                    ImGui::TextDisabled("Writes:");
                                    for (const auto& w : node->writes) {
                                        ImGui::BulletText("0x%llX: %s %s",
                                            (unsigned long long)w.instruction_address,
                                            w.mnemonic.c_str(), w.operands.c_str());
                                    }
                                }

                                // Callers
                                if (!node->callers.empty()) {
                                    ImGui::TextDisabled("Called by:");
                                    for (uint64_t caller : node->callers) {
                                        auto it = result.call_graph.find(caller);
                                        if (it != result.call_graph.end()) {
                                            ImGui::BulletText("%s", it->second.name.c_str());
                                        } else {
                                            ImGui::BulletText("0x%llX (outside graph)", (unsigned long long)caller);
                                        }
                                    }
                                }

                                // Callees in graph
                                if (!node->callees_in_graph.empty()) {
                                    ImGui::TextDisabled("Calls (in graph):");
                                    for (uint64_t callee : node->callees_in_graph) {
                                        auto it = result.call_graph.find(callee);
                                        if (it != result.call_graph.end()) {
                                            ImGui::BulletText("%s", it->second.name.c_str());
                                        }
                                    }
                                }

                                // Context menu for function
                                if (ImGui::SmallButton("Disassemble")) {
                                    disasm_address_ = node->address;
                                    snprintf(disasm_address_input_, sizeof(disasm_address_input_),
                                             "0x%llX", (unsigned long long)node->address);
                                    if (disassembler_) {
                                        auto data = dma_->ReadMemory(selected_pid_, node->address, 1024);
                                        if (!data.empty()) {
                                            disasm_instructions_ = disassembler_->Disassemble(data, node->address);
                                        }
                                    }
                                    panels_.disassembly = true;
                                }

                                ImGui::TreePop();
                            }

                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace orpheus::ui
