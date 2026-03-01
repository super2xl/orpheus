#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "utils/logger.h"
#include <cstdio>

namespace orpheus::ui {

void Application::RenderPointerChain() {
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
    ImGui::Begin("Pointer Chain Resolver", &panels_.pointer_chain);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to resolve pointer chains");
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored(colors::Info, "Follow pointer chains to find dynamic addresses");
    ImGui::Separator();
    ImGui::Spacing();

    // Input section
    ImGui::Text("Base Address:");
    ImGui::SameLine(120);
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##ptr_base", "e.g. 7FF600000000", pointer_base_input_, sizeof(pointer_base_input_));

    ImGui::Text("Offsets:");
    ImGui::SameLine(120);
    ImGui::SetNextItemWidth(350);
    ImGui::InputTextWithHint("##ptr_offsets", "e.g. 0x10, 0x20, 0x8 (comma-separated)", pointer_offsets_input_, sizeof(pointer_offsets_input_));

    ImGui::Spacing();

    // Resolve button
    if (AccentButton("Resolve Chain", ImVec2(120, 0))) {
        pointer_chain_results_.clear();
        pointer_chain_error_.clear();
        pointer_final_address_ = 0;

        // Parse base address
        auto base_opt = ParseHexAddress(pointer_base_input_);
        uint64_t base = 0;
        if (base_opt) {
            base = *base_opt;
        } else {
            pointer_chain_error_ = "Invalid base address";
        }

        if (pointer_chain_error_.empty() && base == 0) {
            pointer_chain_error_ = "Base address cannot be 0";
        }

        // Parse offsets
        std::vector<int64_t> offsets;
        if (pointer_chain_error_.empty() && strlen(pointer_offsets_input_) > 0) {
            std::string offsets_str = pointer_offsets_input_;
            size_t pos = 0;
            while (pos < offsets_str.length()) {
                while (pos < offsets_str.length() && (offsets_str[pos] == ' ' || offsets_str[pos] == ',')) pos++;
                if (pos >= offsets_str.length()) break;

                size_t end = pos;
                while (end < offsets_str.length() && offsets_str[end] != ',' && offsets_str[end] != ' ') end++;

                std::string off_str = offsets_str.substr(pos, end - pos);
                try {
                    bool negative = false;
                    if (!off_str.empty() && off_str[0] == '-') {
                        negative = true;
                        off_str = off_str.substr(1);
                    }
                    if (off_str.length() > 2 && (off_str[0] == '0' && (off_str[1] == 'x' || off_str[1] == 'X'))) {
                        off_str = off_str.substr(2);
                    }
                    int64_t offset = static_cast<int64_t>(std::stoull(off_str, nullptr, 16));
                    if (negative) offset = -offset;
                    offsets.push_back(offset);
                } catch (...) {
                    pointer_chain_error_ = "Invalid offset: " + off_str;
                    break;
                }
                pos = end;
            }
        }

        // Resolve the chain
        if (pointer_chain_error_.empty()) {
            uint64_t current = base;
            pointer_chain_results_.push_back({current, 0});

            for (size_t i = 0; i < offsets.size(); i++) {
                auto ptr_opt = dma_->Read<uint64_t>(selected_pid_, current);
                if (!ptr_opt) {
                    pointer_chain_error_ = "Failed to read pointer at " + std::string(FormatAddress(current));
                    break;
                }

                uint64_t ptr_value = *ptr_opt;
                pointer_chain_results_.back().second = ptr_value;
                current = ptr_value + offsets[i];
                pointer_chain_results_.push_back({current, 0});
            }

            if (pointer_chain_error_.empty()) {
                pointer_final_address_ = current;
                auto final_ptr = dma_->Read<uint64_t>(selected_pid_, current);
                if (final_ptr) {
                    pointer_chain_results_.back().second = *final_ptr;
                }
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", button_size::Small)) {
        pointer_chain_results_.clear();
        pointer_chain_error_.clear();
        pointer_final_address_ = 0;
    }

    ImGui::SameLine();
    HelpMarker("Enter a base address and comma-separated offsets.\n"
               "The resolver will dereference at each step:\n"
               "  [[base]+offset1]+offset2 = final address");

    ImGui::Separator();
    ImGui::Spacing();

    // Error display
    if (!pointer_chain_error_.empty()) {
        ErrorText("Error: %s", pointer_chain_error_.c_str());
        ImGui::Spacing();
    }

    // Results display
    if (!pointer_chain_results_.empty()) {
        GroupLabel("Chain Visualization");

        ImGui::BeginChild("ChainViz", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);

        // Parse offsets again for display
        std::vector<int64_t> offsets;
        if (strlen(pointer_offsets_input_) > 0) {
            std::string offsets_str = pointer_offsets_input_;
            size_t pos = 0;
            while (pos < offsets_str.length()) {
                while (pos < offsets_str.length() && (offsets_str[pos] == ' ' || offsets_str[pos] == ',')) pos++;
                if (pos >= offsets_str.length()) break;
                size_t end = pos;
                while (end < offsets_str.length() && offsets_str[end] != ',' && offsets_str[end] != ' ') end++;
                std::string off_str = offsets_str.substr(pos, end - pos);
                try {
                    bool negative = false;
                    if (!off_str.empty() && off_str[0] == '-') { negative = true; off_str = off_str.substr(1); }
                    if (off_str.length() > 2 && (off_str[0] == '0' && (off_str[1] == 'x' || off_str[1] == 'X'))) off_str = off_str.substr(2);
                    int64_t offset = static_cast<int64_t>(std::stoull(off_str, nullptr, 16));
                    if (negative) offset = -offset;
                    offsets.push_back(offset);
                } catch (...) {}
                pos = end;
            }
        }

        for (size_t i = 0; i < pointer_chain_results_.size(); i++) {
            const auto& [addr, value] = pointer_chain_results_[i];

            ImGui::PushID(static_cast<int>(i));

            // Step indicator
            if (i == 0) {
                ImGui::TextColored(colors::Info, "Base");
            } else if (i == pointer_chain_results_.size() - 1) {
                ImGui::TextColored(colors::Success, "Final");
            } else {
                ImGui::TextColored(colors::Muted, "Step %zu", i);
            }

            ImGui::SameLine(60);

            // Address (clickable)
            char addr_buf[32];
            FormatAddressBuf(addr_buf, sizeof(addr_buf), addr);
            if (ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_None, ImVec2(140, 0))) {
                NavigateToAddress(addr);
            }
            HelpTooltip("Click to navigate to this address in Memory Viewer");

            // Show dereferenced value
            if (i < pointer_chain_results_.size() - 1 && value != 0) {
                ImGui::SameLine();
                ImGui::TextColored(colors::Muted, "->");
                ImGui::SameLine();
                ImGui::TextColored(colors::Warning, "[%s]", FormatAddress(value));

                if (i < offsets.size()) {
                    ImGui::SameLine();
                    if (offsets[i] >= 0) {
                        ImGui::TextColored(colors::Success, "+ 0x%llX", (unsigned long long)offsets[i]);
                    } else {
                        ImGui::TextColored(colors::Error, "- 0x%llX", (unsigned long long)(-offsets[i]));
                    }
                }
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        // Final address section
        if (pointer_final_address_ != 0) {
            ImGui::Spacing();
            ImGui::Separator();

            ImGui::TextColored(colors::Success, "Final Address: %s", FormatAddress(pointer_final_address_));

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy", "Copy"))) {
                char buf[32];
                FormatAddressBuf(buf, sizeof(buf), pointer_final_address_);
                ImGui::SetClipboardText(buf);
                status_message_ = "Address copied to clipboard";
                status_timer_ = 2.0f;
            }

            ImGui::Spacing();

            // Read final value as different types
            ImGui::Text("Read Final Value As:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            const char* type_names[] = { "Int32", "Float", "Int64", "Double" };
            ImGui::Combo("##ptr_type", &pointer_final_type_, type_names, 4);

            ImGui::SameLine();
            if (ImGui::Button("Read")) {
                switch (pointer_final_type_) {
                    case 0: {
                        auto val = dma_->Read<int32_t>(selected_pid_, pointer_final_address_);
                        if (val) { status_message_ = "Int32: " + std::to_string(*val); status_timer_ = 5.0f; }
                        break;
                    }
                    case 1: {
                        auto val = dma_->Read<float>(selected_pid_, pointer_final_address_);
                        if (val) { status_message_ = "Float: " + std::to_string(*val); status_timer_ = 5.0f; }
                        break;
                    }
                    case 2: {
                        auto val = dma_->Read<int64_t>(selected_pid_, pointer_final_address_);
                        if (val) { status_message_ = "Int64: " + std::to_string(*val); status_timer_ = 5.0f; }
                        break;
                    }
                    case 3: {
                        auto val = dma_->Read<double>(selected_pid_, pointer_final_address_);
                        if (val) { status_message_ = "Double: " + std::to_string(*val); status_timer_ = 5.0f; }
                        break;
                    }
                }
            }

            ImGui::Spacing();
            ImGui::BeginChild("FinalValueDisplay", ImVec2(0, 60), true);

            auto data = dma_->ReadMemory(selected_pid_, pointer_final_address_, 8);
            if (!data.empty()) {
                if (data.size() >= 4) {
                    int32_t int32_val = *reinterpret_cast<int32_t*>(data.data());
                    float float_val = *reinterpret_cast<float*>(data.data());
                    ImGui::Text("Int32: %d  |  Float: %.6f", int32_val, float_val);
                }
                if (data.size() >= 8) {
                    int64_t int64_val = *reinterpret_cast<int64_t*>(data.data());
                    double double_val = *reinterpret_cast<double*>(data.data());
                    ImGui::Text("Int64: %lld  |  Double: %.6f", (long long)int64_val, double_val);

                    ImGui::TextColored(colors::Muted, "Hex: %s", FormatHexBytes(data.data(), 8, 8));
                }
            }

            ImGui::EndChild();
        }
    } else if (pointer_chain_error_.empty()) {
        EmptyState("Enter a base address and offsets", "Then click 'Resolve Chain'");
    }

    ImGui::End();
}

} // namespace orpheus::ui
