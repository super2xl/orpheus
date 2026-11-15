# Changelog

All notable changes to Orpheus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- **Stale-session recovery now triggers on empty results.** The 0.2.2 refresh-and-retry only ran when a module or memory-region lookup failed outright, but a stale session returns a successful lookup with zero entries. Empty results now trigger the retry too, and refreshes are rate-limited so processes that really have no modules (such as Memory Compression) don't flush the cache on every call.

## [0.2.3] - 2026-11-15

Analysis accuracy release. Function recovery, function lookups, and snapshot diffs were checked against live hardware and fixed where they gave wrong or incomplete answers.

### Fixed
- **Function recovery kept only part of the exception table (`.pdata`).** Parsing stopped at the first page that was paged out on the target, so on Explorer only 2,731 of 12,411 functions got exact bounds. Unreadable entries are now skipped instead, recovering 11,046. Function caches from older versions rebuild automatically once.
- **False-positive functions inside real ones.** Prologue and call-target guesses that start inside a `.pdata` function are now discarded, so lookups no longer land on a bogus mid-function entry.
- **`get_function_containing`** could match an earlier, size-unknown function across a gap. It now uses the nearest function, or reports that the address is between functions.
- **`find_function_bounds`** returns exact bounds when functions have been recovered. Its fallback now decodes real instructions, so a `0xC3` byte inside another instruction is no longer taken as a `ret`, and heuristic results report "medium" confidence instead of "high".
- **Snapshots could return stale memory.** Two snapshots taken seconds apart occasionally came back identical from MemProcFS's page cache. Snapshots and diffs now read directly from the device.
- **`memory_diff`** could read past the end of a buffer when live memory came back shorter than the snapshot; it now compares the readable part and says so. Totals now cover the whole region even when results are cut off at `max_results`, and `truncated` is set only when more matches existed.

### Added
- Regression tests for all of the above in the scenario suite (50 tests across 5 user journeys).

## [0.2.2] - 2026-11-15

Reliability release, verified against live hardware with a new scenario test suite.

### Added
- **Bridge DMA session tools** (`mcp_bridge.js` 1.4.0) — `dma_status`, `connect_dma`, and `disconnect_dma`. AI clients can now start, check, and reset the DMA session without the UI. `connect_dma` waits for the connection to finish and returns the device name or the real error.
- **Scenario test suite** (`tests/scenarios/`) — drives the MCP server against a live DMA target across the core user journeys (connect, processes → modules → memory, pattern/string scans, async tasks, xrefs), plus an offline check that every UI and bridge endpoint exists on the server. Run with `node tests/scenarios/run.mjs`.

### Fixed
- **Empty module and memory-region lists on long-running sessions.** A stale DMA session could return zero modules and regions for every process with no error. Orpheus now refreshes and retries, and if the map is still unavailable, reports a clear error telling you to reconnect instead of showing an empty list.
- **MemProcFS refresh option had the wrong value** (`0x2001ffff` instead of `0x2001ffff00000000`), so the intended cache refresh was silently rejected.
- **Task tools returned a bare HTTP 500** on a missing or unknown `task_id`, which MCP clients showed as "Invalid JSON response". They now return a JSON error, and a server-wide exception handler guarantees no endpoint returns a body-less 500.
- Bridge network errors no longer show an empty message when the server is unreachable, and the bridge now reports its real version to MCP clients.
- Resolved all compiler warnings in first-party code: the config `port` read is now an explicit `uint16_t` cast (was a silent truncation), the unused `PagesNeeded` helper was removed, and the `WIN32_LEAN_AND_MEAN` redefinition is guarded.

### Removed
- **CS2 subsystem** — retired the CS2 schema dumper, entity tools, and decompiler type-injection. Deleted the dedicated handler/dumper/type-injector sources, removed the 14 `cs2_*` MCP endpoints and their `mcp_bridge.js` schemas, the frontend schema-cache panel, and the dead CS2 radar-map embedding in the build.

