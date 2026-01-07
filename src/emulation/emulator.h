#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>
#include <memory>

namespace orpheus {

class DMAInterface;

namespace emulation {

/**
 * Register identifiers for x64
 */
enum class Reg {
    RAX, RBX, RCX, RDX,
    RSI, RDI, RBP, RSP,
    R8, R9, R10, R11,
    R12, R13, R14, R15,
    RIP, RFLAGS,
    XMM0, XMM1, XMM2, XMM3,
    XMM4, XMM5, XMM6, XMM7,
    XMM8, XMM9, XMM10, XMM11,
    XMM12, XMM13, XMM14, XMM15
};

/**
 * XMM register value (128-bit)
 */
struct XMMValue {
    uint64_t lo;
    uint64_t hi;
};

/**
 * Emulation result
 */
struct EmulationResult {
    bool success = false;
    std::string error;
    uint64_t instructions_executed = 0;
    uint64_t final_rip = 0;

    // Register state after emulation
    std::unordered_map<std::string, uint64_t> registers;
    std::unordered_map<std::string, XMMValue> xmm_registers;
};

/**
 * Emulator configuration
 */
struct EmulatorConfig {
    uint64_t stack_base = 0x80000000ULL;
    uint64_t stack_size = 0x200000ULL;  // 2MB
    uint64_t max_instructions = 100000;
    uint64_t timeout_us = 5000000;  // 5 seconds
    bool lazy_mapping = true;  // Map pages on-demand from DMA
};

/**
 * Emulator - Unicorn-based x64 CPU emulator with DMA integration
 *
 * Allows running game code in an isolated environment, reading memory
 * from the target process via DMA as needed.
 *
 * Primary use case: Running pointer decryption stubs to get real addresses.
 */
class Emulator {
public:
    Emulator();
    ~Emulator();

    // Non-copyable
    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;

    /**
     * Initialize emulator for a specific process
     * @param dma DMA interface for memory access
     * @param pid Target process ID
     * @param config Emulator configuration
     */
    bool Initialize(DMAInterface* dma, uint32_t pid, const EmulatorConfig& config = {});

    /**
     * Check if emulator is initialized
     */
    bool IsInitialized() const { return uc_ != nullptr; }

    /**
     * Map a memory region from the target process
     * @param address Base address to map
     * @param size Size in bytes (will be page-aligned)
     */
    bool MapRegion(uint64_t address, size_t size);

    /**
     * Map an entire module from the target process
     * @param module_name Name of the module (e.g., "game.exe")
     */
    bool MapModule(const std::string& module_name);

    /**
     * Set a general-purpose register value
     */
    bool SetRegister(Reg reg, uint64_t value);

    /**
     * Get a general-purpose register value
     */
    std::optional<uint64_t> GetRegister(Reg reg);

    /**
     * Set an XMM register value
     */
    bool SetXMM(int index, const XMMValue& value);

    /**
     * Get an XMM register value
     */
    std::optional<XMMValue> GetXMM(int index);

    /**
     * Set multiple registers at once
     */
    bool SetRegisters(const std::unordered_map<std::string, uint64_t>& regs);

    /**
     * Run emulation from start_address until end_address is reached
     * @param start_address Address to begin execution
     * @param end_address Address to stop execution (exclusive)
     */
    EmulationResult Run(uint64_t start_address, uint64_t end_address);

    /**
     * Run emulation for a specific number of instructions
     */
    EmulationResult RunInstructions(uint64_t start_address, size_t count);

    /**
     * Reset CPU state (registers) but keep memory mappings
     */
    void ResetCPU();

    /**
     * Completely reset emulator (CPU + memory)
     */
    void Reset();

    /**
     * Get last error message
     */
    const std::string& GetLastError() const { return last_error_; }

    /**
     * Get pages that were accessed during emulation
     */
    const std::unordered_set<uint64_t>& GetAccessedPages() const { return accessed_pages_; }

private:
    // Unicorn callbacks
    static bool OnMemoryUnmapped(void* uc, int type, uint64_t address,
                                   int size, int64_t value, void* user_data);
    static void OnMemoryAccess(void* uc, int type, uint64_t address,
                                int size, int64_t value, void* user_data);

    // Helper functions
    bool MapPage(uint64_t page_address);
    int RegToUnicorn(Reg reg);
    EmulationResult BuildResult(bool success, const std::string& error = "");

    void* uc_ = nullptr;  // Unicorn engine handle
    DMAInterface* dma_ = nullptr;
    uint32_t pid_ = 0;
    EmulatorConfig config_;
    std::string last_error_;

    // Memory tracking
    std::unordered_set<uint64_t> mapped_pages_;
    std::unordered_set<uint64_t> accessed_pages_;

    // Hook handles
    size_t mem_unmapped_hook_ = 0;
    size_t mem_access_hook_ = 0;
};

/**
 * Helper: Parse register name string to Reg enum
 */
std::optional<Reg> ParseRegister(const std::string& name);

/**
 * Helper: Get register name from Reg enum
 */
std::string RegisterName(Reg reg);

} // namespace emulation
} // namespace orpheus
