#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "dumper/cs2_schema.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>

namespace orpheus::ui {

void Application::RenderCS2Schema() {
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("CS2 Schema Dumper", &panels_.cs2_schema);

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

    // Check if CS2 process
    bool is_cs2 = selected_process_name_.find("cs2") != std::string::npos;
    if (!is_cs2) {
        ImGui::TextColored(colors::Warning,
            "Selected process: %s", selected_process_name_.c_str());
        ImGui::TextColored(colors::Muted,
            "This tool is designed for Counter-Strike 2 (cs2.exe)");
        ImGui::Separator();
    }

    // Initialization status
    if (!cs2_schema_initialized_ || cs2_schema_pid_ != selected_pid_) {
        ImGui::TextWrapped("CS2 Schema Dumper extracts class/field offsets from the game's SchemaSystem.");

        // Find schemasystem.dll
        uint64_t schemasystem_base = 0;
        for (const auto& mod : cached_modules_) {
            if (MatchesFilter(mod.name, "schemasystem.dll")) {
                schemasystem_base = mod.base_address;
                break;
            }
        }

        if (schemasystem_base == 0) {
            ErrorText("schemasystem.dll not found! Make sure CS2 is fully loaded.");
        } else {
            ImGui::Text("schemasystem.dll: %s", FormatAddress(schemasystem_base));

            if (AccentButton("Initialize Schema Dumper", ImVec2(200, 0))) {
                cs2_schema_ = std::make_unique<orpheus::dumper::CS2SchemaDumper>(dma_.get(), selected_pid_);
                if (cs2_schema_->Initialize(schemasystem_base)) {
                    cs2_schema_pid_ = selected_pid_;
                    cs2_schema_initialized_ = true;
                    LOG_INFO("CS2 Schema Dumper initialized");
                } else {
                    LOG_ERROR("Failed to initialize CS2 Schema Dumper: {}",
                        cs2_schema_->GetLastError());
                    cs2_schema_.reset();
                }
            }
        }
    } else {
        // Initialized - show controls
        ImGui::TextColored(colors::Success, "Schema System: %s",
            FormatAddress(cs2_schema_->GetSchemaSystemAddress()));

        const auto& scopes = cs2_schema_->GetScopes();
        ImGui::Text("Type Scopes: %zu", scopes.size());

        ImGui::Separator();

        // Dump controls
        if (cs2_schema_dumping_) {
            ProgressBarWithText(
                (float)cs2_schema_progress_ / std::max(1, cs2_schema_total_),
                "Dumping...");
        } else {
            if (AccentButton("Dump All Schemas", ImVec2(150, 0))) {
                cs2_schema_dumping_ = true;
                cs2_schema_progress_ = 0;
                cs2_schema_total_ = 1;

                auto all_schemas = cs2_schema_->DumpAll([this](int current, int total) {
                    cs2_schema_progress_ = current;
                    cs2_schema_total_ = total;
                });

                // Flatten for display
                cs2_cached_classes_.clear();
                for (const auto& [scope, classes] : all_schemas) {
                    for (const auto& cls : classes) {
                        cs2_cached_classes_.push_back(cls);
                    }
                }

                cs2_schema_dumping_ = false;
                LOG_INFO("Dumped {} classes, {} total fields",
                    cs2_schema_->GetTotalClassCount(),
                    cs2_schema_->GetTotalFieldCount());
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_DOWNLOAD " Export JSON", "Export JSON"), ImVec2(100, 0))) {
                if (cs2_schema_->ExportToJson("cs2_schema.json")) {
                    LOG_INFO("Exported schema to cs2_schema.json");
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_FILE_CODE " Export Header", "Export Header"), ImVec2(100, 0))) {
                if (cs2_schema_->ExportToHeader("cs2_offsets.h")) {
                    LOG_INFO("Exported schema to cs2_offsets.h");
                }
            }
        }

        // Stats
        if (!cs2_cached_classes_.empty()) {
            ImGui::Text("Classes: %zu | Fields: %zu",
                cs2_schema_->GetTotalClassCount(),
                cs2_schema_->GetTotalFieldCount());
        }

        ImGui::Separator();

        // Filters
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##class_filter", "Filter classes...",
            cs2_class_filter_, sizeof(cs2_class_filter_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##field_filter", "Filter fields...",
            cs2_field_filter_, sizeof(cs2_field_filter_));

        // Two-pane view: classes on left, fields on right
        float avail_width = ImGui::GetContentRegionAvail().x;
        float avail_height = ImGui::GetContentRegionAvail().y;

        // Class list (left pane)
        ImGui::BeginChild("ClassList", ImVec2(avail_width * 0.4f, avail_height), true);
        ImGui::Text("Classes");
        ImGui::Separator();

        std::string class_filter_lower = ToLower(cs2_class_filter_);

        ImGuiListClipper clipper;
        std::vector<size_t> filtered_indices;

        for (size_t i = 0; i < cs2_cached_classes_.size(); i++) {
            if (!class_filter_lower.empty()) {
                if (!MatchesFilter(cs2_cached_classes_[i].name, class_filter_lower)) {
                    continue;
                }
            }
            filtered_indices.push_back(i);
        }

        clipper.Begin((int)filtered_indices.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                size_t idx = filtered_indices[row];
                const auto& cls = cs2_cached_classes_[idx];

                bool selected = (cs2_selected_class_ == cls.name);
                if (ImGui::Selectable(cls.name.c_str(), selected)) {
                    cs2_selected_class_ = cls.name;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Module: %s", cls.module.c_str());
                    ImGui::Text("Size: 0x%X (%u bytes)", cls.size, cls.size);
                    if (!cls.base_class.empty()) {
                        ImGui::Text("Base: %s", cls.base_class.c_str());
                    }
                    ImGui::Text("Fields: %zu", cls.fields.size());
                    ImGui::EndTooltip();
                }
            }
        }
        clipper.End();

        ImGui::EndChild();

        ImGui::SameLine();

        // Field list (right pane)
        ImGui::BeginChild("FieldList", ImVec2(0, avail_height), true);

        const orpheus::dumper::SchemaClass* selected_cls = nullptr;
        for (const auto& cls : cs2_cached_classes_) {
            if (cls.name == cs2_selected_class_) {
                selected_cls = &cls;
                break;
            }
        }

        if (selected_cls) {
            ImGui::Text("%s", selected_cls->name.c_str());
            if (!selected_cls->base_class.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(colors::Muted,
                    ": %s", selected_cls->base_class.c_str());
            }
            ImGui::Text("Size: 0x%X | Module: %s",
                selected_cls->size, selected_cls->module.c_str());
            ImGui::Separator();

            std::string field_filter_lower = ToLower(cs2_field_filter_);

            if (ImGui::BeginTable("Fields", 3, layout::kStandardTableFlags)) {

                ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& field : selected_cls->fields) {
                    if (!field_filter_lower.empty()) {
                        if (!MatchesFilter(field.name, field_filter_lower)) {
                            continue;
                        }
                    }

                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextColored(colors::Info, "0x%04X", field.offset);

                    // Copy offset on click
                    if (ImGui::IsItemClicked()) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "0x%X", field.offset);
                        ImGui::SetClipboardText(buf);
                    }
                    HelpTooltip("Click to copy offset");

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", field.name.c_str());

                    // Copy field name on click
                    if (ImGui::IsItemClicked()) {
                        ImGui::SetClipboardText(field.name.c_str());
                    }
                    HelpTooltip("Click to copy name");

                    ImGui::TableNextColumn();
                    ImGui::TextColored(colors::Muted, "%s", field.type_name.c_str());
                }

                ImGui::EndTable();
            }
        } else {
            EmptyState("Select a class to view fields");
        }

        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace orpheus::ui
