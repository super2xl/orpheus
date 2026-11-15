#include "function_recovery.h"
#include "disassembler.h"

#include <algorithm>
#include <cstring>

namespace orpheus::analysis {

// PE header constants
static constexpr uint32_t PE_SIGNATURE = 0x00004550;  // "PE\0\0"
static constexpr uint16_t PE_MAGIC_64 = 0x20B;
static constexpr uint16_t PE_MAGIC_32 = 0x10B;
static constexpr uint32_t IMAGE_DIRECTORY_ENTRY_EXCEPTION = 3;
static constexpr uint32_t IMAGE_DIRECTORY_ENTRY_EXPORT = 0;

FunctionRecovery::FunctionRecovery(ReadMemoryFn read_memory,
                                   uint64_t module_base,
                                   uint32_t module_size,
                                   bool is_64bit)
    : read_memory_(std::move(read_memory))
    , module_base_(module_base)
    , module_size_(module_size)
    , is_64bit_(is_64bit) {
}

FunctionRecovery::~FunctionRecovery() = default;

std::map<uint64_t, FunctionInfo> FunctionRecovery::RecoverFunctions(
    const FunctionRecoveryOptions& options,
    ProgressCallback progress) {

    functions_.clear();
    call_targets_.clear();

    // Stage 1: Parse exception data (most reliable on x64)
    if (options.use_exception_data && is_64bit_) {
        if (progress) progress("Parsing exception data", 0.0f);
        ParseExceptionData(options, progress);
    }

    // Stage 2: Scan for prologues
    if (options.use_prologues) {
        if (progress) progress("Scanning for prologues", 0.2f);
        ScanForPrologues(options, progress);
    }

    // Stage 3: Follow CALL instructions
    if (options.follow_calls) {
        if (progress) progress("Following call targets", 0.5f);
        FollowCalls(options, progress);
    }

    // Stage 4: Analyze call graph
    if (options.analyze_callgraph) {
        if (progress) progress("Analyzing call graph", 0.8f);
        AnalyzeCallGraph(options, progress);
    }

    // Stage 5: Detect thunks
    if (options.detect_thunks) {
        DetectThunks(options);
    }

    // Stage 6: .pdata ranges are authoritative; drop heuristic "functions" that start
    // inside one (prologue/call-target false positives mid-function).
    PruneInsideExceptionRanges();

    // Stage 7: Compute function bounds where possible
    ComputeFunctionBounds();

    if (progress) progress("Complete", 1.0f);

    return functions_;
}

void FunctionRecovery::ScanForPrologues(const FunctionRecoveryOptions& options,
                                         ProgressCallback progress) {
    // Read .text section or scan entire module
    // For now, scan first portion of module (typically code section)
    size_t scan_size = std::min(static_cast<size_t>(module_size_),
                                 options.max_prologue_scan);

    auto data = read_memory_(module_base_, scan_size);
    if (data.empty()) return;

    size_t found_count = 0;
    float last_progress = 0.0f;

    for (size_t i = 0; i < data.size() - 16 && functions_.size() < options.max_functions; i++) {
        // Report progress periodically
        if (progress && (i % 0x10000) == 0) {
            float p = 0.2f + 0.3f * (static_cast<float>(i) / data.size());
            if (p - last_progress > 0.01f) {
                progress("Scanning for prologues", p);
                last_progress = p;
            }
        }

        // Check alignment - functions typically start at 16-byte boundaries
        // but not always, so we check both aligned and unaligned
        bool is_aligned = (i % 16) == 0;

        if (IsPrologueAt(data.data(), data.size(), i)) {
            uint64_t func_addr = module_base_ + i;

            // Skip if we already have this function from a more reliable source
            if (functions_.count(func_addr)) continue;

            FunctionInfo info;
            info.entry_address = func_addr;
            info.source = FunctionInfo::Source::Prologue;
            info.confidence = is_aligned ? 0.8f : 0.6f;

            functions_[func_addr] = info;
            found_count++;
        }
    }
}

bool FunctionRecovery::IsPrologueAt(const uint8_t* data, size_t size, size_t offset) {
    if (offset + 16 > size) return false;

    const uint8_t* p = data + offset;

    // Check for common prologues
    using namespace prologues;

    // push rbp; mov rbp, rsp
    if (memcmp(p, PUSH_RBP_MOV, PUSH_RBP_MOV_LEN) == 0) {
        return true;
    }
    if (memcmp(p, PUSH_RBP_MOV_ALT, PUSH_RBP_MOV_ALT_LEN) == 0) {
        return true;
    }

    // sub rsp, imm8 (common for functions that don't use rbp)
    if (memcmp(p, SUB_RSP_IMM8, SUB_RSP_IMM8_LEN) == 0) {
        // Verify the immediate is reasonable (0x08 - 0x80 typically)
        uint8_t imm = p[3];
        if (imm >= 0x08 && imm <= 0x80 && (imm & 0x7) == 0) {
            return true;
        }
    }

    // sub rsp, imm32
    if (memcmp(p, SUB_RSP_IMM32, SUB_RSP_IMM32_LEN) == 0) {
        // Reasonable stack frame (up to 4KB)
        uint32_t imm = *reinterpret_cast<const uint32_t*>(p + 3);
        if (imm >= 0x08 && imm <= 0x1000 && (imm & 0x7) == 0) {
            return true;
        }
    }

    // mov [rsp+8], rbx; mov [rsp+16], rbp (common in MS x64 ABI)
    if (memcmp(p, MOV_RSP8_RBX, MOV_RSP8_RBX_LEN) == 0) {
        // Check if followed by another register save or sub rsp
        if (offset + 10 <= size) {
            const uint8_t* p2 = p + 5;  // After first mov
            if (memcmp(p2, SUB_RSP_IMM8, SUB_RSP_IMM8_LEN) == 0 ||
                memcmp(p2, SUB_RSP_IMM32, SUB_RSP_IMM32_LEN) == 0 ||
                (p2[0] == 0x48 && p2[1] == 0x89)) {  // Another mov
                return true;
            }
        }
    }

    // mov [rsp+8], rcx (saving first parameter - common start)
    if (memcmp(p, MOV_RSP_RCX, MOV_RSP_RCX_LEN) == 0) {
        uint8_t offset_byte = p[4];
        if (offset_byte == 0x08) {  // [rsp+8]
            // Check if followed by push or sub rsp
            if (offset + 10 <= size) {
                const uint8_t* p2 = p + 5;
                if (p2[0] == 0x53 ||  // push rbx
                    p2[0] == 0x55 ||  // push rbp
                    p2[0] == 0x56 ||  // push rsi
                    p2[0] == 0x57 ||  // push rdi
                    memcmp(p2, SUB_RSP_IMM8, SUB_RSP_IMM8_LEN) == 0) {
                    return true;
                }
            }
        }
    }

    // For x86, check push ebp; mov ebp, esp
    if (!is_64bit_) {
        if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC) {
            return true;  // push ebp; mov ebp, esp
        }
        if (p[0] == 0x55 && p[1] == 0x89 && p[2] == 0xE5) {
            return true;  // push ebp; mov ebp, esp (alternate)
        }
    }

