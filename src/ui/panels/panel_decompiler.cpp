#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/icons.h"
#include <imgui.h>
#include <cstdio>

#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
#include "decompiler/decompiler.hh"
#include "utils/logger.h"
#endif

namespace orpheus::ui {

void Application::RenderDecompiler() {
    ImGui::Begin("Decompiler", &panels_.decompiler);

#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    // Initialize decompiler on first use
    if (!decompiler_initialized_ && !decompiler_) {
        decompiler_ = std::make_unique<Decompiler>();
        DecompilerConfig config;

        auto sleigh_dir = RuntimeManager::Instance().GetSleighDirectory();
        if (!sleigh_dir.empty() && std::filesystem::exists(sleigh_dir)) {
            config.sleigh_spec_path = sleigh_dir.string();
        } else {
            LOG_WARN("SLEIGH directory not found, decompiler may not work");
            config.sleigh_spec_path = "";
        }
        LOG_INFO("SLEIGH specs path: {}", config.sleigh_spec_path);

        config.processor = "x86";
        config.address_size = 64;
        config.little_endian = true;
        config.compiler_spec = "windows";

        if (decompiler_->Initialize(config)) {
            decompiler_initialized_ = true;
            LOG_INFO("Ghidra decompiler initialized");
        } else {
            LOG_ERROR("Failed to initialize decompiler: {}", decompiler_->GetLastError());
        }
    }

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected()) {
        // Address input
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("Address", decompile_address_input_, sizeof(decompile_address_input_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            decompile_address_ = strtoull(decompile_address_input_, nullptr, 16);
        }

        ImGui::SameLine();
        bool has_address_input = strlen(decompile_address_input_) > 0;
        bool can_decompile = decompiler_initialized_ && has_address_input;
        if (!can_decompile) ImGui::BeginDisabled();
        if (AccentButton("Decompile")) {
            decompile_address_ = strtoull(decompile_address_input_, nullptr, 16);
            if (decompile_address_ == 0) {
                decompiled_code_ = "// Error: Invalid address (enter a hex address like 7FF600001000)";
            } else if (decompiler_) {
                decompiler_->SetMemoryCallback([this](uint64_t addr, size_t size, uint8_t* buffer) -> bool {
                    auto data = dma_->ReadMemory(selected_pid_, addr, static_cast<uint32_t>(size));
                    if (data.size() >= size) {
                        memcpy(buffer, data.data(), size);
                        return true;
                    }
                    return false;
                });

                auto result = decompiler_->DecompileFunction(decompile_address_);
                if (result.success) {
                    decompiled_code_ = result.c_code;
                    LOG_INFO("Decompiled function at 0x{:X}", decompile_address_);
                } else {
                    decompiled_code_ = "// Decompilation failed: " + result.error;
                    LOG_WARN("Decompilation failed: {}", result.error);
                }
            }
        }
        if (!can_decompile) ImGui::EndDisabled();

        if (!decompiler_initialized_) {
            ImGui::SameLine();
            ImGui::TextColored(colors::Warning, "(Decompiler not initialized)");
        }

        ImGui::Separator();

        // Output area
        if (font_mono_) ImGui::PushFont(font_mono_);

        ImGui::BeginChild("##DecompilerOutput", ImVec2(0, 0), true,
            ImGuiWindowFlags_HorizontalScrollbar);

        if (!decompiled_code_.empty()) {
            ImGui::TextUnformatted(decompiled_code_.c_str());
        } else {
            EmptyState("Enter an address and click Decompile");
        }

        ImGui::EndChild();
        if (font_mono_) ImGui::PopFont();
    } else {
        EmptyState("No process selected", "Select a process to decompile functions");
    }
#else
    ImGui::TextColored(colors::Warning, "Decompiler not available");
    ImGui::TextDisabled("Build with -DORPHEUS_BUILD_DECOMPILER=ON to enable");
#endif

    ImGui::End();
}

} // namespace orpheus::ui
