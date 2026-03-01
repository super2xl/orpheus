#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/layout_constants.h"
#include "ui/icons.h"
#include <imgui.h>
#include "analysis/disassembler.h"
#include "analysis/signature.h"
#include "utils/logger.h"
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace orpheus::ui {

// ============================================================================
// Disassembly syntax coloring helpers
// ============================================================================

// Syntax color palette (VS Code Dark+ inspired - works across all dark themes)
namespace disasm_colors {
    // Structural elements
    static const ImVec4 kAddress     = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);  // #808080 dim gray
    static const ImVec4 kBytes       = ImVec4(0.33f, 0.33f, 0.33f, 1.0f);  // #555555 very dim

    // Mnemonic colors by category
    static const ImVec4 kDefault     = ImVec4(0.83f, 0.83f, 0.83f, 1.0f);  // #D4D4D4 light gray
    static const ImVec4 kCall        = ImVec4(0.34f, 0.61f, 0.84f, 1.0f);  // #569CD6 blue
    static const ImVec4 kJump        = ImVec4(0.86f, 0.86f, 0.67f, 1.0f);  // #DCDCAA yellow
    static const ImVec4 kCondJump    = ImVec4(0.81f, 0.57f, 0.47f, 1.0f);  // #CE9178 orange-brown
    static const ImVec4 kReturn      = ImVec4(0.82f, 0.41f, 0.41f, 1.0f);  // #D16969 soft red
    static const ImVec4 kPush        = ImVec4(0.61f, 0.86f, 0.99f, 1.0f);  // #9CDCFE light blue
    static const ImVec4 kPop         = ImVec4(0.61f, 0.86f, 0.99f, 1.0f);  // #9CDCFE light blue
    static const ImVec4 kCompare     = ImVec4(0.77f, 0.52f, 0.75f, 1.0f);  // #C586C0 purple
    static const ImVec4 kNop         = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);  // #474747 very dim
    static const ImVec4 kSystem      = ImVec4(0.95f, 0.55f, 0.66f, 1.0f);  // #F38BA8 pink

    // Operand token colors
    static const ImVec4 kRegister    = ImVec4(0.31f, 0.79f, 0.69f, 1.0f);  // #4EC9B0 teal
    static const ImVec4 kNumber      = ImVec4(0.71f, 0.81f, 0.66f, 1.0f);  // #B5CEA8 green
    static const ImVec4 kKeyword     = ImVec4(0.34f, 0.61f, 0.84f, 1.0f);  // #569CD6 blue (ptr, byte, etc)
    static const ImVec4 kBracket     = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);  // gray
    static const ImVec4 kPunct       = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);  // gray
    static const ImVec4 kSeparator   = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);  // function boundary line

    inline ImVec4 MnemonicColor(analysis::InstructionCategory cat) {
        using IC = analysis::InstructionCategory;
        switch (cat) {
            case IC::Call:            return kCall;
            case IC::Jump:            return kJump;
            case IC::ConditionalJump: return kCondJump;
            case IC::Return:          return kReturn;
            case IC::Push:            return kPush;
            case IC::Pop:             return kPop;
            case IC::Compare:         return kCompare;
            case IC::Nop:             return kNop;
            case IC::System:          return kSystem;
            default:                  return kDefault;
        }
    }
}

