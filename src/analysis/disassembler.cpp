#include "disassembler.h"
#include <Zydis/Zydis.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>

namespace orpheus::analysis {

Disassembler::Disassembler(bool is_64bit)
    : is_64bit_(is_64bit) {
    InitDecoder();
}

Disassembler::~Disassembler() {
    CleanupDecoder();
}

Disassembler::Disassembler(Disassembler&& other) noexcept
    : is_64bit_(other.is_64bit_)
    , decoder_(other.decoder_)
    , formatter_(other.formatter_) {
    other.decoder_ = nullptr;
    other.formatter_ = nullptr;
}

Disassembler& Disassembler::operator=(Disassembler&& other) noexcept {
    if (this != &other) {
        CleanupDecoder();
        is_64bit_ = other.is_64bit_;
        decoder_ = other.decoder_;
        formatter_ = other.formatter_;
        other.decoder_ = nullptr;
        other.formatter_ = nullptr;
    }
    return *this;
}

void Disassembler::InitDecoder() {
    decoder_ = new ZydisDecoder;
    formatter_ = new ZydisFormatter;

    ZydisMachineMode mode = is_64bit_ ? ZYDIS_MACHINE_MODE_LONG_64 : ZYDIS_MACHINE_MODE_LEGACY_32;
    ZydisStackWidth stack_width = is_64bit_ ? ZYDIS_STACK_WIDTH_64 : ZYDIS_STACK_WIDTH_32;

    if (!ZYAN_SUCCESS(ZydisDecoderInit(decoder_, mode, stack_width))) {
        CleanupDecoder();
        return;
    }

    if (!ZYAN_SUCCESS(ZydisFormatterInit(formatter_, ZYDIS_FORMATTER_STYLE_INTEL))) {
        CleanupDecoder();
        return;
    }

    // Configure formatter
    ZydisFormatterSetProperty(formatter_, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_FALSE);
    ZydisFormatterSetProperty(formatter_, ZYDIS_FORMATTER_PROP_UPPERCASE_MNEMONIC, ZYAN_TRUE);
    ZydisFormatterSetProperty(formatter_, ZYDIS_FORMATTER_PROP_UPPERCASE_REGISTERS, ZYAN_TRUE);
}

void Disassembler::CleanupDecoder() {
    if (decoder_) {
        delete decoder_;
        decoder_ = nullptr;
    }
    if (formatter_) {
        delete formatter_;
        formatter_ = nullptr;
    }
}

std::optional<InstructionInfo> Disassembler::DisassembleOne(const uint8_t* data,
                                                             size_t size,
                                                             uint64_t address) {
    if (!decoder_ || !formatter_ || !data || size == 0) {
        return std::nullopt;
    }

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(decoder_, data, size,
                                              &instruction, operands))) {
        return std::nullopt;
    }

    InstructionInfo info;
    info.address = address;
    info.length = instruction.length;

    // Copy instruction bytes
    info.bytes.assign(data, data + instruction.length);

    // Format instruction
    char buffer[256];
    if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(formatter_, &instruction,
                                                      operands, instruction.operand_count_visible,
                                                      buffer, sizeof(buffer), address, nullptr))) {
        info.full_text = buffer;

        // Extract mnemonic
        const char* mnemonic_str = ZydisMnemonicGetString(instruction.mnemonic);
        if (mnemonic_str) {
            info.mnemonic = mnemonic_str;
        }

        // Extract operands (everything after mnemonic)
        size_t mnemonic_end = info.full_text.find(' ');
        if (mnemonic_end != std::string::npos) {
            info.operands = info.full_text.substr(mnemonic_end + 1);
        }
    }

    // Analyze instruction type
    info.is_call = (instruction.meta.category == ZYDIS_CATEGORY_CALL);
    info.is_ret = (instruction.meta.category == ZYDIS_CATEGORY_RET);
    info.is_jump = (instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
                    instruction.meta.category == ZYDIS_CATEGORY_COND_BR);
    info.is_conditional = (instruction.meta.category == ZYDIS_CATEGORY_COND_BR);

    // Classify instruction category for syntax coloring
    if (info.is_call) {
        info.category = InstructionCategory::Call;
    } else if (info.is_ret) {
        info.category = InstructionCategory::Return;
    } else if (info.is_conditional) {
        info.category = InstructionCategory::ConditionalJump;
    } else if (info.is_jump) {
        info.category = InstructionCategory::Jump;
    } else if (instruction.meta.category == ZYDIS_CATEGORY_PUSH) {
        info.category = InstructionCategory::Push;
    } else if (instruction.meta.category == ZYDIS_CATEGORY_POP) {
        info.category = InstructionCategory::Pop;
    } else if (instruction.meta.category == ZYDIS_CATEGORY_NOP ||
               instruction.meta.category == ZYDIS_CATEGORY_INTERRUPT) {
        info.category = InstructionCategory::Nop;
    } else if (instruction.mnemonic == ZYDIS_MNEMONIC_CMP ||
               instruction.mnemonic == ZYDIS_MNEMONIC_TEST) {
        info.category = InstructionCategory::Compare;
    } else if (instruction.mnemonic == ZYDIS_MNEMONIC_SYSCALL ||
               instruction.mnemonic == ZYDIS_MNEMONIC_SYSENTER ||
               instruction.mnemonic == ZYDIS_MNEMONIC_HLT) {
        info.category = InstructionCategory::System;
    } else {
        info.category = InstructionCategory::Default;
    }

    // Check for memory access
    info.is_memory_access = false;
    for (size_t i = 0; i < instruction.operand_count; i++) {
        if (operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
            info.is_memory_access = true;

            // Calculate memory address if possible
            ZyanU64 result;
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operands[i], address, &result))) {
                info.memory_address = result;
            }
            break;
        }
    }

    // Calculate branch target
    if (info.is_call || info.is_jump) {
        for (size_t i = 0; i < instruction.operand_count; i++) {
            if (operands[i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
                ZyanU64 result;
                if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operands[i], address, &result))) {
                    info.branch_target = result;
                }
                break;
            }
        }
    }

    return info;
}

