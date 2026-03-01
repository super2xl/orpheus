# Changelog

All notable changes to Orpheus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.8] - 2026-03-01

### Added
- **FontAwesome icon integration**: Icons throughout menus, toolbars, context menus, and status bar with graceful fallback when font not available
- **Shared UI helper system**: `panel_helpers.h` with reusable components (EmptyState, AccentButton, DangerButton, HelpTooltip, FormatAddress, ProgressBarWithText, etc.)
- **Layout constants**: Centralized table flags, column widths, and spacing values in `layout_constants.h`
- **Theme-aware semantic colors**: `colors::` namespace (Success, Error, Warning, Info, Muted, Accent) that adapts to light/dark themes
- **Async string scanner**: Chunked 2MB reads with progress bar, cancel button, and real-time string filter
- **Disassembly syntax coloring**: VS Code Dark+ palette with 10 mnemonic categories and tokenized operand rendering

### Changed
- **Extracted 31 panel/dialog files** from `application.cpp` monolith (~6000 lines) into individual files under `src/ui/panels/`
- **Consolidated icon fallback pattern**: All `icons_loaded_` ternary patterns replaced with `ICON_OR_TEXT` macro
- **Empty states for all panels**: Consistent "no data" messaging with title + subtitle pattern

### Fixed
- **C4244 compiler warnings**: All bare `::tolower` usages across the codebase replaced with safe lambda pattern (emulator, MCP handlers, expression evaluator, UI panels)

## [0.1.7] - 2026-02-01

### Added
- SIMD-optimized pattern scanner (SSE2) - 8-16x faster for common patterns
- DMA read cache with page-aligned LRU (50-80% fewer DMA reads when enabled)
- MCP endpoints: `cache_stats`, `cache_config`, `cache_clear`
- Cache toggle in Settings > General with live stats tooltip
- Telemetry toggle in Settings > General with config persistence
- Version number in window title
- `limits.h` - centralized constants for size limits
- RequestValidator improvements (max size, usermode address checks)
- `.clang-format` for consistent code style
- `CONTRIBUTING.md` with development guidelines

### Changed
- Build system: precompiled headers, unity builds, LTO for Release

## [0.1.6] - 2026-02-01

### Added
- Telemetry via Cloudflare Worker relay
  - Startup/shutdown pings with version and platform info
  - Session duration tracking
  - Geo-location handled server-side (country level only)
- Automatic version extraction from git tags

### Changed
- Version now automatically syncs with git tags (no more hardcoded versions)

## [0.1.5] - 2026-01-31

### Fixed
- CS2 schema dumper for January 2025 game patch
- Schema type resolution improvements

## [0.1.4] - 2026-01-30

### Added
- Bind address configuration UI in MCP settings
- Security fixes and performance improvements
- Agent feedback improvements:
  - `decompile` max_instructions parameter
  - `scan_strings` contains filter
  - `read_vtable` tool
  - `resolve_pointer` visualization
  - `find_function_bounds` tool
- `get_health` and `get_version` MCP tools
- Signature generation tool
- Memory diff engine for value scanning

### Fixed
- Linux build - use C++ boolean literals
- Missing VERSION constants

## [0.1.3] - 2026-01-28

### Added
- Version management with git hash embedding
- Enhanced decompiler output with module+offset context
- CS2 radar and dashboard panels
- Embedded radar map resources

### Fixed
- Expression evaluator to parse bare numbers as hex
- GL_CLAMP_TO_EDGE for Windows OpenGL 1.1

## [0.1.2] - 2026-01-26

### Added
- Function recovery (prologue detection, call targets, pdata, RTTI)
- Control flow graph analysis with loop detection
- Async task system with progress reporting
- MCPinstaller standalone tool

## [0.1.1] - 2026-01-24

### Added
- Ghidra decompiler integration with CS2 schema type injection
- RTTI scanning and caching
- CPU emulation via Unicorn Engine
- 40+ MCP API endpoints

## [0.1.0] - 2026-01-20

### Added
- Initial release
- DMA memory access via PCILeech/MemProcFS
- Pattern and string scanning
- x86-64 disassembler
- ImGui-based GUI
- MCP server for AI integration

[Unreleased]: https://github.com/super2xl/orpheus/compare/v0.1.7...HEAD
[0.1.7]: https://github.com/super2xl/orpheus/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/super2xl/orpheus/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/super2xl/orpheus/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/super2xl/orpheus/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/super2xl/orpheus/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/super2xl/orpheus/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/super2xl/orpheus/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/super2xl/orpheus/releases/tag/v0.1.0
