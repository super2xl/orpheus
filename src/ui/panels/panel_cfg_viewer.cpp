#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "analysis/cfg_builder.h"
#include "utils/logger.h"
#include <cstdio>
#include <sstream>

namespace orpheus::ui {

void Application::RenderCFGViewer() {
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("CFG Viewer", &panels_.cfg_viewer);

    if (!dma_ || !dma_->IsConnected()) {
        EmptyState("DMA not connected", "Connect to a DMA device first");
        ImGui::End();
        return;
    }

    if (selected_pid_ == 0) {
        EmptyState("No process selected", "Select a process to build control flow graphs");
        ImGui::End();
        return;
    }

    // Initialize CFG builder if needed
    if (!cfg_builder_) {
        bool is_64bit = true;
        cfg_builder_ = std::make_unique<analysis::CFGBuilder>(
            [this](uint64_t addr, size_t size) {
                return dma_->ReadMemory(selected_pid_, addr, size);
            },
            is_64bit
        );
    }

    // Address input toolbar
    ImGui::Text("Function:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    bool address_entered = ImGui::InputText("##cfg_addr", cfg_address_input_, sizeof(cfg_address_input_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (AccentButton("Build CFG") || address_entered) {
        auto parsed = ParseHexAddress(cfg_address_input_);
        if (parsed) {
            uint64_t addr = *parsed;
            cfg_function_addr_ = addr;
            auto new_cfg = std::make_unique<analysis::ControlFlowGraph>(
                cfg_builder_->BuildCFG(addr)
            );
            if (!new_cfg->nodes.empty()) {
                cfg_builder_->ComputeLayout(*new_cfg);
                cfg_ = std::move(new_cfg);
                cfg_selected_node_ = 0;
                cfg_needs_layout_ = false;
                LOG_INFO("Built CFG for 0x{:X}: {} nodes, {} edges",
                         addr, cfg_->node_count, cfg_->edge_count);
            } else {
                LOG_WARN("Failed to build CFG at 0x{:X}", addr);
            }
        }
    }

    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("##zoom", &cfg_zoom_, 0.25f, 2.0f, "%.2fx");

    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        cfg_scroll_x_ = 0;
        cfg_scroll_y_ = 0;
        cfg_zoom_ = 1.0f;
    }