std::vector<InstructionInfo> Disassembler::Disassemble(const std::vector<uint8_t>& data,
                                                         uint64_t base_address,
                                                         const DisassemblyOptions& options) {
    std::vector<InstructionInfo> results;
    results.reserve(std::min(static_cast<size_t>(options.max_instructions), data.size() / 4));

    size_t offset = 0;
    while (offset < data.size() && results.size() < options.max_instructions) {
        auto instr = DisassembleOne(data.data() + offset, data.size() - offset,
                                     base_address + offset);

        if (instr) {
            results.push_back(std::move(*instr));
            offset += results.back().length;
        } else {
            // Invalid instruction - create a db entry
            InstructionInfo invalid;
            invalid.address = base_address + offset;
            invalid.length = 1;
            invalid.bytes = {data[offset]};
            invalid.mnemonic = "db";

            std::stringstream ss;
            ss << "0x" << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(data[offset]);
            invalid.operands = ss.str();
            invalid.full_text = "db " + invalid.operands;
            invalid.category = InstructionCategory::Nop;  // Treat invalid bytes like padding
            invalid.is_call = false;
            invalid.is_jump = false;
            invalid.is_ret = false;
            invalid.is_conditional = false;
            invalid.is_memory_access = false;

            results.push_back(invalid);
            offset++;
        }
    }

    return results;
}