### Changed
- Third-party header warnings (Ghidra, Unicorn) are suppressed by treating them as `SYSTEM` includes with `/external:W0`, keeping `/W4` meaningful for first-party code.

## [0.2.1] - 2026-03-22

### Changed
- Release artifacts simplified to the NSIS installer plus `mcp_bridge.js` only — the loose `orpheus_core.dll`, MSI, README and LICENSE are no longer published as separate downloads
- README refreshed for the v0.2.0 architecture with current screenshots

### Removed
- Bundled CS2 radar map resources and superseded screenshots

## [0.2.0] - 2026-03-22

Complete UI rewrite. The ImGui desktop application is gone; Orpheus is now a Tauri v2 +
React 19 shell driving the C++ core as a DLL over FFI in a single process.

### Added
- **Tauri v2 + React 19 frontend** replacing the ImGui UI — 24 feature panels (process list,
  module browser, memory viewer, disassembly, decompiler, CFG viewer, pattern scanner,
  string scanner, RTTI scanner, function recovery, xref finder, emulator, write tracer,
  memory regions, pointer chain, vtable reader, memory writer, memory diff, expression
  evaluator, bookmarks, task manager, cache manager, console, settings)
- **`OrpheusCore`** extraction, decoupling the engine from any UI and enabling headless MCP operation
- **MCP client auto-detection** — Settings discovers and configures Claude Desktop, Cursor and
  other supported clients in one click
- Memory snapshot and diff viewer
- Memory writer with type-aware encoding, module dumper, and expression evaluator
- Privacy and telemetry controls in Settings, with `telemetry_status` and `telemetry_config`
  MCP endpoints
- Command palette, toast notifications, right-click context menus with cross-panel navigation,
  and sidebar category grouping
- GoogleTest framework with unit tests for bookmarks, expression evaluator, memory cache and
  pattern scanner
- Unified build scripts (`build.sh`, `build.bat`)

### Changed
- C++ core now compiles as `orpheus_core.dll` and is loaded via FFI from the Tauri shell,
  replacing the previous separate-backend-process model — one process, no subprocess launch
- Frontend communicates with the core over the same local HTTP API that MCP clients use
- MCP terminology removed from user-facing UI copy

### Removed
- ImGui UI, GLFW and OpenGL dependencies, entirely
- CS2 inspector panel and the "game" panel category

### Fixed
- 16 endpoint path mismatches between frontend hooks and the C++ server
- Field-name mismatches in module, region and string-scanner responses
- Thread safety in the DMA interface, task manager, telemetry and runtime cleanup
- Server lifecycle and port cleanup; DMA connect flow made async so the server no longer hangs
- Move constructor correctness and LRU cache behaviour in the memory cache
- RTTI scan responses now include the full class list; cache RVA conversion corrected
- Settings crash; auto-refresh toggle now persists across tab switches

### Security
- MCP authentication enforced with an auto-generated API key, persisted across restarts
  instead of regenerated each launch
- Health endpoint permitted without auth for connectivity checks only
- CORS headers scoped for Tauri webview access
- Debug file logging gated behind debug builds

## [0.1.9] - 2026-03-01

### Added
- Write tracer for tracking memory writes
- Search history persistence
- DMA health monitoring
- Assorted UI improvements

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

[Unreleased]: https://github.com/super2xl/orpheus/compare/v0.2.3...HEAD
[0.2.3]: https://github.com/super2xl/orpheus/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/super2xl/orpheus/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/super2xl/orpheus/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/super2xl/orpheus/compare/v0.1.9...v0.2.0
[0.1.9]: https://github.com/super2xl/orpheus/compare/v0.1.8...v0.1.9
[0.1.8]: https://github.com/super2xl/orpheus/compare/v0.1.7...v0.1.8
[0.1.7]: https://github.com/super2xl/orpheus/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/super2xl/orpheus/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/super2xl/orpheus/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/super2xl/orpheus/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/super2xl/orpheus/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/super2xl/orpheus/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/super2xl/orpheus/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/super2xl/orpheus/releases/tag/v0.1.0
