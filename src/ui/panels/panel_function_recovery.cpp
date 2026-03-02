#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/function_recovery.h"
#include "analysis/disassembler.h"
#include "utils/bookmarks.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace orpheus::ui {

void Application::RenderFunctionRecovery() {
    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Function Recovery", &panels_.function_recovery);

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

    // Check for async recovery completion
    if (function_recovery_running_ && function_recovery_future_.valid()) {
        auto status = function_recovery_future_.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            auto functions = function_recovery_future_.get();
            recovered_functions_.clear();
            recovered_functions_.reserve(functions.size());
            for (auto& [addr, func] : functions) {
                recovered_functions_.push_back(std::move(func));
            }
            function_recovery_running_ = false;
            function_recovery_progress_ = 1.0f;
            function_recovery_progress_stage_ = "Complete";
            LOG_INFO("Function recovery complete: {} functions found in {}",
                     recovered_functions_.size(), function_recovery_module_name_);
            AddToast("Function recovery: " + std::to_string(recovered_functions_.size()) + " functions in " + function_recovery_module_name_, 1);
        }
    }

    // Module selector dropdown
    ImGui::Text("Module:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);

    std::string combo_preview = function_recovery_module_name_.empty()
        ? "Select a module..."
        : function_recovery_module_name_;

    if (ImGui::BeginCombo("##ModuleSelector", combo_preview.c_str())) {
        for (const auto& mod : cached_modules_) {
            bool is_selected = (mod.base_address == function_recovery_module_base_);
            std::string label = mod.name + " (" + std::to_string(mod.size / 1024) + " KB)";

            if (ImGui::Selectable(label.c_str(), is_selected)) {
                function_recovery_module_base_ = mod.base_address;
                function_recovery_module_size_ = mod.size;
                function_recovery_module_name_ = mod.name;
                recovered_functions_.clear();
                function_recovery_progress_ = 0.0f;
                function_recovery_progress_stage_.clear();
            }

            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Use selected module button
    ImGui::SameLine();
    if (selected_module_base_ != 0) {
        if (ImGui::Button("Use Selected")) {
            function_recovery_module_base_ = selected_module_base_;
            function_recovery_module_size_ = selected_module_size_;
            function_recovery_module_name_ = selected_module_name_;
            recovered_functions_.clear();
            function_recovery_progress_ = 0.0f;
            function_recovery_progress_stage_.clear();
        }
        HelpTooltip(("Use: " + selected_module_name_ + " @ " + std::string(FormatAddress(selected_module_base_))).c_str());
    }

    ImGui::Separator();

    // Recovery options
    ImGui::Text("Recovery Options:");
    ImGui::SameLine();
    ImGui::Checkbox("Prologues", &function_recovery_use_prologues_);
    HelpTooltip("Scan for function prologues (push rbp, sub rsp, etc.)");
    ImGui::SameLine();
    ImGui::Checkbox("Follow calls", &function_recovery_follow_calls_);
    HelpTooltip("Mark CALL instruction targets as function entry points");
    ImGui::SameLine();
    ImGui::Checkbox("Use .pdata", &function_recovery_use_pdata_);
    HelpTooltip("Parse PE exception directory for x64 function info");

    ImGui::SameLine(ImGui::GetWindowWidth() - 180);

    // Recover button
    bool can_recover = function_recovery_module_base_ != 0 && !function_recovery_running_;
    if (!can_recover) ImGui::BeginDisabled();
    if (AccentButton("Recover Functions", ImVec2(160, 0))) {
        function_recovery_running_ = true;
        function_recovery_progress_ = 0.0f;
        function_recovery_progress_stage_ = "Starting...";
        recovered_functions_.clear();

        uint32_t pid = selected_pid_;
        uint64_t module_base = function_recovery_module_base_;
        uint32_t module_size = function_recovery_module_size_;
        bool use_prologues = function_recovery_use_prologues_;
        bool follow_calls = function_recovery_follow_calls_;
        bool use_pdata = function_recovery_use_pdata_;
        auto* dma = dma_.get();

        function_recovery_future_ = std::async(std::launch::async,
            [pid, module_base, module_size, use_prologues, follow_calls, use_pdata, dma,
             &progress_stage = function_recovery_progress_stage_,
             &progress = function_recovery_progress_,
             &progress_mutex = progress_mutex_]() -> std::map<uint64_t, analysis::FunctionInfo> {

            analysis::FunctionRecovery recovery(
                [dma, pid](uint64_t addr, size_t size) {
                    return dma->ReadMemory(pid, addr, size);
                },
                module_base,
                module_size,
                true
            );

            analysis::FunctionRecoveryOptions opts;
            opts.use_prologues = use_prologues;
            opts.follow_calls = follow_calls;
            opts.use_exception_data = use_pdata;
            opts.max_functions = 100000;

            return recovery.RecoverFunctions(opts,
                [&progress_stage, &progress, &progress_mutex](const std::string& stage, float prog) {
                    { std::lock_guard<std::mutex> lock(progress_mutex); progress_stage = stage; }
                    progress = prog;
                });
        });
    }
    if (!can_recover) ImGui::EndDisabled();

    // Progress indicator
    if (function_recovery_running_) {
        ImGui::SameLine();
        std::string stage_text;
        { std::lock_guard<std::mutex> lock(progress_mutex_); stage_text = function_recovery_progress_stage_; }
        ImGui::TextColored(colors::Warning, "%s", stage_text.c_str());
        ProgressBarWithText(function_recovery_progress_);
    }

    ImGui::Separator();

    // Results section
    if (!recovered_functions_.empty()) {
        ImGui::Text("Functions: %zu", recovered_functions_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##FuncFilter", "Filter by name or address...",
                                 function_filter_, sizeof(function_filter_));

        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Results")) {
            recovered_functions_.clear();
            function_recovery_progress_ = 0.0f;
            function_recovery_progress_stage_.clear();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_EXPORT " Export CSV", "Export CSV"))) {
            std::string filename = function_recovery_module_name_ + "_functions.csv";
            std::ofstream out(filename);
            if (out.is_open()) {
                out << "Address,Size,Name,Source,Confidence,IsThunk,IsLeaf\n";
                for (const auto& func : recovered_functions_) {
                    out << "0x" << std::hex << func.entry_address << ","
                        << std::dec << func.size << ","
                        << "\"" << func.name << "\","
                        << func.GetSourceString() << ","
                        << func.confidence << ","
                        << (func.is_thunk ? "true" : "false") << ","
                        << (func.is_leaf ? "true" : "false") << "\n";
                }
                out.close();
                LOG_INFO("Exported functions to {}", filename);
            }
        }

        // Build filtered list (custom: searches name AND address)
        std::string filter_str = ToLower(function_filter_);

        std::vector<size_t> filtered_indices;
        for (size_t i = 0; i < recovered_functions_.size(); i++) {
            if (filter_str.empty()) {
                filtered_indices.push_back(i);
            } else {
                const auto& func = recovered_functions_[i];
                if (MatchesFilter(func.name, filter_str)) {
                    filtered_indices.push_back(i);
                    continue;
                }
                // Also check address as hex
                char addr_buf[32];
                snprintf(addr_buf, sizeof(addr_buf), "%llx", (unsigned long long)func.entry_address);
                if (std::string(addr_buf).find(filter_str) != std::string::npos) {
                    filtered_indices.push_back(i);
                }
            }
        }

        ImGui::Text("Showing: %zu", filtered_indices.size());

        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        if (ImGui::BeginTable("##FunctionsTable", 5,
            layout::kSortableTableFlags | ImGuiTableFlags_SizingFixedFit)) {

            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, layout::kColumnAddress);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, layout::kColumnProtection);
            ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, layout::kColumnMethods);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Handle sorting
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                    function_recovery_sort_column_ = sort_specs->Specs[0].ColumnIndex;
                    function_recovery_sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                    sort_specs->SpecsDirty = false;
                }
            }

            // Sort filtered indices
            auto& funcs = recovered_functions_;
            int sort_col = function_recovery_sort_column_;
            bool ascending = function_recovery_sort_ascending_;
            std::sort(filtered_indices.begin(), filtered_indices.end(),
                [&funcs, sort_col, ascending](size_t a, size_t b) {
                    const auto& fa = funcs[a];
                    const auto& fb = funcs[b];
                    int cmp = 0;
                    switch (sort_col) {
                        case 0: cmp = (fa.entry_address < fb.entry_address) ? -1 : (fa.entry_address > fb.entry_address) ? 1 : 0; break;
                        case 1: cmp = (fa.size < fb.size) ? -1 : (fa.size > fb.size) ? 1 : 0; break;
                        case 2: cmp = fa.name.compare(fb.name); break;
                        case 3: cmp = fa.GetSourceString().compare(fb.GetSourceString()); break;
                        case 4: {
                            int flags_a = (fa.is_thunk ? 2 : 0) + (fa.is_leaf ? 1 : 0);
                            int flags_b = (fb.is_thunk ? 2 : 0) + (fb.is_leaf ? 1 : 0);
                            cmp = flags_a - flags_b;
                            break;
                        }
                    }
                    return ascending ? (cmp < 0) : (cmp > 0);
                });

            // Render with clipper
            ImGuiListClipper clipper;
            clipper.Begin((int)filtered_indices.size(), row_height);

            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const auto& func = recovered_functions_[filtered_indices[row]];

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);

                    // Address column
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)filtered_indices[row]);

                    char addr_buf[32];
                    FormatAddressBuf(addr_buf, sizeof(addr_buf), func.entry_address);

                    if (ImGui::Selectable(addr_buf, false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
                            decompile_address_ = func.entry_address;
                            snprintf(decompile_address_input_, sizeof(decompile_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            panels_.decompiler = true;
#else
                            NavigateToAddress(func.entry_address);
#endif
                        }
                    }

                    // Context menu
                    if (ImGui::BeginPopupContextItem("##FuncContext")) {
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                            ImGui::SetClipboardText(addr_buf);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_CODE " View in Disassembly", "View in Disassembly"))) {
                            disasm_address_ = func.entry_address;
                            snprintf(disasm_address_input_, sizeof(disasm_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            auto data = dma_->ReadMemory(selected_pid_, func.entry_address, 4096);
                            if (!data.empty() && disassembler_) {
                                disasm_instructions_ = disassembler_->Disassemble(data, func.entry_address);
                            }
                            panels_.disassembly = true;
                        }
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                            memory_address_ = func.entry_address;
                            snprintf(address_input_, sizeof(address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            memory_data_ = dma_->ReadMemory(selected_pid_, func.entry_address, 512);
                            panels_.memory_viewer = true;
                        }
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_CODE " Decompile", "Decompile"))) {
                            decompile_address_ = func.entry_address;
                            snprintf(decompile_address_input_, sizeof(decompile_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            panels_.decompiler = true;
                        }
#endif
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_DIAGRAM_PROJECT " Build CFG", "Build CFG"))) {
                            cfg_function_addr_ = func.entry_address;
                            snprintf(cfg_address_input_, sizeof(cfg_address_input_),
                                     "0x%llX", (unsigned long long)func.entry_address);
                            panels_.cfg_viewer = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_BOOKMARK " Add Bookmark", "Add Bookmark"))) {
                            if (bookmarks_) {
                                std::string label = func.name.empty() ?
                                    "func_" + std::string(addr_buf) : func.name;
                                bookmarks_->Add(func.entry_address, label, "",
                                               "Functions", function_recovery_module_name_);
                            }
                        }
                        ImGui::EndPopup();
                    }

                    // Tooltip with details
                    if (ImGui::IsItemHovered()) {
                        uint64_t offset = func.entry_address - function_recovery_module_base_;
                        ImGui::BeginTooltip();
                        ImGui::Text("%s+0x%llX", function_recovery_module_name_.c_str(), (unsigned long long)offset);
                        if (!func.name.empty()) {
                            ImGui::Text("Name: %s", func.name.c_str());
                        }
                        ImGui::Text("Confidence: %.1f%%", func.confidence * 100.0f);
                        ImGui::Text("Instructions: %u, Blocks: %u", func.instruction_count, func.basic_block_count);
                        ImGui::TextDisabled("Double-click to decompile, Right-click for options");
                        ImGui::EndTooltip();
                    }

                    ImGui::PopID();

                    // Size column
                    ImGui::TableNextColumn();
                    if (func.size > 0) {
                        ImGui::Text("%u", func.size);
                    } else {
                        ImGui::TextDisabled("-");
                    }

                    // Name column
                    ImGui::TableNextColumn();
                    if (!func.name.empty()) {
                        ImGui::TextUnformatted(func.name.c_str());
                    } else {
                        ImGui::TextDisabled("(unnamed)");
                    }

                    // Source column with color coding
                    ImGui::TableNextColumn();
                    std::string src = func.GetSourceString();
                    ImVec4 src_color;
                    if (src == "pdata") {
                        src_color = colors::Success;
                    } else if (src == "prologue") {
                        src_color = colors::Info;
                    } else if (src == "call_target") {
                        src_color = colors::Warning;
                    } else if (src == "rtti") {
                        src_color = ImVec4(0.8f, 0.5f, 1.0f, 1.0f);  // Purple
                    } else {
                        src_color = colors::Muted;
                    }
                    ImGui::TextColored(src_color, "%s", src.c_str());

                    // Flags column
                    ImGui::TableNextColumn();
                    std::string flags;
                    if (func.is_thunk) flags += "T";
                    if (func.is_leaf) flags += "L";
                    if (!flags.empty()) {
                        ImGui::TextColored(colors::Warning, "%s", flags.c_str());
                        HelpTooltip("T=Thunk (jump to another function)\nL=Leaf (no calls to other functions)");
                    } else {
                        ImGui::TextDisabled("-");
                    }
                }
            }

            ImGui::EndTable();
        }
    } else if (!function_recovery_running_ && function_recovery_module_base_ != 0) {
        EmptyState("No functions recovered", "Click 'Recover Functions' to discover functions");
    }

    ImGui::Separator();

    // Find Function Containing feature
    ImGui::Text("Find Function Containing Address:");
    ImGui::SetNextItemWidth(form_width::Short);
    if (ImGui::InputText("##ContainingAddr", function_containing_input_, sizeof(function_containing_input_),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto parsed_containing = ParseHexAddress(function_containing_input_);
        if (parsed_containing && !recovered_functions_.empty()) {
            uint64_t target_addr = *parsed_containing;
            function_containing_result_addr_ = 0;
            function_containing_result_name_.clear();

            std::vector<const analysis::FunctionInfo*> sorted_funcs;
            sorted_funcs.reserve(recovered_functions_.size());
            for (const auto& func : recovered_functions_) {
                sorted_funcs.push_back(&func);
            }
            std::sort(sorted_funcs.begin(), sorted_funcs.end(),
                [](const analysis::FunctionInfo* a, const analysis::FunctionInfo* b) {
                    return a->entry_address < b->entry_address;
                });

            for (auto it = sorted_funcs.rbegin(); it != sorted_funcs.rend(); ++it) {
                if ((*it)->entry_address <= target_addr) {
                    if ((*it)->size > 0 && target_addr >= (*it)->entry_address + (*it)->size) {
                        continue;
                    }
                    function_containing_result_addr_ = (*it)->entry_address;
                    function_containing_result_name_ = (*it)->name;
                    break;
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Find")) {
        auto parsed_find = ParseHexAddress(function_containing_input_);
        if (parsed_find && !recovered_functions_.empty()) {
            uint64_t target_addr = *parsed_find;
            function_containing_result_addr_ = 0;
            function_containing_result_name_.clear();

            std::vector<const analysis::FunctionInfo*> sorted_funcs;
            sorted_funcs.reserve(recovered_functions_.size());
            for (const auto& func : recovered_functions_) {
                sorted_funcs.push_back(&func);
            }
            std::sort(sorted_funcs.begin(), sorted_funcs.end(),
                [](const analysis::FunctionInfo* a, const analysis::FunctionInfo* b) {
                    return a->entry_address < b->entry_address;
                });

            for (auto it = sorted_funcs.rbegin(); it != sorted_funcs.rend(); ++it) {
                if ((*it)->entry_address <= target_addr) {
                    if ((*it)->size > 0 && target_addr >= (*it)->entry_address + (*it)->size) {
                        continue;
                    }
                    function_containing_result_addr_ = (*it)->entry_address;
                    function_containing_result_name_ = (*it)->name;
                    break;
                }
            }
        }
    }

    // Show result
    if (function_containing_result_addr_ != 0) {
        ImGui::SameLine();
        ImGui::Text("->");
        ImGui::SameLine();
        char result_buf[32];
        FormatAddressBuf(result_buf, sizeof(result_buf), function_containing_result_addr_);
        if (ImGui::SmallButton(result_buf)) {
            NavigateToAddress(function_containing_result_addr_);
        }
        if (!function_containing_result_name_.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(colors::Info, "(%s)", function_containing_result_name_.c_str());
        }
        if (auto parsed_target = ParseHexAddress(function_containing_input_)) {
            uint64_t target = *parsed_target;
            if (target > function_containing_result_addr_) {
                ImGui::SameLine();
                ImGui::TextDisabled("+0x%llX", (unsigned long long)(target - function_containing_result_addr_));
            }
        }
    }

    ImGui::End();
}

} // namespace orpheus::ui