std::map<uint64_t, BasicBlock> Disassembler::IdentifyBasicBlocks(
    const std::vector<InstructionInfo>& instructions) {

    std::map<uint64_t, BasicBlock> blocks;

    if (instructions.empty()) {
        return blocks;
    }

    // Find all block start addresses
    std::set<uint64_t> block_starts;
    block_starts.insert(instructions[0].address);  // First instruction is block start

    for (const auto& instr : instructions) {
        if (instr.is_call || instr.is_jump || instr.is_ret) {
            // Next instruction starts a new block
            uint64_t next_addr = instr.address + instr.length;
            block_starts.insert(next_addr);

            // Branch target starts a new block
            if (instr.branch_target) {
                block_starts.insert(*instr.branch_target);
            }
        }
    }

    // Build blocks
    BasicBlock* current_block = nullptr;

    for (const auto& instr : instructions) {
        // Start new block if needed
        if (block_starts.count(instr.address) || current_block == nullptr) {
            if (current_block) {
                current_block->end_address = current_block->instructions.back().address +
                                              current_block->instructions.back().length;
            }

            blocks[instr.address] = BasicBlock();
            current_block = &blocks[instr.address];
            current_block->start_address = instr.address;
        }

        current_block->instructions.push_back(instr);

        // Record successors
        if (instr.is_jump || instr.is_call) {
            if (instr.branch_target) {
                current_block->successors.push_back(*instr.branch_target);
            }

            // For conditional jumps and calls, fall-through is also a successor
            if (instr.is_conditional || instr.is_call) {
                current_block->successors.push_back(instr.address + instr.length);
            }
        } else if (!instr.is_ret) {
            // Implicit fall-through successor (will be set on block end)
        }
    }

    // Set end address for last block
    if (current_block && !current_block->instructions.empty()) {
        current_block->end_address = current_block->instructions.back().address +
                                      current_block->instructions.back().length;

        // Add fall-through successor if not ending with ret or unconditional jump
        const auto& last_instr = current_block->instructions.back();
        if (!last_instr.is_ret && !(last_instr.is_jump && !last_instr.is_conditional)) {
            current_block->successors.push_back(current_block->end_address);
        }
    }

    // Build predecessor lists
    for (auto& [addr, block] : blocks) {
        for (uint64_t succ_addr : block.successors) {
            auto it = blocks.find(succ_addr);
            if (it != blocks.end()) {
                it->second.predecessors.push_back(addr);
            }
        }
    }

    return blocks;
}

std::string Disassembler::FormatInstruction(const InstructionInfo& instr,
                                             const DisassemblyOptions& options) {
    std::stringstream ss;

    if (options.show_address) {
        ss << disasm::FormatAddress(instr.address, is_64bit_) << "  ";
    }

    if (options.show_bytes) {
        std::string bytes_str = disasm::FormatBytes(instr.bytes, options.bytes_column_width);
        ss << bytes_str;

        // Pad to fixed width
        if (bytes_str.length() < options.bytes_column_width) {
            ss << std::string(options.bytes_column_width - bytes_str.length(), ' ');
        }
        ss << "  ";
    }

    ss << instr.full_text;

    return ss.str();
}

namespace disasm {

bool IsCall(const std::string& mnemonic) {
    return mnemonic.find("CALL") != std::string::npos;
}

bool IsJump(const std::string& mnemonic) {
    if (mnemonic.empty()) return false;
    char first = mnemonic[0];
    return first == 'J' || mnemonic == "LOOP" || mnemonic == "LOOPE" ||
           mnemonic == "LOOPNE" || mnemonic == "LOOPNZ" || mnemonic == "LOOPZ";
}

bool IsConditionalJump(const std::string& mnemonic) {
    if (!IsJump(mnemonic)) return false;
    return mnemonic != "JMP";
}

bool IsReturn(const std::string& mnemonic) {
    return mnemonic == "RET" || mnemonic == "RETN" || mnemonic == "RETF" ||
           mnemonic == "IRET" || mnemonic == "IRETD" || mnemonic == "IRETQ";
}

std::string FormatBytes(const std::vector<uint8_t>& bytes, size_t max_width) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    size_t count = bytes.size();
    if (max_width > 0) {
        // Each byte takes 3 chars (2 hex + space)
        size_t max_bytes = max_width / 3;
        count = std::min(count, max_bytes);
    }

    for (size_t i = 0; i < count; i++) {
        if (i > 0) ss << " ";
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }

    if (count < bytes.size()) {
        ss << "...";
    }

    return ss.str();
}

std::string FormatAddress(uint64_t address, bool is_64bit) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    if (is_64bit) {
        ss << std::setw(16) << address;
    } else {
        ss << std::setw(8) << static_cast<uint32_t>(address);
    }

    return ss.str();
}

} // namespace disasm

} // namespace orpheus::analysis