    return false;
}

void FunctionRecovery::FollowCalls(const FunctionRecoveryOptions& options,
                                    ProgressCallback progress) {
    // Disassemble known functions and find CALL targets
    Disassembler disasm(is_64bit_);

    // First pass: collect call targets from module code
    size_t scan_size = std::min(static_cast<size_t>(module_size_),
                                 options.max_prologue_scan);
    auto data = read_memory_(module_base_, scan_size);
    if (data.empty()) return;

    DisassemblyOptions opts;
    opts.max_instructions = 10000000;  // No real limit for scanning

    // Simple scan for E8 (call rel32) instructions
    for (size_t i = 0; i < data.size() - 5 && call_targets_.size() < options.max_functions; i++) {
        if (data[i] == 0xE8) {  // CALL rel32
            int32_t rel = *reinterpret_cast<const int32_t*>(&data[i + 1]);
            uint64_t target = module_base_ + i + 5 + rel;

            // Check if target is within module
            if (target >= module_base_ && target < module_base_ + module_size_) {
                call_targets_.insert(target);
            }
        }
    }

    // Add call targets as functions
    for (uint64_t target : call_targets_) {
        if (functions_.count(target)) {
            // Boost confidence if we found it via call target too
            functions_[target].confidence = std::min(1.0f, functions_[target].confidence + 0.1f);
            continue;
        }

        FunctionInfo info;
        info.entry_address = target;
        info.source = FunctionInfo::Source::CallTarget;
        info.confidence = 0.7f;

        functions_[target] = info;
    }
}

