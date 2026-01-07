#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <map>

// Forward declare Zydis types to avoid header inclusion in header
struct ZydisDecoder_;
struct ZydisFormatter_;

namespace orpheus::analysis {

/**
 * InstructionInfo - Single disassembled instruction
 */
struct InstructionInfo {
    uint64_t address;
    uint8_t length;
    std::vector<uint8_t> bytes;
    std::string mnemonic;
    std::string operands;
    std::string full_text;      // Complete formatted instruction

    // Analysis info
    bool is_call;
    bool is_jump;
    bool is_ret;
    bool is_conditional;
    bool is_memory_access;

    // Branch target (if applicable)
    std::optional<uint64_t> branch_target;

    // Memory operand (if applicable)
    std::optional<uint64_t> memory_address;
};

/**
 * BasicBlock - Sequence of instructions ending in branch/ret
 */
struct BasicBlock {
    uint64_t start_address;
    uint64_t end_address;
    std::vector<InstructionInfo> instructions;
    std::vector<uint64_t> successors;  // Addresses of successor blocks
    std::vector<uint64_t> predecessors;
};

/**
 * DisassemblyOptions - Configuration for disassembly
 */
struct DisassemblyOptions {
    bool uppercase = true;           // Uppercase mnemonics
    bool show_address = true;        // Show address prefix
    bool show_bytes = true;          // Show instruction bytes
    bool resolve_rip_relative = true; // Calculate RIP-relative addresses
    uint32_t max_instructions = 1000; // Maximum instructions to disassemble
    size_t bytes_column_width = 24;   // Width for bytes column
};

/**
 * Disassembler - x64/x86 disassembly using Zydis
 *
 * Features:
 * - Full x64/x86 instruction support
 * - RIP-relative address resolution
 * - Branch target calculation
 * - Basic block identification
 */
class Disassembler {
public:
    /**
     * Create disassembler for specified architecture
     * @param is_64bit true for x64, false for x86
     */
    explicit Disassembler(bool is_64bit = true);
    ~Disassembler();

    // Disable copy
    Disassembler(const Disassembler&) = delete;
    Disassembler& operator=(const Disassembler&) = delete;

    // Allow move
    Disassembler(Disassembler&&) noexcept;
    Disassembler& operator=(Disassembler&&) noexcept;

    /**
     * Disassemble a buffer of bytes
     * @param data Bytes to disassemble
     * @param base_address Address of first byte
     * @param options Disassembly options
     * @return Vector of disassembled instructions
     */
    std::vector<InstructionInfo> Disassemble(const std::vector<uint8_t>& data,
                                               uint64_t base_address,
                                               const DisassemblyOptions& options = {});

    /**
     * Disassemble a single instruction
     * @param data Bytes (should be at least 15 bytes)
     * @param address Address of instruction
     * @return Instruction info, or nullopt if invalid
     */
    std::optional<InstructionInfo> DisassembleOne(const uint8_t* data,
                                                    size_t size,
                                                    uint64_t address);

    /**
     * Identify basic blocks in disassembly
     * @param instructions Already disassembled instructions
     * @return Map of start_address -> BasicBlock
     */
    std::map<uint64_t, BasicBlock> IdentifyBasicBlocks(
        const std::vector<InstructionInfo>& instructions);

    /**
     * Format a single instruction for display
     */
    std::string FormatInstruction(const InstructionInfo& instr,
                                   const DisassemblyOptions& options = {});

    /**
     * Check if disassembler is initialized
     */
    bool IsValid() const { return decoder_ != nullptr; }

    /**
     * Get architecture
     */
    bool Is64Bit() const { return is_64bit_; }

private:
    void InitDecoder();
    void CleanupDecoder();

    bool is_64bit_;
    ZydisDecoder_* decoder_ = nullptr;
    ZydisFormatter_* formatter_ = nullptr;
};

/**
 * Helper functions for instruction analysis
 */
namespace disasm {
    /**
     * Check if mnemonic is a call instruction
     */
    bool IsCall(const std::string& mnemonic);

    /**
     * Check if mnemonic is a jump instruction
     */
    bool IsJump(const std::string& mnemonic);

    /**
     * Check if mnemonic is a conditional jump
     */
    bool IsConditionalJump(const std::string& mnemonic);

    /**
     * Check if mnemonic is a return instruction
     */
    bool IsReturn(const std::string& mnemonic);

    /**
     * Format bytes as hex string
     */
    std::string FormatBytes(const std::vector<uint8_t>& bytes, size_t max_width = 0);

    /**
     * Format address as hex string
     */
    std::string FormatAddress(uint64_t address, bool is_64bit = true);
}

} // namespace orpheus::analysis
