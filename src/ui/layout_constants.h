#pragma once

#include <imgui.h>

namespace orpheus::ui::layout {

// ============================================================================
// Table column widths
// ============================================================================

inline constexpr float kColumnAddress    = 130.0f;
inline constexpr float kColumnSize       = 100.0f;
inline constexpr float kColumnPID        = 60.0f;
inline constexpr float kColumnArch       = 40.0f;
inline constexpr float kColumnFlags      = 50.0f;
inline constexpr float kColumnMethods    = 60.0f;
inline constexpr float kColumnNameMin    = 200.0f;
inline constexpr float kColumnProtection = 80.0f;

// ============================================================================
// Filter bar
// ============================================================================

// Width reserved for button(s) next to filter input
inline constexpr float kFilterButtonReserve = 90.0f;

// ============================================================================
// Disassembly
// ============================================================================

inline constexpr int   kDisasmBytesDisplayMax = 8;
inline constexpr int   kDisasmMnemonicPad     = 8;  // pad mnemonic to N chars
inline constexpr int   kBytesPerRow           = 16;

// ============================================================================
// Dialog sizes (use as base, scale with DPI if needed)
// ============================================================================

inline constexpr ImVec2 kDialogSmall   = {400, 250};
inline constexpr ImVec2 kDialogMedium  = {620, 380};
inline constexpr ImVec2 kDialogLarge   = {800, 580};

// ============================================================================
// Status bar
// ============================================================================

inline constexpr float kStatusBarHeight       = 24.0f;
inline constexpr float kStatusBarPaddingX     = 8.0f;
inline constexpr float kStatusBarPaddingY     = 3.0f;
inline constexpr float kStatusBarItemGap      = 8.0f;   // gap between items
inline constexpr float kStatusBarSectionGap   = 16.0f;  // gap between sections

// ============================================================================
// General spacing
// ============================================================================

inline constexpr float kIndentSize       = 10.0f;
inline constexpr float kSectionSpacing   = 8.0f;

// Standard table flags for most panels
inline constexpr ImGuiTableFlags kStandardTableFlags =
    ImGuiTableFlags_Borders |
    ImGuiTableFlags_RowBg |
    ImGuiTableFlags_ScrollY |
    ImGuiTableFlags_Resizable |
    ImGuiTableFlags_Reorderable;

inline constexpr ImGuiTableFlags kSortableTableFlags =
    kStandardTableFlags |
    ImGuiTableFlags_Sortable |
    ImGuiTableFlags_SortTristate;

} // namespace orpheus::ui::layout
