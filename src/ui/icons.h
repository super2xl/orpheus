#pragma once

// FontAwesome 6 Free Solid icon codepoints for ImGui
// Requires fa-solid-900.ttf to be embedded via EmbedResources.cmake
// Place the font file in resources/fonts/fa-solid-900.ttf
//
// Glyph ranges for ImGui font loading:
#define ICON_FA_MIN 0xe000
#define ICON_FA_MAX 0xf8ff

// --- Navigation & Actions ---
#define ICON_FA_MAGNIFYING_GLASS    "\xef\x80\x82"  // U+F002
#define ICON_FA_CHECK               "\xef\x80\x8c"  // U+F00C
#define ICON_FA_XMARK               "\xef\x80\x8d"  // U+F00D
#define ICON_FA_GEAR                "\xef\x80\x93"  // U+F013
#define ICON_FA_DOWNLOAD            "\xef\x80\x99"  // U+F019
#define ICON_FA_ARROW_LEFT          "\xef\x81\xa0"  // U+F060
#define ICON_FA_ARROW_RIGHT         "\xef\x81\xa1"  // U+F061
#define ICON_FA_ROTATE              "\xef\x8b\xb1"  // U+F2F1
#define ICON_FA_COPY                "\xef\x83\x85"  // U+F0C5
#define ICON_FA_FILTER              "\xef\x82\xb0"  // U+F0B0

// --- Status & Indicators ---
#define ICON_FA_CIRCLE              "\xef\x84\x91"  // U+F111
#define ICON_FA_CIRCLE_CHECK        "\xef\x81\x98"  // U+F058
#define ICON_FA_CIRCLE_XMARK        "\xef\x81\x97"  // U+F057
#define ICON_FA_CIRCLE_INFO         "\xef\x81\x9a"  // U+F05A
#define ICON_FA_TRIANGLE_EXCLAMATION "\xef\x81\xb1" // U+F071
#define ICON_FA_PLAY                "\xef\x81\x8b"  // U+F04B
#define ICON_FA_STOP                "\xef\x81\x8d"  // U+F04D

// --- Panels & Features ---
#define ICON_FA_MICROCHIP           "\xef\x8b\x9b"  // U+F2DB - Processes
#define ICON_FA_PUZZLE_PIECE        "\xef\x84\xae"  // U+F12E - Modules
#define ICON_FA_TABLE_CELLS         "\xee\x92\xa1"  // U+E4A1 - Memory
#define ICON_FA_CODE                "\xef\x84\xa1"  // U+F121 - Disassembly
#define ICON_FA_FILE_CODE           "\xef\x87\x89"  // U+F1C9 - Decompiler
#define ICON_FA_TERMINAL            "\xef\x84\xa0"  // U+F120 - Console
#define ICON_FA_BOOKMARK            "\xef\x80\xae"  // U+F02E - Bookmarks
#define ICON_FA_EYE                 "\xef\x81\xae"  // U+F06E - Memory Watcher
#define ICON_FA_SITEMAP             "\xef\x83\xa8"  // U+F0E8 - RTTI
#define ICON_FA_FONT                "\xef\x80\xb1"  // U+F031 - String Scanner
#define ICON_FA_DIAGRAM_PROJECT     "\xef\x95\x82"  // U+F542 - CFG
#define ICON_FA_FINGERPRINT         "\xef\x95\xb7"  // U+F577 - Signatures
#define ICON_FA_LIST_CHECK          "\xef\x82\xae"  // U+F0AE - Tasks
#define ICON_FA_BROOM               "\xef\x94\x9c"  // U+F51A - Cleanup
#define ICON_FA_WAND_MAGIC_SPARKLES "\xee\x8b\x8a"  // U+E2CA - Emulator
#define ICON_FA_LINK                "\xef\x83\x81"  // U+F0C1 - Pointer Chain
#define ICON_FA_LAYER_GROUP         "\xef\x97\xbd"  // U+F5FD - Memory Regions
#define ICON_FA_FILE_EXPORT         "\xef\x95\xae"  // U+F56E - Dump/Export
#define ICON_FA_DATABASE            "\xef\x87\x80"  // U+F1C0 - Cache/Schema

// --- Connectivity & Server ---
#define ICON_FA_PLUG                "\xef\x87\xa6"  // U+F1E6 - Connect DMA
#define ICON_FA_SERVER              "\xef\x88\xb3"  // U+F233 - MCP Server
#define ICON_FA_ROBOT               "\xef\x95\x84"  // U+F544 - MCP/AI

// --- Game-specific ---
#define ICON_FA_CROSSHAIRS          "\xef\x81\x9b"  // U+F05B - Radar
#define ICON_FA_GAUGE_HIGH          "\xef\x98\xa5"  // U+F625 - Dashboard

// Helper: wrap icon with trailing space for use in labels
// Usage: ICON_LABEL(ICON_FA_GEAR, "Settings") -> "[gear] Settings"
#define ICON_LABEL(icon, text) icon " " text

// Helper: show icon if loaded, plain text otherwise
// Usage: ICON_OR_TEXT(icons_loaded_, ICON_FA_GEAR " Settings", "Settings")
#define ICON_OR_TEXT(loaded, icon_text, fallback) ((loaded) ? (icon_text) : (fallback))