void FunctionRecovery::ParseExceptionData(const FunctionRecoveryOptions& options,
                                           ProgressCallback progress) {
    auto runtime_funcs = GetExceptionDirectory();
    if (!runtime_funcs) return;

    for (const auto& rf : *runtime_funcs) {
        if (functions_.size() >= options.max_functions) break;

        uint64_t func_addr = module_base_ + rf.begin_address;
        uint64_t func_end = module_base_ + rf.end_address;

        // Skip if we somehow got an invalid entry
        if (rf.begin_address >= rf.end_address) continue;
        if (rf.begin_address >= module_size_) continue;

        FunctionInfo info;
        info.entry_address = func_addr;
        info.end_address = func_end;
        info.size = rf.end_address - rf.begin_address;
        info.source = FunctionInfo::Source::ExceptionData;
        info.confidence = 1.0f;  // .pdata is authoritative
        info.has_unwind_info = true;

        functions_[func_addr] = info;
    }
}

std::optional<std::vector<RuntimeFunction>> FunctionRecovery::GetExceptionDirectory() {
    uint32_t pdata_size = 0;
    auto pdata_rva = GetDataDirectory(IMAGE_DIRECTORY_ENTRY_EXCEPTION, pdata_size);
    if (!pdata_rva || pdata_size == 0) return std::nullopt;

    uint64_t pdata_addr = module_base_ + *pdata_rva;
    auto data = read_memory_(pdata_addr, pdata_size);
    if (data.size() < sizeof(RuntimeFunction)) return std::nullopt;

    std::vector<RuntimeFunction> result;
    size_t count = data.size() / sizeof(RuntimeFunction);
    result.reserve(count);

    const RuntimeFunction* entries = reinterpret_cast<const RuntimeFunction*>(data.data());
    for (size_t i = 0; i < count; i++) {
        // Skip, don't stop: pages of .pdata that are paged out on the target read back
        // zero-padded. Stopping at the first zero entry dropped ~78% of Explorer's
        // 12,411 entries because one page 32 KB in was non-resident.
        const auto& rf = entries[i];
        if (rf.begin_address == 0 || rf.end_address <= rf.begin_address) continue;
        if (module_size_ != 0 && rf.end_address > module_size_) continue;
        result.push_back(rf);
    }

    return result;
}

std::optional<uint64_t> FunctionRecovery::GetDataDirectory(uint32_t index, uint32_t& size) {
    // Read DOS header
    auto dos_header = read_memory_(module_base_, 64);
    if (dos_header.size() < 64) return std::nullopt;

    // Check DOS signature
    if (dos_header[0] != 'M' || dos_header[1] != 'Z') return std::nullopt;

    // Get PE header offset
    uint32_t pe_offset = *reinterpret_cast<const uint32_t*>(&dos_header[0x3C]);
    if (pe_offset > module_size_ - 256) return std::nullopt;

    // Read PE header
    auto pe_header = read_memory_(module_base_ + pe_offset, 256);
    if (pe_header.size() < 256) return std::nullopt;

    // Check PE signature
    if (*reinterpret_cast<const uint32_t*>(pe_header.data()) != PE_SIGNATURE) return std::nullopt;

    // Get optional header offset
    uint16_t optional_header_offset = 24;  // After PE signature + COFF header

    // Check magic
    uint16_t magic = *reinterpret_cast<const uint16_t*>(&pe_header[optional_header_offset]);
    bool is_pe64 = (magic == PE_MAGIC_64);

    // Data directory offset within optional header
    uint32_t dd_offset = is_pe64 ? 112 : 96;

    // Data directory entry
    uint32_t entry_offset = optional_header_offset + dd_offset + index * 8;
    if (entry_offset + 8 > pe_header.size()) return std::nullopt;

    uint32_t rva = *reinterpret_cast<const uint32_t*>(&pe_header[entry_offset]);
    size = *reinterpret_cast<const uint32_t*>(&pe_header[entry_offset + 4]);

    if (rva == 0) return std::nullopt;
    return rva;
}

