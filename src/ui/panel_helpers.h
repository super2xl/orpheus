#pragma once

#include <imgui.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <optional>

namespace orpheus::ui {

// ============================================================================
// Standard sizes & spacing
// ============================================================================

namespace button_size {
    inline constexpr ImVec2 Small  = {80, 0};
    inline constexpr ImVec2 Normal = {120, 0};
    inline constexpr ImVec2 Large  = {160, 0};
}

namespace form_width {
    inline constexpr float Short  = 150.0f;
    inline constexpr float Normal = 250.0f;
    inline constexpr float Wide   = 400.0f;
    inline constexpr float Full   = -1.0f;
}

// ============================================================================
// Colors
// ============================================================================

// Theme-aware semantic colors — call ApplyThemeColors() when theme changes
namespace colors {
    inline ImVec4 Success    = {0.3f, 0.85f, 0.4f, 1.0f};
    inline ImVec4 Error      = {0.95f, 0.3f, 0.3f, 1.0f};
    inline ImVec4 Warning    = {0.95f, 0.75f, 0.2f, 1.0f};
    inline ImVec4 Info       = {0.4f, 0.7f, 1.0f, 1.0f};
    inline ImVec4 Muted      = {0.5f, 0.5f, 0.5f, 1.0f};
    inline ImVec4 Accent     = {0.45f, 0.55f, 0.95f, 1.0f};
    inline ImVec4 Dangerous  = {1.0f, 0.4f, 0.0f, 1.0f};
    inline ImVec4 GroupLabel = {0.7f, 0.85f, 1.0f, 1.0f};

    // Apply theme-appropriate colors. Call from ApplyTheme().
    // is_light: true for light theme, false for dark themes
    inline void ApplyThemeColors(bool is_light) {
        if (is_light) {
            Success    = {0.15f, 0.6f, 0.2f, 1.0f};
            Error      = {0.8f, 0.15f, 0.15f, 1.0f};
            Warning    = {0.7f, 0.5f, 0.0f, 1.0f};
            Info       = {0.15f, 0.4f, 0.8f, 1.0f};
            Muted      = {0.45f, 0.45f, 0.45f, 1.0f};
            Accent     = {0.25f, 0.35f, 0.8f, 1.0f};
            Dangerous  = {0.85f, 0.3f, 0.0f, 1.0f};
            GroupLabel = {0.2f, 0.35f, 0.65f, 1.0f};
        } else {
            Success    = {0.3f, 0.85f, 0.4f, 1.0f};
            Error      = {0.95f, 0.3f, 0.3f, 1.0f};
            Warning    = {0.95f, 0.75f, 0.2f, 1.0f};
            Info       = {0.4f, 0.7f, 1.0f, 1.0f};
            Muted      = {0.5f, 0.5f, 0.5f, 1.0f};
            Accent     = {0.45f, 0.55f, 0.95f, 1.0f};
            Dangerous  = {1.0f, 0.4f, 0.0f, 1.0f};
            GroupLabel = {0.7f, 0.85f, 1.0f, 1.0f};
        }
    }
}

// ============================================================================
// Text & formatting helpers
// ============================================================================

// Format address to static buffer — NOT thread-safe, use immediately or copy
inline const char* FormatAddress(uint64_t address) {
    static thread_local char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)address);
    return buf;
}

// Format address into user-provided buffer
inline void FormatAddressBuf(char* buf, size_t buf_size, uint64_t address) {
    snprintf(buf, buf_size, "0x%llX", (unsigned long long)address);
}

// Format size as human-readable
inline const char* FormatSize(uint64_t bytes) {
    static thread_local char buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

// Parse hex address string, returns nullopt on failure
inline std::optional<uint64_t> ParseHexAddress(const char* input) {
    if (!input || !*input) return std::nullopt;
    char* end = nullptr;
    // Skip optional "0x" prefix
    const char* p = input;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!*p) return std::nullopt;
    uint64_t val = strtoull(p, &end, 16);
    if (end == p) return std::nullopt; // no digits consumed
    return val;
}

