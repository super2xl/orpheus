#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "dumper/cs2_schema.h"
#include "analysis/rtti_parser.h"
#include "analysis/pattern_scanner.h"
#include "utils/logger.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace orpheus::ui {

void Application::RenderCS2EntityInspector() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Entity Inspector", &panels_.cs2_entity_inspector);

    // Check if process selected
    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select cs2.exe from the process list");
        ImGui::End();
        return;
    }

    // Check if CS2 process
    bool is_cs2 = selected_process_name_.find("cs2") != std::string::npos;
    if (!is_cs2) {
        EmptyState("Wrong process selected", "This tool is designed for Counter-Strike 2 (cs2.exe)");
        ImGui::End();
        return;
    }

    // Check if schema is initialized (required for field lookups)
    if (!cs2_schema_initialized_ || cs2_schema_pid_ != selected_pid_) {
        ImGui::TextColored(colors::Warning,
            "Schema not initialized. Open CS2 Schema Dumper first and dump schemas.");
        if (AccentButton("Open Schema Dumper")) {
            panels_.cs2_schema = true;
        }
        ImGui::End();
        return;
    }

    // Initialize entity system if needed
    if (!cs2_entity_initialized_) {
        ImGui::TextWrapped("Entity Inspector uses RTTI + Schema to identify and inspect live entities.");
        ImGui::Spacing();

        if (AccentButton("Initialize Entity System", ImVec2(200, 0))) {
            // Find client.dll
            for (const auto& mod : cached_modules_) {
                if (MatchesFilter(mod.name, "client.dll")) {
                    cs2_client_base_ = mod.base_address;
                    cs2_client_size_ = mod.size;
                    break;
                }
            }

            if (cs2_client_base_ == 0) {
                LOG_ERROR("client.dll not found!");
            } else {
                // Read a portion of client.dll for pattern scanning
                size_t scan_size = std::min(static_cast<size_t>(cs2_client_size_), static_cast<size_t>(20 * 1024 * 1024));
                auto client_data = dma_->ReadMemory(selected_pid_, cs2_client_base_, scan_size);

                if (!client_data.empty()) {
                    // Pattern scan for CGameEntitySystem
                    auto entity_pattern = analysis::PatternScanner::Compile(
                        "48 8B 0D ?? ?? ?? ?? 8B D3 E8 ?? ?? ?? ?? 48 8B F0", "EntitySystem");

                    if (entity_pattern) {
                        auto entity_results = analysis::PatternScanner::Scan(
                            client_data, *entity_pattern, cs2_client_base_, 1);

                        if (!entity_results.empty()) {
                            uint64_t instr_addr = entity_results[0];
                            auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                            if (offset_data.size() >= 4) {
                                int32_t rip_offset;
                                std::memcpy(&rip_offset, offset_data.data(), 4);
                                uint64_t ptr_addr = instr_addr + 7 + rip_offset;

                                auto ptr_data = dma_->ReadMemory(selected_pid_, ptr_addr, 8);
                                if (ptr_data.size() >= 8) {
                                    std::memcpy(&cs2_entity_system_, ptr_data.data(), 8);
                                    LOG_INFO("Found CGameEntitySystem: 0x{:X}", cs2_entity_system_);
                                }
                            }
                        }
                    }

                    // Pattern scan for LocalPlayerController array
                    auto lpc_pattern = analysis::PatternScanner::Compile(
                        "48 8D 0D ?? ?? ?? ?? 48 8B 04 C1", "LocalPlayerArray");

                    if (lpc_pattern) {
                        auto lpc_results = analysis::PatternScanner::Scan(
                            client_data, *lpc_pattern, cs2_client_base_, 1);

                        if (!lpc_results.empty()) {
                            uint64_t instr_addr = lpc_results[0];
                            auto offset_data = dma_->ReadMemory(selected_pid_, instr_addr + 3, 4);
                            if (offset_data.size() >= 4) {
                                int32_t rip_offset;
                                std::memcpy(&rip_offset, offset_data.data(), 4);
                                cs2_local_player_array_ = instr_addr + 7 + rip_offset;
                                LOG_INFO("Found LocalPlayerController array: 0x{:X}", cs2_local_player_array_);
                            }
                        }
                    }
                }

                if (cs2_entity_system_ != 0 && cs2_local_player_array_ != 0) {
                    cs2_entity_initialized_ = true;
                    LOG_INFO("CS2 Entity System initialized successfully");
                } else {
                    LOG_ERROR("Failed to find entity system patterns");
                }
            }
        }
        ImGui::End();
        return;
    }

    // Top section: Entity system info
    ImGui::TextColored(colors::Success, "Entity System: %s", FormatAddress(cs2_entity_system_));
    ImGui::SameLine(300);
    ImGui::TextColored(colors::Muted, "Client: %s", FormatAddress(cs2_client_base_));

    ImGui::Separator();

    // Local Player Section
    if (ImGui::CollapsingHeader("Local Player", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto lpc_data = dma_->ReadMemory(selected_pid_, cs2_local_player_array_, 8);
        uint64_t local_controller = 0;
        if (lpc_data.size() >= 8) {
            std::memcpy(&local_controller, lpc_data.data(), 8);
        }

        if (local_controller != 0) {
            ImGui::Text("Controller: %s", FormatAddress(local_controller));

            // Read key fields using schema offsets
            if (cs2_schema_) {
                uint32_t pawn_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_hPlayerPawn");
                uint32_t health_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_iPawnHealth");
                uint32_t armor_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_iPawnArmor");
                uint32_t alive_offset = cs2_schema_->GetOffset("CCSPlayerController", "m_bPawnIsAlive");

                if (pawn_offset > 0) {
                    auto pawn_data = dma_->ReadMemory(selected_pid_, local_controller + pawn_offset, 4);
                    if (pawn_data.size() >= 4) {
                        uint32_t pawn_handle;
                        std::memcpy(&pawn_handle, pawn_data.data(), 4);
                        int pawn_index = pawn_handle & 0x7FFF;
                        ImGui::Text("Pawn Handle: 0x%X (index %d)", pawn_handle, pawn_index);
                    }
                }

                if (health_offset > 0) {
                    auto health_data = dma_->ReadMemory(selected_pid_, local_controller + health_offset, 4);
                    if (health_data.size() >= 4) {
                        int32_t health;
                        std::memcpy(&health, health_data.data(), 4);
                        ImVec4 health_color = health > 50 ? colors::Success :
                                              health > 25 ? colors::Warning : colors::Error;
                        ImGui::TextColored(health_color, "Health: %d", health);
                    }
                }

                if (armor_offset > 0) {
                    auto armor_data = dma_->ReadMemory(selected_pid_, local_controller + armor_offset, 4);
                    if (armor_data.size() >= 4) {
                        int32_t armor;
                        std::memcpy(&armor, armor_data.data(), 4);
                        ImGui::SameLine();
                        ImGui::Text("Armor: %d", armor);
                    }
                }

                if (alive_offset > 0) {
                    auto alive_data = dma_->ReadMemory(selected_pid_, local_controller + alive_offset, 1);
                    if (alive_data.size() >= 1) {
                        bool alive = alive_data[0] != 0;
                        ImGui::SameLine();
                        if (alive) {
                            ImGui::TextColored(colors::Success, "[ALIVE]");
                        } else {
                            ImGui::TextColored(colors::Error, "[DEAD]");
                        }
                    }
                }
            }

            // Button to inspect controller
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS " Inspect Controller", "Inspect Controller"))) {
                cs2_selected_entity_ = local_controller;
                cs2_selected_entity_class_ = "CCSPlayerController";
                cs2_field_cache_.clear();
            }
        } else {
            ImGui::TextColored(colors::Muted, "Not in game (no local player)");
        }
    }

    // Player List Section
    if (ImGui::CollapsingHeader("Player List")) {
        constexpr uint32_t OFFSET_PLAYER_NAME = 0x6F8;
        constexpr uint32_t OFFSET_TEAM_NUM = 0x3F3;
        constexpr uint32_t OFFSET_PAWN_HANDLE = 0x90C;
        constexpr uint32_t OFFSET_PAWN_IS_ALIVE = 0x914;
        constexpr uint32_t OFFSET_PAWN_HEALTH = 0x918;
        constexpr uint32_t OFFSET_CONNECTED = 0x6F4;
        constexpr uint32_t OFFSET_STEAM_ID = 0x780;

        auto chunk0_ptr_data = dma_->ReadMemory(selected_pid_, cs2_entity_system_ + 0x10, 8);
        uint64_t chunk0_ptr = 0;
        if (chunk0_ptr_data.size() >= 8) {
            std::memcpy(&chunk0_ptr, chunk0_ptr_data.data(), 8);
        }

        if (chunk0_ptr != 0) {
            uint64_t chunk0_base = chunk0_ptr & ~0xFULL;

            if (ImGui::BeginTable("##PlayerTable", 6, layout::kStandardTableFlags, ImVec2(0, 200))) {

                ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 30);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("Bot", ImGuiTableColumnFlags_WidthFixed, 30);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableHeadersRow();

                for (int idx = 1; idx <= 64; idx++) {
                    uint64_t entry_addr = chunk0_base + 0x08 + idx * 0x70;
                    auto ctrl_data = dma_->ReadMemory(selected_pid_, entry_addr, 8);
                    if (ctrl_data.size() < 8) continue;

                    uint64_t controller;
                    std::memcpy(&controller, ctrl_data.data(), 8);
                    if (controller == 0 || controller < 0x10000000000ULL) continue;

                    auto conn_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_CONNECTED, 4);
                    if (conn_data.size() < 4) continue;
                    uint32_t connected;
                    std::memcpy(&connected, conn_data.data(), 4);
                    if (connected > 2) continue;

                    auto name_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PLAYER_NAME, 64);
                    if (name_data.empty()) continue;
                    std::string name(reinterpret_cast<char*>(name_data.data()));
                    if (name.empty()) continue;

                    auto steam_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_STEAM_ID, 8);
                    uint64_t steam_id = 0;
                    if (steam_data.size() >= 8) std::memcpy(&steam_id, steam_data.data(), 8);
                    bool is_bot = (steam_id == 0);

                    auto team_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_TEAM_NUM, 1);
                    uint8_t team = team_data.size() >= 1 ? team_data[0] : 0;

                    auto alive_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_IS_ALIVE, 1);
                    bool is_alive = alive_data.size() >= 1 && alive_data[0] != 0;

                    auto health_data = dma_->ReadMemory(selected_pid_, controller + OFFSET_PAWN_HEALTH, 4);
                    uint32_t health = 0;
                    if (health_data.size() >= 4) std::memcpy(&health, health_data.data(), 4);

                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::Text("%d", idx);

                    ImGui::TableNextColumn();
                    if (!is_alive) {
                        ImGui::TextColored(colors::Muted, "%s", name.c_str());
                    } else {
                        ImGui::Text("%s", name.c_str());
                    }

                    ImGui::TableNextColumn();
                    if (team == 2) {
                        ImGui::TextColored(colors::Warning, "T");
                    } else if (team == 3) {
                        ImGui::TextColored(colors::Info, "CT");
                    } else {
                        ImGui::TextColored(colors::Muted, "?");
                    }

                    ImGui::TableNextColumn();
                    if (is_alive) {
                        ImVec4 hp_color = health > 50 ? colors::Success :
                                          health > 25 ? colors::Warning : colors::Error;
                        ImGui::TextColored(hp_color, "%d", health);
                    } else {
                        ImGui::TextColored(colors::Muted, "-");
                    }

                    ImGui::TableNextColumn();
                    if (is_bot) {
                        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.9f, 1.0f), "Y");
                    }

                    ImGui::TableNextColumn();
                    ImGui::PushID(idx);
                    if (ImGui::SmallButton(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS, "Inspect"))) {
                        cs2_selected_entity_ = controller;
                        cs2_selected_entity_class_ = "CCSPlayerController";
                        cs2_field_cache_.clear();
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        } else {
            ImGui::TextColored(colors::Muted, "Entity system not ready");
        }
    }

    ImGui::Separator();

    // Entity inspector
    ImGui::BeginChild("##EntityInspector", ImVec2(0, 0), true);

    if (cs2_selected_entity_ != 0) {
        ImGui::Text("Inspecting: %s", FormatAddress(cs2_selected_entity_));
        ImGui::TextColored(colors::Info, "Class: %s",
            cs2_selected_entity_class_.empty() ? "Unknown" : cs2_selected_entity_class_.c_str());

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
        ImGui::Checkbox("Auto-refresh", &cs2_entity_auto_refresh_);

        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ROTATE " Refresh Fields", "Refresh Fields")) ||
            (cs2_entity_auto_refresh_ && cs2_field_cache_.empty())) {
            cs2_field_cache_.clear();

            // Get fields from schema
            if (cs2_schema_ && !cs2_selected_entity_class_.empty()) {
                auto cls = cs2_schema_->FindClass(cs2_selected_entity_class_);
                if (cls) {
                    for (const auto& field : cls->fields) {
                        FieldCacheEntry entry;
                        entry.name = field.name;
                        entry.type = field.type_name;
                        entry.offset = field.offset;

                        // Read and format value based on type
                        size_t read_size = 8;
                        if (field.type_name.find("bool") != std::string::npos) read_size = 1;
                        else if (field.type_name.find("int8") != std::string::npos) read_size = 1;
                        else if (field.type_name.find("int16") != std::string::npos ||
                                 field.type_name.find("uint16") != std::string::npos) read_size = 2;
                        else if (field.type_name.find("int32") != std::string::npos ||
                                 field.type_name.find("uint32") != std::string::npos ||
                                 field.type_name.find("float") != std::string::npos) read_size = 4;

                        auto value_data = dma_->ReadMemory(selected_pid_,
                            cs2_selected_entity_ + field.offset, read_size);

                        if (!value_data.empty()) {
                            std::stringstream ss;
                            if (field.type_name.find("bool") != std::string::npos) {
                                ss << (value_data[0] ? "true" : "false");
                            } else if (field.type_name.find("float") != std::string::npos) {
                                float val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << std::fixed << std::setprecision(2) << val;
                            } else if (field.type_name.find("int32") != std::string::npos) {
                                int32_t val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << val;
                            } else if (field.type_name.find("uint32") != std::string::npos) {
                                uint32_t val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << val;
                            } else if (field.type_name.find("Handle") != std::string::npos) {
                                uint32_t val;
                                std::memcpy(&val, value_data.data(), 4);
                                ss << "0x" << std::hex << val << " (idx " << std::dec << (val & 0x7FFF) << ")";
                            } else if (read_size == 8) {
                                uint64_t val;
                                std::memcpy(&val, value_data.data(), 8);
                                ss << "0x" << std::hex << val;
                            } else {
                                for (size_t i = 0; i < value_data.size() && i < 8; i++) {
                                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)value_data[i];
                                }
                            }
                            entry.value = ss.str();
                        } else {
                            entry.value = "???";
                        }

                        cs2_field_cache_.push_back(entry);
                    }
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " Go to Memory", "Go to Memory"))) {
            NavigateToAddress(cs2_selected_entity_);
        }

        ImGui::Separator();

        // Field filter
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##field_filter", "Filter fields...",
            cs2_entity_filter_, sizeof(cs2_entity_filter_));

        // Field table
        if (ImGui::BeginTable("##FieldTable", 4, layout::kStandardTableFlags)) {

            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableHeadersRow();

            std::string filter_lower = ToLower(cs2_entity_filter_);

            for (const auto& field : cs2_field_cache_) {
                if (!filter_lower.empty()) {
                    if (!MatchesFilter(field.name, filter_lower)) continue;
                }

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("0x%X", field.offset);

                ImGui::TableNextColumn();
                ImGui::Text("%s", field.name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(colors::Muted, "%s", field.type.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(colors::Success, "%s", field.value.c_str());
            }

            ImGui::EndTable();
        }
    } else {
        EmptyState("No entity selected", "Click 'Inspect Controller' above or enter an address below");

        static char addr_buf[32] = {};
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputTextWithHint("##entity_addr", "0x...", addr_buf, sizeof(addr_buf),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
            auto addr_opt = ParseHexAddress(addr_buf);
            if (addr_opt) {
                cs2_selected_entity_ = *addr_opt;
                cs2_selected_entity_class_.clear();
                cs2_field_cache_.clear();

                // Try to identify class via RTTI
                auto vtable_data = dma_->ReadMemory(selected_pid_, cs2_selected_entity_, 8);
                if (vtable_data.size() >= 8) {
                    uint64_t vtable;
                    std::memcpy(&vtable, vtable_data.data(), 8);

                    if (vtable >= cs2_client_base_ && vtable < cs2_client_base_ + cs2_client_size_) {
                        auto read_func = [this](uint64_t addr, size_t size) {
                            return dma_->ReadMemory(selected_pid_, addr, size);
                        };
                        analysis::RTTIParser rtti(read_func, cs2_client_base_);
                        auto info = rtti.ParseVTable(vtable);
                        if (info && !info->demangled_name.empty()) {
                            std::string name = info->demangled_name;
                            if (name.substr(0, 6) == "class ") {
                                name = name.substr(6);
                            }
                            cs2_selected_entity_class_ = name;
                        }
                    }
                }
            } else {
                LOG_ERROR("Invalid address format");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Inspect")) {
            // Trigger the InputText callback
        }
    }

    ImGui::EndChild();

    // Auto-refresh logic
    if (cs2_entity_auto_refresh_ && cs2_selected_entity_ != 0) {
        cs2_entity_refresh_timer_ += ImGui::GetIO().DeltaTime;
        if (cs2_entity_refresh_timer_ > 0.5f) {
            cs2_entity_refresh_timer_ = 0;
            cs2_field_cache_.clear();
        }
    }

    ImGui::End();
}

} // namespace orpheus::ui
