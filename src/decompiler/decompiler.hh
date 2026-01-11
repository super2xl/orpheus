// Ghidra Decompiler Wrapper for Orpheus
// Copyright (C) 2025 Orpheus Project
// GPL-3.0 License

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>

// Include CS2 schema types for type injection API
#include "dumper/cs2_schema.h"

// Forward declaration
namespace orpheus {
class DMALoadImage;
class TypeInjector;
}

// DMA callback type
namespace orpheus {
using DMAReadCallback = std::function<bool(uint64_t addr, size_t size, uint8_t* buffer)>;
}

// Forward declarations from Ghidra
namespace ghidra {
    class Architecture;
    class Funcdata;
    class AddrSpace;
}

namespace orpheus {

/// Result of a decompilation operation
struct DecompileResult {
    bool success = false;
    std::string error;
    std::string c_code;           // Decompiled C code
    std::string function_name;
    uint64_t entry_point = 0;
    std::vector<std::string> warnings;
};

/// Configuration for the decompiler
struct DecompilerConfig {
    std::string sleigh_spec_path;  // Path to SLEIGH processor specs
    std::string processor = "x86"; // Processor family
    int address_size = 64;         // 32 or 64 bit
    bool little_endian = true;
    std::string compiler_spec = "windows";  // Calling convention spec
};

/// \brief High-level wrapper around Ghidra's decompiler
///
/// This class provides a clean interface for Orpheus to use the Ghidra
/// decompiler library. It handles initialization, processor specification
/// loading, and provides methods to decompile functions from DMA memory.
class Decompiler {
public:
    Decompiler();
    ~Decompiler();

    // Non-copyable
    Decompiler(const Decompiler&) = delete;
    Decompiler& operator=(const Decompiler&) = delete;

    /// Initialize the decompiler library
    /// @param config Configuration options
    /// @return true if initialization succeeded
    bool Initialize(const DecompilerConfig& config);

    /// Check if decompiler is ready
    bool IsInitialized() const { return initialized_; }

    /// Shutdown and cleanup
    void Shutdown();

    /// Set the DMA read callback for memory access
    /// @param callback Function to read memory (addr, size, buffer) -> success
    void SetMemoryCallback(DMAReadCallback callback);

    /// Decompile a function at the given address
    /// @param address Entry point of the function
    /// @param function_name Optional name for the function
    /// @param this_type Optional class name for 'this' pointer (e.g., "CCSPlayerController")
    ///                  When specified, sets the first parameter type to enable field name resolution
    /// @param max_instructions Optional limit on flow analysis instructions (0 = default 100000)
    ///                         Increase for large functions, decrease to fail faster on huge ones
    /// @return Decompilation result with C code or error
    DecompileResult DecompileFunction(uint64_t address,
                                       const std::string& function_name = "",
                                       const std::string& this_type = "",
                                       uint32_t max_instructions = 0);

    /// Decompile a range of code (useful for snippets)
    /// @param start Start address
    /// @param end End address
    /// @return Decompilation result
    DecompileResult DecompileRange(uint64_t start, uint64_t end);

    /// Get last error message
    const std::string& GetLastError() const { return last_error_; }

    /// Get list of available processor specs
    std::vector<std::string> GetAvailableProcessors() const;

    // ===== CS2 Schema Type Injection (Level 3 Integration) =====

    /// Inject CS2 schema types into the decompiler's type system
    /// This enables automatic field naming in decompiled output
    /// @param schema_classes Vector of schema class definitions
    /// @return Number of types successfully injected
    int InjectSchemaTypes(const std::vector<orpheus::dumper::SchemaClass>& schema_classes);

    /// Check if types have been injected
    bool HasInjectedTypes() const { return types_injected_; }

    /// Get the number of injected types
    int GetInjectedTypeCount() const { return injected_type_count_; }

    /// Clear all injected types (requires re-initialization)
    void ClearInjectedTypes();

private:
    bool initialized_ = false;
    std::string last_error_;
    DecompilerConfig config_;

    // Ghidra objects (opaque to avoid header pollution)
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // DMA memory callback
    DMAReadCallback memory_callback_;

    // Type injection state
    std::unique_ptr<TypeInjector> type_injector_;
    bool types_injected_ = false;
    int injected_type_count_ = 0;

    // Internal helpers
    bool SetupArchitecture();
    std::string CaptureOutput(ghidra::Funcdata* fd);
};

} // namespace orpheus
