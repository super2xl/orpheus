#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "emulation/emulator.h"
#include "utils/logger.h"
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace orpheus::ui {

void Application::RenderEmulatorPanel() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Emulator", &panels_.emulator);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to emulate code");
        ImGui::End();
        return;
    }

    // Emulator status
    bool has_emulator = emulator_ != nullptr && emulator_->IsInitialized();
    bool correct_pid = has_emulator && emulator_pid_ == selected_pid_;

    // Status header
    ImGui::BeginChild("EmuHeader", ImVec2(0, 60), true);
    if (correct_pid) {
        ImGui::TextColored(colors::Success, "EMULATOR ACTIVE");
        ImGui::SameLine(200);
        ImGui::Text("PID: %u  |  Process: %s", emulator_pid_, selected_process_name_.c_str());
        ImGui::Spacing();
        if (DangerButton("Destroy Emulator", ImVec2(150, 0))) {
            emulator_.reset();
            emulator_pid_ = 0;
            emu_last_result_ = "Emulator destroyed";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset CPU", ImVec2(100, 0))) {
            emulator_->ResetCPU();
            emu_last_result_ = "CPU registers reset to initial state";
        }
        ImGui::SameLine();
        if (ImGui::Button("Full Reset", ImVec2(100, 0))) {
            emulator_->Reset();
            if (emulator_->Initialize(dma_.get(), selected_pid_)) {
                emu_last_result_ = "Full reset complete - memory mappings cleared";
            }
        }
    } else {
        ImGui::TextColored(colors::Muted, "NO EMULATOR");
        ImGui::SameLine(200);
        ImGui::Text("Selected: PID %u  |  %s", selected_pid_, selected_process_name_.c_str());
        ImGui::Spacing();
        if (AccentButton("Create Emulator", ImVec2(150, 0))) {
            emulator_ = std::make_unique<emulation::Emulator>();
            if (emulator_->Initialize(dma_.get(), selected_pid_)) {
                emulator_pid_ = selected_pid_;
                emu_last_result_ = "Emulator initialized - lazy memory mapping enabled";
                LOG_INFO("Emulator created for PID {}", selected_pid_);
            } else {
                emu_last_result_ = "Failed: " + emulator_->GetLastError();
                emulator_.reset();
                LOG_ERROR("Failed to create emulator: {}", emu_last_result_);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Unicorn x64 CPU emulation with DMA memory access");
    }
    ImGui::EndChild();

    if (!correct_pid) {
        EmptyState("Create an emulator to access these features");
        ImGui::End();
        return;
    }

    ImGui::Spacing();

    // Main content area with tabs
    if (ImGui::BeginTabBar("EmulatorTabs", ImGuiTabBarFlags_None)) {

        // === EXECUTION TAB ===
        if (ImGui::BeginTabItem("Execution")) {
            ImGui::BeginChild("ExecutionChild", ImVec2(0, 0), false);

            GroupLabel("Execute Code");

            // Run to address
            ImGui::Text("Run until address:");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_start", "Start address (hex)", emu_start_addr_, sizeof(emu_start_addr_));
            ImGui::SameLine();
            ImGui::Text("->");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_end", "End address (hex)", emu_end_addr_, sizeof(emu_end_addr_));
            ImGui::SameLine();
            if (AccentButton("Run to End", ImVec2(100, 0))) {
                if (strlen(emu_start_addr_) > 0 && strlen(emu_end_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        uint64_t end = std::stoull(emu_end_addr_, nullptr, 16);
                        auto result = emulator_->Run(start, end);
                        if (result.success) {
                            std::stringstream ss;
                            ss << "Execution complete | Final RIP: 0x" << std::hex << std::uppercase << result.final_rip;
                            ss << " | RAX: 0x" << result.registers["rax"];
                            emu_last_result_ = ss.str();
                        } else {
                            emu_last_result_ = "Execution failed: " + result.error;
                        }
                    } catch (const std::exception& e) {
                        emu_last_result_ = std::string("Invalid address: ") + e.what();
                    }
                } else {
                    emu_last_result_ = "Enter both start and end addresses";
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Step N instructions
            ImGui::Text("Step instructions:");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_step_addr", "Start address (hex)", emu_start_addr_, sizeof(emu_start_addr_));
            ImGui::SameLine();
            ImGui::Text("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::InputText("##emu_count", emu_instr_count_, sizeof(emu_instr_count_));
            ImGui::SameLine();
            ImGui::Text("instructions");
            ImGui::SameLine();
            if (ImGui::Button("Step", ImVec2(100, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        size_t count = std::stoul(emu_instr_count_);
                        auto result = emulator_->RunInstructions(start, count);
                        if (result.success) {
                            std::stringstream ss;
                            ss << "Stepped " << std::dec << count << " instructions | Final RIP: 0x"
                               << std::hex << std::uppercase << result.final_rip;
                            emu_last_result_ = ss.str();
                        } else {
                            emu_last_result_ = "Step failed: " + result.error;
                        }
                    } catch (const std::exception& e) {
                        emu_last_result_ = std::string("Invalid input: ") + e.what();
                    }
                } else {
                    emu_last_result_ = "Enter a start address";
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Quick actions
            ImGui::Text("Quick Actions:");
            if (ImGui::Button("Step 1", ImVec2(60, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        auto result = emulator_->RunInstructions(start, 1);
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << result.final_rip;
                        strncpy(emu_start_addr_, ss.str().c_str() + 2, sizeof(emu_start_addr_) - 1);
                        emu_last_result_ = result.success ? "Stepped 1 instruction" : result.error;
                    } catch (...) {}
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Step 10", ImVec2(60, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        auto result = emulator_->RunInstructions(start, 10);
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << result.final_rip;
                        strncpy(emu_start_addr_, ss.str().c_str() + 2, sizeof(emu_start_addr_) - 1);
                        emu_last_result_ = result.success ? "Stepped 10 instructions" : result.error;
                    } catch (...) {}
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Step 100", ImVec2(60, 0))) {
                if (strlen(emu_start_addr_) > 0) {
                    try {
                        uint64_t start = std::stoull(emu_start_addr_, nullptr, 16);
                        auto result = emulator_->RunInstructions(start, 100);
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << result.final_rip;
                        strncpy(emu_start_addr_, ss.str().c_str() + 2, sizeof(emu_start_addr_) - 1);
                        emu_last_result_ = result.success ? "Stepped 100 instructions" : result.error;
                    } catch (...) {}
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // === REGISTERS TAB ===
        if (ImGui::BeginTabItem("Registers")) {
            ImGui::BeginChild("RegistersChild", ImVec2(0, 0), false);

            static char reg_edit_value[32] = {};
            static std::string editing_reg = "";

            GroupLabel("CPU Registers");

            // Read all registers once
            struct RegDisplay {
                const char* name;
                emulation::Reg reg;
                uint64_t value;
                bool valid;
            };

            std::vector<RegDisplay> regs = {
                {"RAX", emulation::Reg::RAX, 0, false},
                {"RBX", emulation::Reg::RBX, 0, false},
                {"RCX", emulation::Reg::RCX, 0, false},
                {"RDX", emulation::Reg::RDX, 0, false},
                {"RSI", emulation::Reg::RSI, 0, false},
                {"RDI", emulation::Reg::RDI, 0, false},
                {"RBP", emulation::Reg::RBP, 0, false},
                {"RSP", emulation::Reg::RSP, 0, false},
                {"R8",  emulation::Reg::R8,  0, false},
                {"R9",  emulation::Reg::R9,  0, false},
                {"R10", emulation::Reg::R10, 0, false},
                {"R11", emulation::Reg::R11, 0, false},
                {"R12", emulation::Reg::R12, 0, false},
                {"R13", emulation::Reg::R13, 0, false},
                {"R14", emulation::Reg::R14, 0, false},
                {"R15", emulation::Reg::R15, 0, false},
                {"RIP", emulation::Reg::RIP, 0, false},
                {"RFLAGS", emulation::Reg::RFLAGS, 0, false},
            };

            for (auto& r : regs) {
                auto val = emulator_->GetRegister(r.reg);
                if (val) {
                    r.value = *val;
                    r.valid = true;
                }
            }

            if (ImGui::BeginTable("RegTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < regs.size(); i += 2) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", regs[i].name);
                    ImGui::TableNextColumn();
                    if (regs[i].valid) {
                        ImGui::TextColored(colors::Info, "0x%016llX", regs[i].value);
                        ImGui::SameLine();
                        std::string btn1 = "Set##" + std::string(regs[i].name);
                        if (ImGui::SmallButton(btn1.c_str())) {
                            editing_reg = regs[i].name;
                            snprintf(reg_edit_value, sizeof(reg_edit_value), "%llX", regs[i].value);
                            ImGui::OpenPopup("EditRegPopup");
                        }
                    } else {
                        ImGui::TextDisabled("N/A");
                    }

                    if (i + 1 < regs.size()) {
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", regs[i+1].name);
                        ImGui::TableNextColumn();
                        if (regs[i+1].valid) {
                            ImGui::TextColored(colors::Info, "0x%016llX", regs[i+1].value);
                            ImGui::SameLine();
                            std::string btn2 = "Set##" + std::string(regs[i+1].name);
                            if (ImGui::SmallButton(btn2.c_str())) {
                                editing_reg = regs[i+1].name;
                                snprintf(reg_edit_value, sizeof(reg_edit_value), "%llX", regs[i+1].value);
                                ImGui::OpenPopup("EditRegPopup");
                            }
                        } else {
                            ImGui::TextDisabled("N/A");
                        }
                    }
                }
                ImGui::EndTable();
            }

            // Edit popup
            if (ImGui::BeginPopup("EditRegPopup")) {
                ImGui::Text("Set %s:", editing_reg.c_str());
                ImGui::SetNextItemWidth(180.0f);
                bool entered = ImGui::InputText("##editval", reg_edit_value, sizeof(reg_edit_value),
                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::SameLine();
                if (ImGui::Button("Apply") || entered) {
                    try {
                        uint64_t new_val = std::stoull(reg_edit_value, nullptr, 16);
                        auto reg = emulation::ParseRegister(editing_reg);
                        if (reg && emulator_->SetRegister(*reg, new_val)) {
                            emu_last_result_ = editing_reg + " = 0x" + std::string(reg_edit_value);
                        } else {
                            emu_last_result_ = "Failed to set register";
                        }
                    } catch (...) {
                        emu_last_result_ = "Invalid hex value";
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // === MEMORY TAB ===
        if (ImGui::BeginTabItem("Memory")) {
            ImGui::BeginChild("MemoryChild", ImVec2(0, 0), false);

            GroupLabel("Memory Mapping");

            // Map module
            ImGui::Text("Map entire module into emulator:");
            ImGui::SetNextItemWidth(250.0f);
            ImGui::InputTextWithHint("##emu_module", "Module name (e.g., game.exe)", emu_map_module_, sizeof(emu_map_module_));
            ImGui::SameLine();
            if (AccentButton("Map Module", ImVec2(120, 0))) {
                if (strlen(emu_map_module_) > 0) {
                    if (emulator_->MapModule(emu_map_module_)) {
                        emu_last_result_ = std::string("Successfully mapped: ") + emu_map_module_;
                    } else {
                        emu_last_result_ = "Failed to map module: " + emulator_->GetLastError();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Map region
            ImGui::Text("Map memory region:");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##emu_addr", "Address (hex)", emu_map_addr_, sizeof(emu_map_addr_));
            ImGui::SameLine();
            ImGui::Text("Size:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputText("##emu_size", emu_map_size_, sizeof(emu_map_size_));
            ImGui::SameLine();
            if (AccentButton("Map Region", ImVec2(120, 0))) {
                if (strlen(emu_map_addr_) > 0 && strlen(emu_map_size_) > 0) {
                    try {
                        uint64_t addr = std::stoull(emu_map_addr_, nullptr, 16);
                        size_t size = std::stoul(emu_map_size_);
                        if (emulator_->MapRegion(addr, size)) {
                            std::stringstream ss;
                            ss << "Mapped 0x" << std::hex << size << " bytes at 0x" << addr;
                            emu_last_result_ = ss.str();
                        } else {
                            emu_last_result_ = "Failed to map region: " + emulator_->GetLastError();
                        }
                    } catch (const std::exception& e) {
                        emu_last_result_ = std::string("Invalid input: ") + e.what();
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Statistics
            ImGui::Text("Memory Statistics:");
            const auto& accessed = emulator_->GetAccessedPages();
            ImGui::BulletText("Accessed pages: %zu", accessed.size());
            ImGui::BulletText("Total accessed memory: %zu KB", accessed.size() * 4);

            ImGui::Spacing();
            ImGui::TextDisabled("Note: With lazy mapping enabled, pages are automatically");
            ImGui::TextDisabled("fetched from the target process when accessed during emulation.");

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // === HELP TAB ===
        if (ImGui::BeginTabItem("Help")) {
            ImGui::BeginChild("HelpChild", ImVec2(0, 0), false);

            ImGui::Text("Unicorn x64 Emulator");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped(
                "This emulator runs x64 code in an isolated environment, reading memory "
                "from the target process via DMA as needed. It's useful for:"
            );
            ImGui::Spacing();
            ImGui::BulletText("Running pointer decryption routines");
            ImGui::BulletText("Analyzing obfuscated code");
            ImGui::BulletText("Understanding complex algorithms");
            ImGui::BulletText("Testing code paths without affecting the target");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Workflow:");
            ImGui::BulletText("1. Select a process and create an emulator");
            ImGui::BulletText("2. Set registers to initial values (Registers tab)");
            ImGui::BulletText("3. Optionally pre-map modules/regions (Memory tab)");
            ImGui::BulletText("4. Run code from start to end address");
            ImGui::BulletText("5. Check result in registers (e.g., RAX for return value)");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Tips:");
            ImGui::BulletText("Lazy mapping is enabled - pages load on-demand");
            ImGui::BulletText("Use 'Reset CPU' to clear registers between runs");
            ImGui::BulletText("Use 'Full Reset' to also clear mapped memory");
            ImGui::BulletText("The emulator has a 5 second timeout by default");

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Result display at bottom
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(colors::Warning, "Last Result:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", emu_last_result_.c_str());

    ImGui::End();
}

} // namespace orpheus::ui
