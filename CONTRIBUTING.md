# Contributing to Orpheus

Thanks for your interest in contributing! Here's how to get started.

## Getting Started

### Prerequisites

**Windows:**
- Visual Studio 2022 with C++20 support
- CMake 3.20+
- Git

**Linux:**
- GCC 12+ or Clang 15+
- CMake 3.20+
- OpenGL development packages

### Building

```bash
# Clone with submodules
git clone --recursive https://github.com/super2xl/orpheus.git
cd orpheus

# Build
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Code Style

- Use `.clang-format` for consistent formatting
- Member variables use trailing underscore: `enabled_`, `session_id_`
- Use `LOG_*` macros for logging, not `std::cout`/`std::cerr`
- Prefer `std::string_view` for read-only string parameters
- Use `[[nodiscard]]` for functions with important return values

### Naming Conventions

```cpp
namespace orpheus {

class MyClass {
public:
    void DoSomething();           // PascalCase for public methods
    int GetValue() const;

private:
    void internalHelper();        // camelCase for private methods
    int value_;                   // Trailing underscore for members
    static constexpr int kMaxSize = 100;  // k prefix for constants
};

}  // namespace orpheus
```

## Commit Messages

Use conventional commits format:

```
feat: add new MCP endpoint for memory regions
fix: correct pattern scanning offset calculation
docs: update README with new build instructions
refactor: extract common validation logic
```

## Pull Requests

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run tests if available
5. Submit a PR with clear description of changes

## Project Structure

```
src/
├── core/          # DMA interface, runtime management
├── analysis/      # Disassembler, pattern scanner, RTTI
├── decompiler/    # Ghidra integration
├── emulation/     # Unicorn CPU emulator
├── dumper/        # CS2 schema dumper
├── mcp/           # MCP server and handlers
├── ui/            # ImGui application
└── utils/         # Logging, bookmarks, telemetry
```

## MCP Handlers

When adding new MCP endpoints:

1. Add handler function to appropriate `mcp_handlers_*.cpp` file
2. Register in `mcp_server.cpp`
3. Add input validation for all parameters
4. Use consistent JSON response format
5. Document the endpoint in README

## Questions?

Open an issue or reach out on the project discussions.