// ============================================================================
// Section & layout helpers
// ============================================================================

// Section header with separator line
inline void SectionHeader(const char* label) {
    ImGui::Spacing();
    ImGui::SeparatorText(label);
}

// Colored group label (for settings categories etc.)
inline void GroupLabel(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, colors::GroupLabel);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Colored status badge text
inline void StatusBadge(const char* text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

// Centered empty state message for panels with no data
inline void EmptyState(const char* message, const char* hint = nullptr) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 text_size = ImGui::CalcTextSize(message);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - text_size.x) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.4f);
    ImGui::TextDisabled("%s", message);
    if (hint) {
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - hint_size.x) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }
}

// Error message in red
inline void ErrorText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, colors::Error);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

// ============================================================================
// Buttons
// ============================================================================

// Styled action button (accent colored)
inline bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.35f, 0.75f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// Danger button (red)
inline bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// Success button (green)
inline bool SuccessButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// ============================================================================
// Input & filter helpers
// ============================================================================

// Filter bar with integrated search input
inline bool FilterBar(const char* id, char* buf, size_t buf_size, float extra_width = 0.0f) {
    float width = extra_width > 0.0f ? -(extra_width) : -1.0f;
    ImGui::SetNextItemWidth(width);
    return ImGui::InputTextWithHint(id, "Filter...", buf, buf_size);
}

// Case-insensitive substring match
inline bool MatchesFilter(const std::string& text, const std::string& filter_lower) {
    if (filter_lower.empty()) return true;
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(filter_lower) != std::string::npos;
}

// Convert string to lowercase (for filter prep)
inline std::string ToLower(const std::string& str) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
inline std::string ToLower(const char* str) { return ToLower(std::string(str)); }

// Build filtered index list from a collection
// getter: returns the string to match against for each item
template<typename T>
std::vector<size_t> BuildFilteredIndices(
    const std::vector<T>& items,
    const char* filter,
    const std::function<std::string(const T&)>& getter)
{
    std::string filter_lower = ToLower(filter);
    std::vector<size_t> indices;
    indices.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
        if (MatchesFilter(getter(items[i]), filter_lower)) {
            indices.push_back(i);
        }
    }
    return indices;
}

// ============================================================================
// Module header (shared by pattern/string/xref scanners)
// ============================================================================

inline void ModuleHeader(const char* module_name, uint64_t module_base, uint32_t module_size) {
    if (module_base != 0) {
        ImGui::TextColored(colors::Info, "%s", module_name);
        ImGui::SameLine();
        ImGui::TextDisabled("@ %s (0x%X bytes)", FormatAddress(module_base), module_size);
    } else {
        ImGui::TextColored(colors::Warning, "No module selected");
        ImGui::SameLine();
        ImGui::TextDisabled("- Select a module in the Modules panel");
    }
    ImGui::Separator();
}

// ============================================================================
// Hex bytes formatting
// ============================================================================

// Format raw bytes as hex string (e.g., "4A 8B 00 FF ...")
inline const char* FormatHexBytes(const uint8_t* data, size_t size, size_t max_bytes = 16) {
    static thread_local char buf[256];
    int pos = 0;
    size_t count = (size < max_bytes) ? size : max_bytes;
    for (size_t i = 0; i < count && pos < 240; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", data[i]);
    }
    if (size > max_bytes && pos < 250) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "...");
    }
    // trim trailing space
    if (pos > 0 && buf[pos - 1] == ' ') pos--;
    buf[pos] = '\0';
    return buf;
}

inline const char* FormatHexBytes(const std::vector<uint8_t>& data, size_t max_bytes = 16) {
    return FormatHexBytes(data.data(), data.size(), max_bytes);
}

// ============================================================================
// Address display & interaction
// ============================================================================