void FunctionRecovery::AnalyzeCallGraph(const FunctionRecoveryOptions& options,
                                         ProgressCallback progress) {
    Disassembler disasm(is_64bit_);

    size_t analyzed = 0;
    float last_progress = 0.8f;

    for (auto& [addr, func] : functions_) {
        analyzed++;

        if (progress && (analyzed % 100) == 0) {
            float p = 0.8f + 0.15f * (static_cast<float>(analyzed) / functions_.size());
            if (p - last_progress > 0.01f) {
                progress("Analyzing call graph", p);
                last_progress = p;
            }
        }

        // Read function bytes
        size_t func_size = func.size > 0 ? func.size : 4096;  // Default to 4KB max
        func_size = std::min(func_size, static_cast<size_t>(4096));

        auto data = read_memory_(addr, func_size);
        if (data.empty()) continue;

        // Disassemble
        DisassemblyOptions opts;
        opts.max_instructions = 500;

        auto instructions = disasm.Disassemble(data, addr, opts);
        if (instructions.empty()) continue;

        func.instruction_count = static_cast<uint32_t>(instructions.size());

        // Find calls and returns
        bool has_call = false;
        bool found_ret = false;

        for (const auto& instr : instructions) {
            if (instr.is_ret) {
                found_ret = true;
                // Function likely ends here
                if (func.end_address == 0 || instr.address + instr.length < func.end_address) {
                    // Only update if we don't have better bounds
                }
            }

            if (instr.is_call && instr.branch_target) {
                has_call = true;
                uint64_t target = *instr.branch_target;

                // Add to callees
                func.callees.push_back(target);

                // Add caller reference to target function
                auto it = functions_.find(target);
                if (it != functions_.end()) {
                    it->second.callers.push_back(addr);
                }
            }
        }

        func.is_leaf = !has_call;

        // Compute basic blocks
        auto blocks = disasm.IdentifyBasicBlocks(instructions);
        func.basic_block_count = static_cast<uint32_t>(blocks.size());
    }
}

void FunctionRecovery::DetectThunks(const FunctionRecoveryOptions& options) {
    Disassembler disasm(is_64bit_);

    for (auto& [addr, func] : functions_) {
        // Read first few bytes
        auto data = read_memory_(addr, 16);
        if (data.size() < 5) continue;

        // Check for jmp rel32 (E9 XX XX XX XX)
        if (data[0] == 0xE9) {
            int32_t rel = *reinterpret_cast<const int32_t*>(&data[1]);
            uint64_t target = addr + 5 + rel;

            // Check if target is a known function
            if (functions_.count(target)) {
                func.is_thunk = true;
                func.size = 5;
                func.end_address = addr + 5;
            }
        }

        // Check for jmp [rip+rel32] (FF 25 XX XX XX XX) - import thunk
        if (data[0] == 0xFF && data[1] == 0x25) {
            func.is_thunk = true;
            func.size = 6;
            func.end_address = addr + 6;
        }
    }
}

