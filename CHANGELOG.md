# Changelog

All notable changes to Orpheus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Precompiled headers for faster incremental builds
- Unity builds for faster clean builds (Release only)
- Link-time optimization (LTO) for Release builds
- Telemetry documentation in README
- Telemetry settings toggle in Settings > General
- Telemetry config persistence (enabled by default)
- `.clang-format` for consistent code style
- `CONTRIBUTING.md` with development guidelines
- RequestValidator improvements (max size, usermode address checks)
- SIMD-optimized pattern scanner (SSE2) - 8-16x faster for common patterns
- Build status badge in README
- DMA read cache with page-aligned LRU (50-80% fewer DMA reads when enabled)
- MCP endpoints: `cache_stats`, `cache_config`, `cache_clear`
- Cache toggle in Settings > General with live stats tooltip
- Version number in window title
- `limits.h` - Centralized constants for size limits

### Changed
- Standardized member variable naming convention
- Improved DMA error messages with actionable troubleshooting tips
- Replaced std::cerr with LOG_* macros for consistent logging

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

[Unreleased]: https://github.com/super2xl/orpheus/compare/v0.1.6...HEAD
[0.1.6]: https://github.com/super2xl/orpheus/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/super2xl/orpheus/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/super2xl/orpheus/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/super2xl/orpheus/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/super2xl/orpheus/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/super2xl/orpheus/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/super2xl/orpheus/releases/tag/v0.1.0