    // CFG info
    if (cfg_ && !cfg_->nodes.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %u nodes, %u edges%s",
                           cfg_->node_count, cfg_->edge_count,
                           cfg_->has_loops ? ", has loops" : "");
    }

    ImGui::Separator();

    // Graph canvas
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 50) canvas_size.x = 50;
    if (canvas_size.y < 50) canvas_size.y = 50;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                            IM_COL32(30, 30, 35, 255));
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                      IM_COL32(60, 60, 70, 255));

    // Handle mouse input for panning
    ImGui::InvisibleButton("cfg_canvas", canvas_size);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        cfg_scroll_x_ += delta.x;
        cfg_scroll_y_ += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // Handle zoom with scroll wheel
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            float old_zoom = cfg_zoom_;
            cfg_zoom_ += wheel * 0.1f;
            cfg_zoom_ = std::clamp(cfg_zoom_, 0.25f, 2.0f);

            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 mouse_canvas = {mouse_pos.x - canvas_pos.x - cfg_scroll_x_,
                                   mouse_pos.y - canvas_pos.y - cfg_scroll_y_};
            float zoom_factor = cfg_zoom_ / old_zoom;
            cfg_scroll_x_ -= mouse_canvas.x * (zoom_factor - 1.0f);
            cfg_scroll_y_ -= mouse_canvas.y * (zoom_factor - 1.0f);
        }
    }

    draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

    if (cfg_ && !cfg_->nodes.empty()) {
        // Node colors (domain-specific)
        ImU32 col_node_normal = IM_COL32(50, 60, 80, 255);
        ImU32 col_node_entry = IM_COL32(40, 100, 60, 255);
        ImU32 col_node_exit = IM_COL32(100, 50, 50, 255);
        ImU32 col_node_call = IM_COL32(80, 70, 50, 255);
        ImU32 col_node_cond = IM_COL32(70, 50, 90, 255);
        ImU32 col_node_loop = IM_COL32(100, 80, 40, 255);
        ImU32 col_node_selected = IM_COL32(100, 150, 200, 255);
        ImU32 col_node_border = IM_COL32(100, 110, 130, 255);
        ImU32 col_edge_fallthrough = IM_COL32(100, 150, 100, 200);
        ImU32 col_edge_branch = IM_COL32(150, 100, 100, 200);
        ImU32 col_edge_unconditional = IM_COL32(100, 100, 150, 200);
        ImU32 col_edge_backedge = IM_COL32(200, 100, 50, 200);
        ImU32 col_text = IM_COL32(220, 220, 220, 255);
        ImU32 col_text_addr = IM_COL32(150, 200, 255, 255);

        // Draw edges first (under nodes)
        for (const auto& edge : cfg_->edges) {
            auto from_it = cfg_->nodes.find(edge.from);
            auto to_it = cfg_->nodes.find(edge.to);
            if (from_it == cfg_->nodes.end() || to_it == cfg_->nodes.end()) continue;

            const auto& from_node = from_it->second;
            const auto& to_node = to_it->second;

            float from_x = canvas_pos.x + cfg_scroll_x_ + (from_node.x + from_node.width / 2) * cfg_zoom_;
            float from_y = canvas_pos.y + cfg_scroll_y_ + (from_node.y + from_node.height) * cfg_zoom_;
            float to_x = canvas_pos.x + cfg_scroll_x_ + (to_node.x + to_node.width / 2) * cfg_zoom_;
            float to_y = canvas_pos.y + cfg_scroll_y_ + to_node.y * cfg_zoom_;

            ImU32 edge_col = col_edge_fallthrough;
            if (edge.is_back_edge) {
                edge_col = col_edge_backedge;
            } else {
                switch (edge.type) {
                    case analysis::CFGEdge::Type::Branch:
                        edge_col = col_edge_branch;
                        break;
                    case analysis::CFGEdge::Type::Unconditional:
                        edge_col = col_edge_unconditional;
                        break;
                    default:
                        edge_col = col_edge_fallthrough;
                        break;
                }
            }

            if (edge.is_back_edge) {
                float ctrl_x = std::max(from_x, to_x) + 50 * cfg_zoom_;
                draw_list->AddBezierCubic(
                    ImVec2(from_x, from_y),
                    ImVec2(ctrl_x, from_y),
                    ImVec2(ctrl_x, to_y),
                    ImVec2(to_x, to_y),
                    edge_col, 2.0f * cfg_zoom_
                );
            } else {
                float mid_y = (from_y + to_y) / 2;
                draw_list->AddBezierCubic(
                    ImVec2(from_x, from_y),
                    ImVec2(from_x, mid_y),
                    ImVec2(to_x, mid_y),
                    ImVec2(to_x, to_y),
                    edge_col, 1.5f * cfg_zoom_
                );
            }

            // Arrowhead
            float arrow_size = 8 * cfg_zoom_;
            ImVec2 arrow_tip(to_x, to_y);
            ImVec2 arrow_left(to_x - arrow_size * 0.5f, to_y - arrow_size);
            ImVec2 arrow_right(to_x + arrow_size * 0.5f, to_y - arrow_size);
            draw_list->AddTriangleFilled(arrow_tip, arrow_left, arrow_right, edge_col);
        }

        // Draw nodes
        for (const auto& [addr, node] : cfg_->nodes) {
            float x = canvas_pos.x + cfg_scroll_x_ + node.x * cfg_zoom_;
            float y = canvas_pos.y + cfg_scroll_y_ + node.y * cfg_zoom_;
            float w = node.width * cfg_zoom_;
            float h = node.height * cfg_zoom_;

            if (x + w < canvas_pos.x || x > canvas_pos.x + canvas_size.x ||
                y + h < canvas_pos.y || y > canvas_pos.y + canvas_size.y) {
                continue;
            }

            ImU32 node_col = col_node_normal;
            if (addr == cfg_selected_node_) {
                node_col = col_node_selected;
            } else if (node.is_loop_header) {
                node_col = col_node_loop;
            } else {
                switch (node.type) {
                    case analysis::CFGNode::Type::Entry:
                        node_col = col_node_entry;
                        break;
                    case analysis::CFGNode::Type::Exit:
                        node_col = col_node_exit;
                        break;
                    case analysis::CFGNode::Type::Call:
                        node_col = col_node_call;
                        break;
                    case analysis::CFGNode::Type::ConditionalJump:
                        node_col = col_node_cond;
                        break;
                    default:
                        break;
                }
            }

            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), node_col, 4.0f * cfg_zoom_);
            draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), col_node_border, 4.0f * cfg_zoom_);

            // Address header
            char addr_buf[32];
            snprintf(addr_buf, sizeof(addr_buf), "%llX", (unsigned long long)addr);
            float text_scale = cfg_zoom_ * 0.8f;
            if (text_scale > 0.4f) {
                ImVec2 text_pos(x + 5 * cfg_zoom_, y + 3 * cfg_zoom_);
                draw_list->AddText(nullptr, 12 * cfg_zoom_, text_pos, col_text_addr, addr_buf);
            }

            // Instruction count
            if (text_scale > 0.5f) {
                char info_buf[64];
                snprintf(info_buf, sizeof(info_buf), "%zu instr",
                        node.instructions.size());
                ImVec2 info_pos(x + 5 * cfg_zoom_, y + 18 * cfg_zoom_);
                draw_list->AddText(nullptr, 10 * cfg_zoom_, info_pos, col_text, info_buf);
            }

            // Handle click on node
            ImVec2 mouse_pos = ImGui::GetMousePos();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                mouse_pos.x >= x && mouse_pos.x <= x + w &&
                mouse_pos.y >= y && mouse_pos.y <= y + h) {
                cfg_selected_node_ = addr;
            }
        }
    } else {
        // No CFG loaded
        const char* hint = "Enter a function address and click 'Build CFG' to visualize control flow";
        ImVec2 text_size = ImGui::CalcTextSize(hint);
        ImVec2 text_pos(canvas_pos.x + (canvas_size.x - text_size.x) / 2,
                       canvas_pos.y + (canvas_size.y - text_size.y) / 2);
        draw_list->AddText(text_pos, IM_COL32(100, 100, 100, 200), hint);
    }

    draw_list->PopClipRect();

    // Selected node details panel
    if (cfg_ && cfg_selected_node_ != 0) {
        auto it = cfg_->nodes.find(cfg_selected_node_);
        if (it != cfg_->nodes.end()) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::Separator();

            const auto& node = it->second;
            ImGui::Text("Selected: %s - 0x%llX (%u bytes, %zu instructions)",
                       FormatAddress(node.address),
                       (unsigned long long)node.end_address,
                       node.size,
                       node.instructions.size());

            if (!node.instructions.empty()) {
                ImGui::BeginChild("node_instrs", ImVec2(0, 100), true);
                for (size_t i = 0; i < std::min(node.instructions.size(), (size_t)10); i++) {
                    const auto& instr = node.instructions[i];
                    ImGui::TextColored(colors::Info, "%llX:",
                                      (unsigned long long)instr.address);
                    ImGui::SameLine();
                    ImGui::Text("%s", instr.full_text.c_str());
                }
                if (node.instructions.size() > 10) {
                    ImGui::TextDisabled("... %zu more", node.instructions.size() - 10);
                }
                ImGui::EndChild();
            }

            if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_ARROW_RIGHT " Go to Address", "Go to Address"))) {
                NavigateToAddress(node.address);
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect")) {
                cfg_selected_node_ = 0;
            }
        }
    }

    ImGui::End();
}

} // namespace orpheus::ui