void FunctionRecovery::PruneInsideExceptionRanges() {
    // functions_ is ordered by address. Walk it once, tracking the furthest end of any
    // .pdata function seen so far; a non-.pdata entry starting before that end lies
    // inside a known function body.
    uint64_t covered_until = 0;
    for (auto it = functions_.begin(); it != functions_.end();) {
        const auto& f = it->second;
        if (f.source == FunctionInfo::Source::ExceptionData) {
            covered_until = std::max(covered_until, f.end_address);
            ++it;
        } else if (it->first < covered_until) {
            it = functions_.erase(it);
        } else {
            ++it;
        }
    }
}

void FunctionRecovery::ComputeFunctionBounds() {
    // Sort functions by address
    std::vector<uint64_t> sorted_addrs;
    sorted_addrs.reserve(functions_.size());
    for (const auto& [addr, _] : functions_) {
        sorted_addrs.push_back(addr);
    }
    std::sort(sorted_addrs.begin(), sorted_addrs.end());

    // For functions without bounds, estimate from next function
    for (size_t i = 0; i < sorted_addrs.size(); i++) {
        auto& func = functions_[sorted_addrs[i]];

        if (func.end_address == 0 && i + 1 < sorted_addrs.size()) {
            // Estimate end as start of next function (minus alignment padding)
            uint64_t next_addr = sorted_addrs[i + 1];
            uint64_t gap = next_addr - func.entry_address;

            // Reasonable function size limit
            if (gap <= 0x10000) {
                func.end_address = next_addr;
                func.size = static_cast<uint32_t>(gap);
            }
        }
    }
}

void FunctionRecovery::AddRTTIFunctions(const std::map<uint64_t, std::vector<uint64_t>>& vtable_methods) {
    for (const auto& [vtable_addr, methods] : vtable_methods) {
        for (uint64_t method_addr : methods) {
            // Skip if outside module
            if (method_addr < module_base_ || method_addr >= module_base_ + module_size_) {
                continue;
            }

            if (functions_.count(method_addr)) {
                // Boost confidence and mark as virtual
                functions_[method_addr].confidence = std::min(1.0f, functions_[method_addr].confidence + 0.1f);
                functions_[method_addr].is_virtual_method = true;
                continue;
            }

            FunctionInfo info;
            info.entry_address = method_addr;
            info.source = FunctionInfo::Source::RTTI;
            info.confidence = 0.9f;
            info.is_virtual_method = true;

            functions_[method_addr] = info;
        }
    }
}

void FunctionRecovery::AddExportedFunctions(const std::map<uint64_t, std::string>& exports) {
    for (const auto& [addr, name] : exports) {
        if (functions_.count(addr)) {
            // Add name to existing function
            functions_[addr].name = name;
            functions_[addr].confidence = std::min(1.0f, functions_[addr].confidence + 0.2f);
            continue;
        }

        FunctionInfo info;
        info.entry_address = addr;
        info.name = name;
        info.source = FunctionInfo::Source::Export;
        info.confidence = 1.0f;

        functions_[addr] = info;
    }
}

void FunctionRecovery::AddFunction(uint64_t address, const std::string& name,
                                    FunctionInfo::Source source) {
    FunctionInfo info;
    info.entry_address = address;
    info.name = name;
    info.source = source;
    info.confidence = 1.0f;

    functions_[address] = info;
}

std::optional<FunctionInfo> FunctionRecovery::GetFunctionAt(uint64_t address) const {
    auto it = functions_.find(address);
    if (it != functions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<FunctionInfo> FunctionRecovery::GetFunctionContaining(uint64_t address) const {
    // Binary search for function containing address
    auto it = functions_.upper_bound(address);
    if (it == functions_.begin()) {
        return std::nullopt;
    }
    --it;

    const auto& func = it->second;
    if (func.end_address > 0 && address >= func.end_address) {
        return std::nullopt;
    }

    // If no end address, check if within reasonable bounds
    if (func.end_address == 0) {
        uint64_t offset = address - func.entry_address;
        if (offset > 0x10000) {  // 64KB max guess
            return std::nullopt;
        }
    }

    return func;
}

} // namespace orpheus::analysis
