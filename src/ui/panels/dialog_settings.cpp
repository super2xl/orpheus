#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include <imgui.h>
#include "mcp/mcp_server.h"
#include "core/runtime_manager.h"
#include "utils/logger.h"
#include "utils/telemetry.h"
#include <filesystem>
#include <cstdio>

namespace orpheus::ui {

void Application::RenderSettingsDialog() {
    if (BeginCenteredModal("Settings", &show_settings_, ImGuiWindowFlags_NoResize, layout::kDialogLarge)) {
        // Tabbed interface
        if (ImGui::BeginTabBar("SettingsTabs")) {
            // ===== MCP Tab =====
            if (ImGui::BeginTabItem("MCP Server")) {
                ImGui::Spacing();
                ImGui::TextWrapped("Model Context Protocol server provides REST API access for LLMs like Claude to interact with Orpheus.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (!mcp_config_) {
                    mcp_config_ = std::make_unique<mcp::MCPConfig>();
                    // Try to load existing config
                    mcp::MCPServer::LoadConfig(*mcp_config_);
                }

                if (ImGui::Checkbox("Enable MCP Server", &mcp_config_->enabled)) {
                    mcp_config_dirty_ = true;
                }
                ImGui::SameLine();
                ImGui::Button("?");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Starts an HTTP server that allows external tools to interact with Orpheus");
                }

                ImGui::Spacing();

                // Port configuration
                ImGui::Text("Server Port:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                int port = static_cast<int>(mcp_config_->port);
                if (ImGui::InputInt("##port", &port, 0, 0)) {
                    if (port > 0 && port <= 65535) {
                        mcp_config_->port = static_cast<uint16_t>(port);
                        mcp_config_dirty_ = true;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(default: 8765)");

                ImGui::Spacing();

                // Bind address configuration
                ImGui::Text("Bind Address:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                char bind_addr_input[128];
                strncpy(bind_addr_input, mcp_config_->bind_address.c_str(), sizeof(bind_addr_input) - 1);
                bind_addr_input[sizeof(bind_addr_input) - 1] = '\0';
                if (ImGui::InputText("##bind_addr", bind_addr_input, sizeof(bind_addr_input))) {
                    mcp_config_->bind_address = bind_addr_input;
                    mcp_config_dirty_ = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Localhost")) {
                    mcp_config_->bind_address = "127.0.0.1";
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Secure: Only accessible from this machine");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("All Interfaces")) {
                    mcp_config_->bind_address = "0.0.0.0";
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Listen on all network interfaces (allows remote access)");
                }

                ImGui::Spacing();

                // Authentication
                if (ImGui::Checkbox("Require Authentication", &mcp_config_->require_auth)) {
                    mcp_config_dirty_ = true;
                }
                ImGui::SameLine();
                ImGui::Button("?##auth");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Require Bearer token for API access");
                }

                if (mcp_config_->require_auth) {
                    ImGui::Spacing();
                    ImGui::Text("API Key:");
                    ImGui::SameLine();

                    // Show masked API key
                    std::string masked_key = mcp_config_->api_key.empty() ?
                        "<not generated>" :
                        (mcp_config_->api_key.substr(0, 8) + "..." + mcp_config_->api_key.substr(mcp_config_->api_key.length() - 8));
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", masked_key.c_str());

                    ImGui::SameLine();
                    if (ImGui::Button("Generate New Key")) {
                        mcp_config_->api_key = mcp::MCPServer::GenerateApiKey();
                        mcp::MCPServer::SaveConfig(*mcp_config_);
                        LOG_INFO("Generated new MCP API key");
                    }

                    if (!mcp_config_->api_key.empty()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Copy to Clipboard")) {
                            ImGui::SetClipboardText(mcp_config_->api_key.c_str());
                            LOG_INFO("API key copied to clipboard");
                        }
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Feature permissions header
                ImGui::Text("Feature Permissions:");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Control which MCP tool endpoints are enabled.\nGreen = enabled, Red = disabled");
                }

                // Quick toggle buttons - right-align from window edge
                float buttons_width = ImGui::CalcTextSize("Safe Defaults").x + ImGui::CalcTextSize("Enable All").x +
                    ImGui::CalcTextSize("Disable All").x + ImGui::GetStyle().FramePadding.x * 6 +
                    ImGui::GetStyle().ItemSpacing.x * 2 + 12.0f;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttons_width);
                if (ImGui::SmallButton("Safe Defaults")) {
                    mcp_config_->allow_read = true;
                    mcp_config_->allow_write = false;  // Keep write disabled
                    mcp_config_->allow_scan = true;
                    mcp_config_->allow_dump = true;
                    mcp_config_->allow_disasm = true;
                    mcp_config_->allow_emu = true;
                    mcp_config_->allow_rtti = true;
                    mcp_config_->allow_cs2_schema = true;
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enable all safe features (write stays OFF)");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Enable All")) {
                    mcp_config_->allow_read = true;
                    mcp_config_->allow_write = true;
                    mcp_config_->allow_scan = true;
                    mcp_config_->allow_dump = true;
                    mcp_config_->allow_disasm = true;
                    mcp_config_->allow_emu = true;
                    mcp_config_->allow_rtti = true;
                    mcp_config_->allow_cs2_schema = true;
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enable ALL features including write");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Disable All")) {
                    mcp_config_->allow_read = false;
                    mcp_config_->allow_write = false;
                    mcp_config_->allow_scan = false;
                    mcp_config_->allow_dump = false;
                    mcp_config_->allow_disasm = false;
                    mcp_config_->allow_emu = false;
                    mcp_config_->allow_rtti = false;
                    mcp_config_->allow_cs2_schema = false;
                    mcp_config_dirty_ = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Disable all features");
                }
                ImGui::Spacing();

                // Helper lambda for permission row with status indicator
                auto RenderPermissionRow = [&](const char* label, bool* enabled, const char* tooltip, bool dangerous = false) {
                    // Status indicator
                    if (*enabled) {
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[ON]");
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[OFF]");
                    }
                    ImGui::SameLine();

                    if (ImGui::Checkbox(label, enabled)) {
                        mcp_config_dirty_ = true;
                    }

                    if (dangerous) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "!");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("DANGEROUS - Use with caution!");
                        }
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tooltip);
                    }
                    ImGui::Spacing();
                };

                // Memory Operations group
                GroupLabel("Memory Operations");
                ImGui::Indent(layout::kIndentSize);
                RenderPermissionRow("Memory Read", &mcp_config_->allow_read,
                    "read_memory - Read raw bytes from process memory");
                RenderPermissionRow("Memory Write", &mcp_config_->allow_write,
                    "write_memory - Write bytes to process memory", true);
                RenderPermissionRow("Pointer Resolution", &mcp_config_->allow_read,
                    "resolve_pointer - Follow pointer chains (uses read)");
                ImGui::Unindent(layout::kIndentSize);
                ImGui::Spacing();

                // Analysis Tools group
                GroupLabel("Analysis Tools");
                ImGui::Indent(layout::kIndentSize);
                RenderPermissionRow("Pattern/String Scan", &mcp_config_->allow_scan,
                    "scan_pattern, scan_strings, find_xrefs - Memory scanning operations");
                RenderPermissionRow("Disassembly", &mcp_config_->allow_disasm,
                    "disassemble - x64 instruction disassembly");
                RenderPermissionRow("Module Dumping", &mcp_config_->allow_dump,
                    "dump_module - Dump modules to disk");
                ImGui::Unindent(layout::kIndentSize);
                ImGui::Spacing();

                // Advanced Features group
                GroupLabel("Advanced Features");
                ImGui::Indent(layout::kIndentSize);
                RenderPermissionRow("Emulation", &mcp_config_->allow_emu,
                    "emu_* - Unicorn x64 CPU emulation for decryption/analysis");
                RenderPermissionRow("RTTI Analysis", &mcp_config_->allow_rtti,
                    "rtti_* - MSVC RTTI parsing (vtables, class hierarchy, inheritance)");
                RenderPermissionRow("CS2 Schema", &mcp_config_->allow_cs2_schema,
                    "cs2_schema_* - Counter-Strike 2 schema dumping (class offsets, fields)");
                ImGui::Unindent(layout::kIndentSize);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Permission summary
                int enabled_count = 0;
                if (mcp_config_->allow_read) enabled_count++;
                if (mcp_config_->allow_write) enabled_count++;
                if (mcp_config_->allow_scan) enabled_count++;
                if (mcp_config_->allow_dump) enabled_count++;
                if (mcp_config_->allow_disasm) enabled_count++;
                if (mcp_config_->allow_emu) enabled_count++;
                if (mcp_config_->allow_rtti) enabled_count++;
                if (mcp_config_->allow_cs2_schema) enabled_count++;

                ImVec4 summary_color = enabled_count == 8 ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) :
                                       enabled_count == 0 ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) :
                                                           ImVec4(0.9f, 0.8f, 0.3f, 1.0f);
                ImGui::TextColored(summary_color, "Features: %d/8 enabled", enabled_count);
                if (mcp_config_->allow_write) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "(Write enabled!)");
                }

                ImGui::Spacing();

                // Server status
                bool is_running = mcp_server_ && mcp_server_->IsRunning();
                ImGui::Text("Server Status:");
                ImGui::SameLine();
                if (is_running) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "RUNNING");
                    ImGui::SameLine();
                    if (ImGui::Button("Stop Server")) {
                        if (mcp_server_) {
                            mcp_server_->Stop();
                            LOG_INFO("MCP server stopped");
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "STOPPED");
                    ImGui::SameLine();
                    if (ImGui::Button("Start Server")) {
                        if (!mcp_server_) {
                            mcp_server_ = std::make_unique<mcp::MCPServer>(this);
                        }
                        if (mcp_config_->require_auth && mcp_config_->api_key.empty()) {
                            mcp_config_->api_key = mcp::MCPServer::GenerateApiKey();
                            LOG_INFO("Auto-generated MCP API key");
                        }
                        if (mcp_server_->Start(*mcp_config_)) {
                            LOG_INFO("MCP server started on port {}", mcp_config_->port);
                        } else {
                            LOG_ERROR("Failed to start MCP server");
                        }
                    }
                }

                if (is_running) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                        "Listening on all interfaces (0.0.0.0:%d)", mcp_config_->port);
                    ImGui::TextDisabled("Accessible via http://localhost:%d or http://<your-ip>:%d",
                        mcp_config_->port, mcp_config_->port);
                }

                ImGui::EndTabItem();
            }

            // ===== Appearance Tab =====
            if (ImGui::BeginTabItem("Appearance")) {
                ImGui::Spacing();
                ImGui::Text("UI Customization");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Theme selector
                ImGui::Text("Theme:");
                ImGui::SameLine();
                const char* themes[] = { "Dark", "Light", "Nord", "Dracula", "Catppuccin Mocha" };
                int theme_index = static_cast<int>(current_theme_);
                ImGui::SetNextItemWidth(200);
                if (ImGui::Combo("##theme", &theme_index, themes, IM_ARRAYSIZE(themes))) {
                    current_theme_ = static_cast<Theme>(theme_index);
                    ApplyTheme(current_theme_);
                }

                // Theme previews
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Dark: Classic dark theme with grey tones");
                    ImGui::Text("Light: Bright theme for well-lit environments");
                    ImGui::Text("Nord: Stylish dark theme with blue accents");
                    ImGui::Text("Dracula: Dark theme with purple/pink accents");
                    ImGui::Text("Catppuccin Mocha: Warm pastel dark theme");
                    ImGui::EndTooltip();
                }

                ImGui::Spacing();
                ImGui::Spacing();

                // Font size
                ImGui::Text("Font Size:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("##fontsize", &font_size_, 12.0f, 20.0f, "%.0f px")) {
                    theme_changed_ = true;  // Mark that font size changed
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply##fontsize")) {
                    if (theme_changed_) {
                        pending_font_rebuild_ = true;  // Defer to next frame start
                        status_message_ = "Font size updated";
                        status_timer_ = 2.0f;
                        theme_changed_ = false;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Adjust font size and click Apply to update");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Quick theme buttons
                ImGui::Text("Quick Select:");
                ImGui::SameLine();
                if (ImGui::Button("Dark")) {
                    current_theme_ = Theme::Dark;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Light")) {
                    current_theme_ = Theme::Light;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Nord")) {
                    current_theme_ = Theme::Nord;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Dracula")) {
                    current_theme_ = Theme::Dracula;
                    ApplyTheme(current_theme_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Mocha")) {
                    current_theme_ = Theme::CatppuccinMocha;
                    ApplyTheme(current_theme_);
                }

                ImGui::Spacing();
                ImGui::Spacing();

                // Current theme indicator
                const char* theme_name = themes[static_cast<int>(current_theme_)];
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Active Theme: %s", theme_name);

                ImGui::EndTabItem();
            }

            // ===== General Tab =====
            if (ImGui::BeginTabItem("General")) {
                ImGui::Spacing();
                ImGui::Text("General Application Settings");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Auto-refresh settings
                ImGui::Text("Auto-Refresh");
                ImGui::Spacing();

                ImGui::Checkbox("Enable Live Refresh", &auto_refresh_enabled_);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Automatically refresh process and module lists at specified intervals");
                }

                ImGui::Spacing();

                ImGui::BeginDisabled(!auto_refresh_enabled_);

                ImGui::Text("Process List Interval:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("##proc_interval", &process_refresh_interval_, 0.5f, 10.0f, "%.1f sec");

                ImGui::Text("Module List Interval:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("##mod_interval", &module_refresh_interval_, 0.5f, 5.0f, "%.1f sec");

                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::TextDisabled("Lower intervals = more responsive, but higher DMA overhead");

                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Telemetry settings
                ImGui::Text("Telemetry");
                ImGui::Spacing();

                bool telemetry_enabled = Telemetry::Instance().IsEnabled();
                if (ImGui::Checkbox("Send anonymous usage data", &telemetry_enabled)) {
                    Telemetry::Instance().SetEnabled(telemetry_enabled);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Just want to see if this thing is actually useful!\n\n"
                        "Only sends: version, platform, session duration,\n"
                        "and approximate region (via Cloudflare).\n\n"
                        "No process info, memory data, or personal details."
                    );
                }

                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Memory cache settings
                ImGui::Text("Performance");
                ImGui::Spacing();

                if (dma_) {
                    bool cache_enabled = dma_->IsCacheEnabled();
                    if (ImGui::Checkbox("Enable DMA read cache", &cache_enabled)) {
                        dma_->SetCacheEnabled(cache_enabled);
                    }
                    if (ImGui::IsItemHovered()) {
                        auto stats = dma_->GetCacheStats();
                        ImGui::SetTooltip(
                            "Cache recently read memory pages to reduce DMA traffic.\n"
                            "Reduces reads by 50-80%% for repetitive access patterns.\n\n"
                            "Hit rate: %.1f%% (%llu hits, %llu misses)\n"
                            "Pages cached: %zu (%.1f KB)",
                            stats.HitRate() * 100.0,
                            stats.hits, stats.misses,
                            stats.current_pages,
                            stats.current_bytes / 1024.0
                        );
                    }
                } else {
                    ImGui::BeginDisabled();
                    bool dummy = false;
                    ImGui::Checkbox("Enable DMA read cache", &dummy);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Connect to DMA first");
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Bottom buttons
        if (DialogCloseButton()) {
            // Only save MCP config if it was modified
            if (mcp_config_ && mcp_config_dirty_) {
                mcp::MCPServer::SaveConfig(*mcp_config_);
                mcp_config_dirty_ = false;
            }
            show_settings_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