// Hex address display — returns true if clicked (for navigation)
inline bool ClickableAddress(uint64_t address, ImFont* mono_font = nullptr) {
    char buf[32];
    FormatAddressBuf(buf, sizeof(buf), address);

    if (mono_font) ImGui::PushFont(mono_font);
    ImGui::PushStyleColor(ImGuiCol_Text, colors::Info);
    bool clicked = ImGui::SmallButton(buf);
    ImGui::PopStyleColor();
    if (mono_font) ImGui::PopFont();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to navigate to %s", buf);
    }
    return clicked;
}

// Standard address context menu items (Copy, View in Memory, View in Disasm)
// Call between BeginPopupContextItem / EndPopup
// Returns: 0=none, 1=copy, 2=view_memory, 3=view_disasm
inline int AddressContextMenuItems(uint64_t address, bool icons_loaded = false) {
    int result = 0;
    char addr_str[32];
    FormatAddressBuf(addr_str, sizeof(addr_str), address);

    if (ImGui::MenuItem(icons_loaded ? "\xef\x83\x85 Copy Address" : "Copy Address")) {
        ImGui::SetClipboardText(addr_str);
        result = 1;
    }
    ImGui::Separator();
    if (ImGui::MenuItem(icons_loaded ? "\xee\x92\xa1 View in Memory" : "View in Memory")) {
        result = 2;
    }
    if (ImGui::MenuItem(icons_loaded ? "\xef\x84\xa1 View in Disassembly" : "View in Disassembly")) {
        result = 3;
    }
    return result;
}

// ============================================================================
// Tooltip helpers
// ============================================================================

// Show tooltip on hover for previous item
inline void HelpTooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", text);
    }
}

// Help marker: (?) that shows tooltip on hover
inline void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", text);
    }
}

// ============================================================================
// Key-value display
// ============================================================================

inline void KeyValue(const char* key, const char* value) {
    ImGui::TextDisabled("%s:", key);
    ImGui::SameLine();
    ImGui::TextUnformatted(value);
}

inline void KeyValueInt(const char* key, int value) {
    ImGui::TextDisabled("%s:", key);
    ImGui::SameLine();
    ImGui::Text("%d", value);
}

inline void KeyValueHex(const char* key, uint64_t value) {
    ImGui::TextDisabled("%s:", key);
    ImGui::SameLine();
    ImGui::Text("%s", FormatAddress(value));
}

// ============================================================================
// Progress helpers
// ============================================================================

// Progress bar with percentage text overlay
inline void ProgressBarWithText(float fraction, const char* text = nullptr, const ImVec2& size = ImVec2(-1, 0)) {
    if (text) {
        ImGui::ProgressBar(fraction, size, text);
    } else {
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.0f%%", fraction * 100.0f);
        ImGui::ProgressBar(fraction, size, overlay);
    }
}

// ============================================================================
// Modal dialog helpers
// ============================================================================

// Begin a centered modal popup. Returns true if the popup is open.
// Handles OpenPopup + centering + BeginPopupModal boilerplate.
// Caller must call ImGui::EndPopup() when this returns true.
inline bool BeginCenteredModal(const char* name, bool* open,
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize,
    const ImVec2& size = ImVec2(0, 0))
{
    ImGui::OpenPopup(name);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (size.x > 0 && size.y > 0) {
        ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    }
    return ImGui::BeginPopupModal(name, open, flags);
}

// Standard close button at bottom of dialog
inline bool DialogCloseButton(const char* label = "Close") {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    bool clicked = ImGui::Button(label, button_size::Normal);
    return clicked;
}

// Standard OK/Cancel button pair at bottom of dialog
// Returns: 0=none, 1=ok, 2=cancel
inline int DialogOkCancelButtons(const char* ok_label = "OK", const char* cancel_label = "Cancel") {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    int result = 0;
    if (ImGui::Button(ok_label, button_size::Normal)) result = 1;
    ImGui::SameLine();
    if (ImGui::Button(cancel_label, button_size::Normal)) result = 2;
    return result;
}

} // namespace orpheus::ui