// Check if an uppercase token is an x86-64 register name
static bool IsX86Register(const char* s, size_t len) {
    if (len < 2 || len > 5) return false;

    static const std::unordered_set<std::string> regs = {
        "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
        "EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP",
        "R8D", "R9D", "R10D", "R11D", "R12D", "R13D", "R14D", "R15D",
        "AX", "BX", "CX", "DX", "SI", "DI", "BP", "SP",
        "R8W", "R9W", "R10W", "R11W", "R12W", "R13W", "R14W", "R15W",
        "AL", "AH", "BL", "BH", "CL", "CH", "DL", "DH",
        "SIL", "DIL", "BPL", "SPL",
        "R8B", "R9B", "R10B", "R11B", "R12B", "R13B", "R14B", "R15B",
        "CS", "DS", "ES", "FS", "GS", "SS",
        "RIP", "EIP", "IP",
        "ST0", "ST1", "ST2", "ST3", "ST4", "ST5", "ST6", "ST7",
    };

    if (len >= 4 && s[1] == 'M' && s[2] == 'M') {
        if (s[0] == 'X' || s[0] == 'Y' || s[0] == 'Z') return true;
    }

    return regs.count(std::string(s, len)) > 0;
}

static bool IsNumber(const char* s, size_t len) {
    if (len == 0) return false;
    if (len >= 3 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return true;
    return s[0] >= '0' && s[0] <= '9';
}

static bool IsSizeKeyword(const char* s, size_t len) {
    if (len < 3 || len > 5) return false;
    static const std::unordered_set<std::string> kw = {
        "BYTE", "WORD", "DWORD", "QWORD", "TBYTE", "XWORD", "YWORD", "ZWORD", "PTR",
        "byte", "word", "dword", "qword", "tbyte", "xword", "yword", "zword", "ptr",
    };
    return kw.count(std::string(s, len)) > 0;
}

// Render operands with syntax coloring (inline, zero-gap)
static void RenderColoredOperands(const char* text) {
    if (!text || !*text) return;

    const char* p = text;
    bool first = true;

    auto EmitText = [&](const char* s, size_t len, const ImVec4& color) {
        if (len == 0) return;
        if (!first) ImGui::SameLine(0, 0);
        first = false;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(s, s + len);
        ImGui::PopStyleColor();
    };

    while (*p) {
        if (*p == ' ') {
            EmitText(p, 1, disasm_colors::kDefault);
            p++;
            continue;
        }

        if (*p == '[' || *p == ']') {
            EmitText(p, 1, disasm_colors::kBracket);
            p++;
            continue;
        }
        if (*p == ',' || *p == '+' || *p == '-' || *p == '*' || *p == ':') {
            EmitText(p, 1, disasm_colors::kPunct);
            p++;
            continue;
        }

        const char* start = p;
        while (*p && *p != ' ' && *p != '[' && *p != ']' &&
               *p != ',' && *p != '+' && *p != '-' && *p != '*' && *p != ':') {
            p++;
        }
        size_t len = p - start;

        ImVec4 color;
        if (IsX86Register(start, len)) {
            color = disasm_colors::kRegister;
        } else if (IsNumber(start, len)) {
            color = disasm_colors::kNumber;
        } else if (IsSizeKeyword(start, len)) {
            color = disasm_colors::kKeyword;
        } else {
            color = disasm_colors::kDefault;
        }
        EmitText(start, len, color);
    }
}

void Application::RenderDisassembly() {
    ImGui::Begin("Disassembly", &panels_.disassembly);

    if (selected_pid_ != 0 && dma_ && dma_->IsConnected() && disassembler_) {
        // Toolbar
        if (icons_loaded_) {
            ImGui::TextColored(colors::Muted, ICON_FA_CODE);
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("##DisasmAddr", disasm_address_input_, sizeof(disasm_address_input_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            disasm_address_ = strtoull(disasm_address_input_, nullptr, 16);
            auto code = dma_->ReadMemory(selected_pid_, disasm_address_, 1024);
            if (!code.empty()) {
                disasm_instructions_ = disassembler_->Disassemble(code, disasm_address_);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_PLAY " Disassemble", "Disassemble"))) {
            disasm_address_ = strtoull(disasm_address_input_, nullptr, 16);
            auto code = dma_->ReadMemory(selected_pid_, disasm_address_, 1024);
            if (!code.empty()) {
                disasm_instructions_ = disassembler_->Disassemble(code, disasm_address_);
            }
        }

        ImGui::Separator();

        if (font_mono_) ImGui::PushFont(font_mono_);

        // Zero item spacing for inline colored text rendering
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));

        const float row_height = ImGui::GetTextLineHeightWithSpacing();

        ImGui::BeginChild("##DisasmView", ImVec2(0, 0), true);

        ImGuiListClipper clipper;
        clipper.Begin((int)disasm_instructions_.size(), row_height);

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const auto& instr = disasm_instructions_[i];

                ImGui::PushID(i);

                // Function boundary separator
                if (i > 0) {
                    const auto& prev = disasm_instructions_[i - 1];
                    bool prev_is_boundary = (prev.category == analysis::InstructionCategory::Nop ||
                                             prev.category == analysis::InstructionCategory::Return);
                    bool curr_is_code = (instr.category != analysis::InstructionCategory::Nop);
                    if (prev_is_boundary && curr_is_code && prev.category == analysis::InstructionCategory::Nop) {
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        float width = ImGui::GetContentRegionAvail().x;
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(pos.x, pos.y - 1),
                            ImVec2(pos.x + width, pos.y - 1),
                            ImGui::ColorConvertFloat4ToU32(disasm_colors::kSeparator), 1.0f);
                    }
                }

                ImGui::BeginGroup();

                // Address column
                ImGui::TextColored(disasm_colors::kAddress, "%016llX", (unsigned long long)instr.address);
                ImGui::SameLine(0, 0);
                ImGui::TextUnformatted("  ");
                ImGui::SameLine(0, 0);

                // Bytes column
                bool is_nop = (instr.category == analysis::InstructionCategory::Nop);
                ImVec4 bytes_color = is_nop ? disasm_colors::kNop : disasm_colors::kBytes;

                char bytes_buf[32];
                int bpos = 0;
                for (size_t j = 0; j < instr.bytes.size() && j < 8; j++) {
                    bpos += snprintf(bytes_buf + bpos, sizeof(bytes_buf) - bpos, "%02X ", instr.bytes[j]);
                }
                while (bpos < 24) bytes_buf[bpos++] = ' ';
                bytes_buf[24] = '\0';

                ImGui::TextColored(bytes_color, "%s", bytes_buf);
                ImGui::SameLine(0, 0);

                // Mnemonic
                ImVec4 mnemonic_color = disasm_colors::MnemonicColor(instr.category);
                char mnemonic_buf[16];
                snprintf(mnemonic_buf, sizeof(mnemonic_buf), "%-8s", instr.mnemonic.c_str());

                ImGui::PushStyleColor(ImGuiCol_Text, mnemonic_color);
                ImGui::TextUnformatted(mnemonic_buf, mnemonic_buf + 8);
                ImGui::PopStyleColor();

                // Operands
                if (!instr.operands.empty()) {
                    ImGui::SameLine(0, 0);
                    if (is_nop) {
                        ImGui::TextColored(disasm_colors::kNop, "%s", instr.operands.c_str());
                    } else {
                        RenderColoredOperands(instr.operands.c_str());
                    }
                }

                // Branch target annotation (placeholder for future use)
                if (instr.branch_target && (instr.is_call || instr.is_jump)) {
                    ImGui::SameLine(0, 0);
                }

                ImGui::EndGroup();

                // Right-click context menu
                if (ImGui::BeginPopupContextItem("##disasm_ctx")) {
                    char addr_buf[32];
                    FormatAddressBuf(addr_buf, sizeof(addr_buf), instr.address);

                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy Address", "Copy Address"))) {
                        ImGui::SetClipboardText(addr_buf);
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_TABLE_CELLS " View in Memory", "View in Memory"))) {
                        memory_address_ = instr.address;
                        snprintf(address_input_, sizeof(address_input_), "%llX", (unsigned long long)instr.address);
                        memory_data_ = dma_->ReadMemory(selected_pid_, instr.address, 256);
                        panels_.memory_viewer = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_FINGERPRINT " Generate Signature", "Generate Signature"))) {
                        signature_address_ = instr.address;

                        auto sig_data = dma_->ReadMemory(selected_pid_, instr.address, 128);
                        if (!sig_data.empty()) {
                            analysis::SignatureGenerator generator;
                            analysis::SignatureOptions options;
                            options.wildcard_rip_relative = true;
                            options.wildcard_calls = true;
                            options.wildcard_jumps = true;
                            options.wildcard_large_immediates = true;
                            options.min_unique_bytes = 8;
                            options.max_length = 64;

                            auto sig = generator.Generate(sig_data, instr.address, options);

                            generated_signature_ = sig.pattern;
                            generated_signature_ida_ = analysis::SignatureGenerator::FormatIDA(sig);
                            generated_signature_ce_ = analysis::SignatureGenerator::FormatCE(sig);
                            generated_signature_mask_ = sig.pattern_mask;
                            generated_signature_length_ = static_cast<int>(sig.length);
                            generated_signature_unique_ = static_cast<int>(sig.unique_bytes);
                            generated_signature_ratio_ = sig.uniqueness_ratio;
                            generated_signature_valid_ = sig.is_valid;

                            show_signature_popup_ = true;
                        }
                    }
                    if (ImGui::MenuItem(ICON_OR_TEXT(icons_loaded_, ICON_FA_MAGNIFYING_GLASS " Find XRefs", "Find XRefs to this address"))) {
                        snprintf(xref_target_input_, sizeof(xref_target_input_), "%llX", (unsigned long long)instr.address);
                        panels_.xref_finder = true;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();  // ItemSpacing
        if (font_mono_) ImGui::PopFont();
    } else {
        EmptyState("No disassembly available", "Select a process and navigate to an address");
    }

    ImGui::End();

    // Signature Generator popup dialog
    if (show_signature_popup_) {
        ImGui::OpenPopup("Signature Generator");
        show_signature_popup_ = false;
    }

    if (ImGui::BeginPopupModal("Signature Generator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Address: %s", FormatAddress(signature_address_));
        ImGui::Separator();

        if (generated_signature_valid_) {
            ImGui::TextColored(colors::Success, "Valid signature");
        } else {
            ImGui::TextColored(colors::Warning, "Warning: May not be unique enough");
        }
        ImGui::Separator();

        ImGui::Text("Length: %d bytes | Unique: %d bytes | Ratio: %.1f%%",
                    generated_signature_length_, generated_signature_unique_,
                    generated_signature_ratio_ * 100.0f);
        ImGui::Separator();

        // IDA Pattern
        ImGui::Text("IDA Pattern:");
        if (font_mono_) ImGui::PushFont(font_mono_);
        ImGui::InputText("##ida_pattern", &generated_signature_ida_[0], generated_signature_ida_.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);
        if (font_mono_) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy##ida", "Copy##ida"))) {
            ImGui::SetClipboardText(generated_signature_ida_.c_str());
        }

        // CE Pattern
        ImGui::Text("CE Pattern:");
        if (font_mono_) ImGui::PushFont(font_mono_);
        ImGui::InputText("##ce_pattern", &generated_signature_ce_[0], generated_signature_ce_.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);
        if (font_mono_) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy##ce", "Copy##ce"))) {
            ImGui::SetClipboardText(generated_signature_ce_.c_str());
        }

        // Mask
        ImGui::Text("Mask:");
        if (font_mono_) ImGui::PushFont(font_mono_);
        ImGui::InputText("##mask", &generated_signature_mask_[0], generated_signature_mask_.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);
        if (font_mono_) ImGui::PopFont();
        ImGui::SameLine();
        if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_COPY " Copy##mask", "Copy##mask"))) {
            ImGui::SetClipboardText(generated_signature_mask_.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace orpheus::ui
