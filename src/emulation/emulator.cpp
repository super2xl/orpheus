#include "emulator.h"
#include "../core/dma_interface.h"

#include <unicorn/unicorn.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace orpheus {
namespace emulation {

// Page size constant
constexpr uint64_t PAGE_SIZE = 0x1000;
constexpr uint64_t PAGE_MASK = ~(PAGE_SIZE - 1);

// Helper to align address down to page boundary
static uint64_t AlignToPage(uint64_t addr) {
    return addr & PAGE_MASK;
}

// Helper to calculate pages needed for a region
static size_t PagesNeeded(uint64_t addr, size_t size) {
    uint64_t start_page = AlignToPage(addr);
    uint64_t end_page = AlignToPage(addr + size + PAGE_SIZE - 1);
    return static_cast<size_t>((end_page - start_page) / PAGE_SIZE);
}

Emulator::Emulator() = default;

Emulator::~Emulator() {
    Reset();
}

bool Emulator::Initialize(DMAInterface* dma, uint32_t pid, const EmulatorConfig& config) {
    if (uc_) {
        Reset();
    }

    dma_ = dma;
    pid_ = pid;
    config_ = config;

    // Create Unicorn engine for x86-64
    uc_engine* uc = nullptr;
    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &uc);
    if (err != UC_ERR_OK) {
        last_error_ = std::string("Failed to create Unicorn engine: ") + uc_strerror(err);
        spdlog::error("{}", last_error_);
        return false;
    }
    uc_ = uc;

    // Set up stack
    err = uc_mem_map(uc, config_.stack_base, config_.stack_size, UC_PROT_READ | UC_PROT_WRITE);
    if (err != UC_ERR_OK) {
        last_error_ = std::string("Failed to map stack: ") + uc_strerror(err);
        spdlog::error("{}", last_error_);
        uc_close(uc);
        uc_ = nullptr;
        return false;
    }

    // Initialize RSP to middle of stack
    uint64_t rsp = config_.stack_base + config_.stack_size / 2;
    uc_reg_write(uc, UC_X86_REG_RSP, &rsp);
    uc_reg_write(uc, UC_X86_REG_RBP, &rsp);

    // Set up memory unmapped hook for lazy mapping
    if (config_.lazy_mapping) {
        err = uc_hook_add(uc, &mem_unmapped_hook_,
                         UC_HOOK_MEM_UNMAPPED,
                         reinterpret_cast<void*>(OnMemoryUnmapped),
                         this, 1, 0);
        if (err != UC_ERR_OK) {
            spdlog::warn("Failed to add memory unmapped hook: {}", uc_strerror(err));
        }
    }

    // Set up memory access hook for tracking
    err = uc_hook_add(uc, &mem_access_hook_,
                     UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                     reinterpret_cast<void*>(OnMemoryAccess),
                     this, 1, 0);
    if (err != UC_ERR_OK) {
        spdlog::warn("Failed to add memory access hook: {}", uc_strerror(err));
    }

    spdlog::info("Emulator initialized for PID {} with {}MB stack",
                 pid_, config_.stack_size / (1024 * 1024));
    return true;
}

bool Emulator::MapRegion(uint64_t address, size_t size) {
    if (!uc_ || !dma_) {
        last_error_ = "Emulator not initialized";
        return false;
    }

    uc_engine* uc = static_cast<uc_engine*>(uc_);

    // Align to page boundaries
    uint64_t page_start = AlignToPage(address);
    uint64_t page_end = AlignToPage(address + size + PAGE_SIZE - 1);
    size_t aligned_size = page_end - page_start;

    // Check if already mapped
    bool already_mapped = true;
    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE) {
        if (mapped_pages_.find(page) == mapped_pages_.end()) {
            already_mapped = false;
            break;
        }
    }
    if (already_mapped) {
        return true;  // Already fully mapped
    }

    // Read memory from target process
    auto buffer = dma_->ReadMemory(pid_, page_start, aligned_size);
    if (buffer.empty()) {
        spdlog::warn("Failed to read 0x{:X} bytes at 0x{:016X}, filling with zeros", aligned_size, page_start);
        // Continue anyway - we'll map it but with zeros
        buffer.resize(aligned_size, 0);
    }

    // Map pages that aren't already mapped
    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE) {
        if (mapped_pages_.find(page) != mapped_pages_.end()) {
            // Already mapped, just write data
            size_t offset = page - page_start;
            uc_mem_write(uc, page, buffer.data() + offset, PAGE_SIZE);
        } else {
            // Map new page
            uc_err err = uc_mem_map(uc, page, PAGE_SIZE, UC_PROT_ALL);
            if (err != UC_ERR_OK && err != UC_ERR_MAP) {
                // UC_ERR_MAP means already mapped, which is fine
                last_error_ = std::string("Failed to map page: ") + uc_strerror(err);
                spdlog::error("Failed to map page at 0x{:016X}: {}", page, uc_strerror(err));
                return false;
            }

            // Write data to mapped page
            size_t offset = page - page_start;
            uc_mem_write(uc, page, buffer.data() + offset, PAGE_SIZE);
            mapped_pages_.insert(page);
        }
    }

    spdlog::debug("Mapped region 0x{:016X} - 0x{:016X} ({} pages)",
                  page_start, page_end, aligned_size / PAGE_SIZE);
    return true;
}

bool Emulator::MapModule(const std::string& module_name) {
    if (!uc_ || !dma_) {
        last_error_ = "Emulator not initialized";
        return false;
    }

    auto modules = dma_->GetModuleList(pid_);
    for (const auto& mod : modules) {
        if (mod.name == module_name ||
            mod.name.find(module_name) != std::string::npos) {
            spdlog::info("Mapping module {} at 0x{:016X} ({} bytes)",
                        mod.name, mod.base_address, mod.size);
            return MapRegion(mod.base_address, mod.size);
        }
    }

    last_error_ = "Module not found: " + module_name;
    return false;
}

bool Emulator::MapPage(uint64_t page_address) {
    if (!uc_ || !dma_) {
        return false;
    }

    page_address = AlignToPage(page_address);

    if (mapped_pages_.find(page_address) != mapped_pages_.end()) {
        return true;  // Already mapped
    }

    uc_engine* uc = static_cast<uc_engine*>(uc_);

    // Read page from target process
    auto buffer = dma_->ReadMemory(pid_, page_address, PAGE_SIZE);
    if (buffer.empty()) {
        spdlog::warn("Failed to read page at 0x{:016X}, mapping with zeros", page_address);
        buffer.resize(PAGE_SIZE, 0);
    }

    // Map page
    uc_err err = uc_mem_map(uc, page_address, PAGE_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        spdlog::error("Failed to map page at 0x{:016X}: {}", page_address, uc_strerror(err));
        return false;
    }

    // Write data
    uc_mem_write(uc, page_address, buffer.data(), PAGE_SIZE);
    mapped_pages_.insert(page_address);

    spdlog::debug("Lazy-mapped page at 0x{:016X}", page_address);
    return true;
}

int Emulator::RegToUnicorn(Reg reg) {
    switch (reg) {
        case Reg::RAX: return UC_X86_REG_RAX;
        case Reg::RBX: return UC_X86_REG_RBX;
        case Reg::RCX: return UC_X86_REG_RCX;
        case Reg::RDX: return UC_X86_REG_RDX;
        case Reg::RSI: return UC_X86_REG_RSI;
        case Reg::RDI: return UC_X86_REG_RDI;
        case Reg::RBP: return UC_X86_REG_RBP;
        case Reg::RSP: return UC_X86_REG_RSP;
        case Reg::R8:  return UC_X86_REG_R8;
        case Reg::R9:  return UC_X86_REG_R9;
        case Reg::R10: return UC_X86_REG_R10;
        case Reg::R11: return UC_X86_REG_R11;
        case Reg::R12: return UC_X86_REG_R12;
        case Reg::R13: return UC_X86_REG_R13;
        case Reg::R14: return UC_X86_REG_R14;
        case Reg::R15: return UC_X86_REG_R15;
        case Reg::RIP: return UC_X86_REG_RIP;
        case Reg::RFLAGS: return UC_X86_REG_RFLAGS;
        case Reg::XMM0: return UC_X86_REG_XMM0;
        case Reg::XMM1: return UC_X86_REG_XMM1;
        case Reg::XMM2: return UC_X86_REG_XMM2;
        case Reg::XMM3: return UC_X86_REG_XMM3;
        case Reg::XMM4: return UC_X86_REG_XMM4;
        case Reg::XMM5: return UC_X86_REG_XMM5;
        case Reg::XMM6: return UC_X86_REG_XMM6;
        case Reg::XMM7: return UC_X86_REG_XMM7;
        case Reg::XMM8: return UC_X86_REG_XMM8;
        case Reg::XMM9: return UC_X86_REG_XMM9;
        case Reg::XMM10: return UC_X86_REG_XMM10;
        case Reg::XMM11: return UC_X86_REG_XMM11;
        case Reg::XMM12: return UC_X86_REG_XMM12;
        case Reg::XMM13: return UC_X86_REG_XMM13;
        case Reg::XMM14: return UC_X86_REG_XMM14;
        case Reg::XMM15: return UC_X86_REG_XMM15;
        default: return -1;
    }
}

bool Emulator::SetRegister(Reg reg, uint64_t value) {
    if (!uc_) {
        last_error_ = "Emulator not initialized";
        return false;
    }

    int uc_reg = RegToUnicorn(reg);
    if (uc_reg < 0) {
        last_error_ = "Invalid register";
        return false;
    }

    uc_engine* uc = static_cast<uc_engine*>(uc_);
    uc_err err = uc_reg_write(uc, uc_reg, &value);
    if (err != UC_ERR_OK) {
        last_error_ = std::string("Failed to set register: ") + uc_strerror(err);
        return false;
    }
    return true;
}

std::optional<uint64_t> Emulator::GetRegister(Reg reg) {
    if (!uc_) {
        last_error_ = "Emulator not initialized";
        return std::nullopt;
    }

    int uc_reg = RegToUnicorn(reg);
    if (uc_reg < 0) {
        last_error_ = "Invalid register";
        return std::nullopt;
    }

    uc_engine* uc = static_cast<uc_engine*>(uc_);
    uint64_t value = 0;
    uc_err err = uc_reg_read(uc, uc_reg, &value);
    if (err != UC_ERR_OK) {
        last_error_ = std::string("Failed to read register: ") + uc_strerror(err);
        return std::nullopt;
    }
    return value;
}

bool Emulator::SetXMM(int index, const XMMValue& value) {
    if (!uc_) {
        last_error_ = "Emulator not initialized";
        return false;
    }

    if (index < 0 || index > 15) {
        last_error_ = "Invalid XMM index";
        return false;
    }

    Reg reg = static_cast<Reg>(static_cast<int>(Reg::XMM0) + index);
    int uc_reg = RegToUnicorn(reg);

    uc_engine* uc = static_cast<uc_engine*>(uc_);

    // XMM registers are 128-bit, Unicorn uses uc_x86_xmm struct
    uint8_t xmm_data[16];
    memcpy(xmm_data, &value.lo, 8);
    memcpy(xmm_data + 8, &value.hi, 8);

    uc_err err = uc_reg_write(uc, uc_reg, xmm_data);
    if (err != UC_ERR_OK) {
        last_error_ = std::string("Failed to set XMM register: ") + uc_strerror(err);
        return false;
    }
    return true;
}

std::optional<XMMValue> Emulator::GetXMM(int index) {
    if (!uc_) {
        last_error_ = "Emulator not initialized";
        return std::nullopt;
    }

    if (index < 0 || index > 15) {
        last_error_ = "Invalid XMM index";
        return std::nullopt;
    }

    Reg reg = static_cast<Reg>(static_cast<int>(Reg::XMM0) + index);
    int uc_reg = RegToUnicorn(reg);

    uc_engine* uc = static_cast<uc_engine*>(uc_);

    uint8_t xmm_data[16];
    uc_err err = uc_reg_read(uc, uc_reg, xmm_data);
    if (err != UC_ERR_OK) {
        last_error_ = std::string("Failed to read XMM register: ") + uc_strerror(err);
        return std::nullopt;
    }

    XMMValue value;
    memcpy(&value.lo, xmm_data, 8);
    memcpy(&value.hi, xmm_data + 8, 8);
    return value;
}

bool Emulator::SetRegisters(const std::unordered_map<std::string, uint64_t>& regs) {
    for (const auto& [name, value] : regs) {
        auto reg = ParseRegister(name);
        if (!reg) {
            last_error_ = "Unknown register: " + name;
            return false;
        }
        if (!SetRegister(*reg, value)) {
            return false;
        }
    }
    return true;
}

EmulationResult Emulator::Run(uint64_t start_address, uint64_t end_address) {
    if (!uc_) {
        return BuildResult(false, "Emulator not initialized");
    }

    uc_engine* uc = static_cast<uc_engine*>(uc_);
    accessed_pages_.clear();

    // Pre-map the code region if not using lazy mapping
    if (!config_.lazy_mapping) {
        if (!MapRegion(start_address, end_address - start_address)) {
            return BuildResult(false, "Failed to map code region");
        }
    } else {
        // At minimum, map the first page
        if (!MapPage(AlignToPage(start_address))) {
            return BuildResult(false, "Failed to map starting page");
        }
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Run emulation
    uc_err err = uc_emu_start(uc, start_address, end_address,
                              config_.timeout_us, config_.max_instructions);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();

    if (err != UC_ERR_OK) {
        // Get current RIP for debugging
        uint64_t rip = 0;
        uc_reg_read(uc, UC_X86_REG_RIP, &rip);

        std::string error = std::string("Emulation error at 0x") +
                           std::to_string(rip) + ": " + uc_strerror(err);
        spdlog::error("{}", error);
        return BuildResult(false, error);
    }

    spdlog::debug("Emulation completed in {} us", duration_us);
    return BuildResult(true);
}

EmulationResult Emulator::RunInstructions(uint64_t start_address, size_t count) {
    if (!uc_) {
        return BuildResult(false, "Emulator not initialized");
    }

    uc_engine* uc = static_cast<uc_engine*>(uc_);
    accessed_pages_.clear();

    // Map the starting page
    if (!MapPage(AlignToPage(start_address))) {
        return BuildResult(false, "Failed to map starting page");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Run for specific number of instructions
    // Use 0 as end address to run indefinitely (limited by count)
    uc_err err = uc_emu_start(uc, start_address, 0,
                              config_.timeout_us, count);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();

    if (err != UC_ERR_OK) {
        uint64_t rip = 0;
        uc_reg_read(uc, UC_X86_REG_RIP, &rip);

        std::string error = std::string("Emulation error at 0x") +
                           std::to_string(rip) + ": " + uc_strerror(err);
        spdlog::error("{}", error);
        return BuildResult(false, error);
    }

    spdlog::debug("Executed {} instructions in {} us", count, duration_us);
    return BuildResult(true);
}

EmulationResult Emulator::BuildResult(bool success, const std::string& error) {
    EmulationResult result;
    result.success = success;
    result.error = error;

    if (uc_) {
        uc_engine* uc = static_cast<uc_engine*>(uc_);

        // Read all general-purpose registers
        const std::vector<std::pair<std::string, int>> gp_regs = {
            {"rax", UC_X86_REG_RAX}, {"rbx", UC_X86_REG_RBX},
            {"rcx", UC_X86_REG_RCX}, {"rdx", UC_X86_REG_RDX},
            {"rsi", UC_X86_REG_RSI}, {"rdi", UC_X86_REG_RDI},
            {"rbp", UC_X86_REG_RBP}, {"rsp", UC_X86_REG_RSP},
            {"r8",  UC_X86_REG_R8},  {"r9",  UC_X86_REG_R9},
            {"r10", UC_X86_REG_R10}, {"r11", UC_X86_REG_R11},
            {"r12", UC_X86_REG_R12}, {"r13", UC_X86_REG_R13},
            {"r14", UC_X86_REG_R14}, {"r15", UC_X86_REG_R15},
            {"rip", UC_X86_REG_RIP}, {"rflags", UC_X86_REG_RFLAGS}
        };

        for (const auto& [name, uc_reg] : gp_regs) {
            uint64_t value = 0;
            uc_reg_read(uc, uc_reg, &value);
            result.registers[name] = value;
        }

        result.final_rip = result.registers["rip"];

        // Read XMM registers
        for (int i = 0; i < 16; i++) {
            uint8_t xmm_data[16];
            uc_reg_read(uc, UC_X86_REG_XMM0 + i, xmm_data);

            XMMValue xmm;
            memcpy(&xmm.lo, xmm_data, 8);
            memcpy(&xmm.hi, xmm_data + 8, 8);
            result.xmm_registers["xmm" + std::to_string(i)] = xmm;
        }
    }

    return result;
}

void Emulator::ResetCPU() {
    if (!uc_) return;

    uc_engine* uc = static_cast<uc_engine*>(uc_);

    // Zero all general-purpose registers
    uint64_t zero = 0;
    uc_reg_write(uc, UC_X86_REG_RAX, &zero);
    uc_reg_write(uc, UC_X86_REG_RBX, &zero);
    uc_reg_write(uc, UC_X86_REG_RCX, &zero);
    uc_reg_write(uc, UC_X86_REG_RDX, &zero);
    uc_reg_write(uc, UC_X86_REG_RSI, &zero);
    uc_reg_write(uc, UC_X86_REG_RDI, &zero);
    uc_reg_write(uc, UC_X86_REG_R8, &zero);
    uc_reg_write(uc, UC_X86_REG_R9, &zero);
    uc_reg_write(uc, UC_X86_REG_R10, &zero);
    uc_reg_write(uc, UC_X86_REG_R11, &zero);
    uc_reg_write(uc, UC_X86_REG_R12, &zero);
    uc_reg_write(uc, UC_X86_REG_R13, &zero);
    uc_reg_write(uc, UC_X86_REG_R14, &zero);
    uc_reg_write(uc, UC_X86_REG_R15, &zero);
    uc_reg_write(uc, UC_X86_REG_RIP, &zero);

    // Reset RSP/RBP to middle of stack
    uint64_t rsp = config_.stack_base + config_.stack_size / 2;
    uc_reg_write(uc, UC_X86_REG_RSP, &rsp);
    uc_reg_write(uc, UC_X86_REG_RBP, &rsp);

    // Reset flags
    uint64_t rflags = 0x202;  // Standard initial flags (IF set)
    uc_reg_write(uc, UC_X86_REG_RFLAGS, &rflags);

    // Zero XMM registers
    uint8_t xmm_zero[16] = {0};
    for (int i = 0; i < 16; i++) {
        uc_reg_write(uc, UC_X86_REG_XMM0 + i, xmm_zero);
    }

    accessed_pages_.clear();
}

void Emulator::Reset() {
    if (uc_) {
        uc_engine* uc = static_cast<uc_engine*>(uc_);

        // Remove hooks
        if (mem_unmapped_hook_) {
            uc_hook_del(uc, mem_unmapped_hook_);
            mem_unmapped_hook_ = 0;
        }
        if (mem_access_hook_) {
            uc_hook_del(uc, mem_access_hook_);
            mem_access_hook_ = 0;
        }

        uc_close(uc);
        uc_ = nullptr;
    }

    mapped_pages_.clear();
    accessed_pages_.clear();
    last_error_.clear();
}

// Static callback for unmapped memory access
bool Emulator::OnMemoryUnmapped(void* uc, int type, uint64_t address,
                                 int size, int64_t value, void* user_data) {
    (void)uc;
    (void)type;
    (void)size;
    (void)value;

    Emulator* emu = static_cast<Emulator*>(user_data);
    if (!emu) return false;

    // Try to lazily map the page
    uint64_t page = AlignToPage(address);
    spdlog::debug("Lazy-mapping page 0x{:016X} (access to 0x{:016X})", page, address);

    return emu->MapPage(page);
}

// Static callback for memory access tracking
void Emulator::OnMemoryAccess(void* uc, int type, uint64_t address,
                               int size, int64_t value, void* user_data) {
    (void)uc;
    (void)type;
    (void)size;
    (void)value;

    Emulator* emu = static_cast<Emulator*>(user_data);
    if (!emu) return;

    // Track accessed pages
    uint64_t page = AlignToPage(address);
    emu->accessed_pages_.insert(page);
}

// Helper functions
std::optional<Reg> ParseRegister(const std::string& name) {
    static const std::unordered_map<std::string, Reg> reg_map = {
        {"rax", Reg::RAX}, {"rbx", Reg::RBX}, {"rcx", Reg::RCX}, {"rdx", Reg::RDX},
        {"rsi", Reg::RSI}, {"rdi", Reg::RDI}, {"rbp", Reg::RBP}, {"rsp", Reg::RSP},
        {"r8", Reg::R8}, {"r9", Reg::R9}, {"r10", Reg::R10}, {"r11", Reg::R11},
        {"r12", Reg::R12}, {"r13", Reg::R13}, {"r14", Reg::R14}, {"r15", Reg::R15},
        {"rip", Reg::RIP}, {"rflags", Reg::RFLAGS},
        {"xmm0", Reg::XMM0}, {"xmm1", Reg::XMM1}, {"xmm2", Reg::XMM2}, {"xmm3", Reg::XMM3},
        {"xmm4", Reg::XMM4}, {"xmm5", Reg::XMM5}, {"xmm6", Reg::XMM6}, {"xmm7", Reg::XMM7},
        {"xmm8", Reg::XMM8}, {"xmm9", Reg::XMM9}, {"xmm10", Reg::XMM10}, {"xmm11", Reg::XMM11},
        {"xmm12", Reg::XMM12}, {"xmm13", Reg::XMM13}, {"xmm14", Reg::XMM14}, {"xmm15", Reg::XMM15}
    };

    // Convert to lowercase
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto it = reg_map.find(lower);
    if (it != reg_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string RegisterName(Reg reg) {
    switch (reg) {
        case Reg::RAX: return "rax";
        case Reg::RBX: return "rbx";
        case Reg::RCX: return "rcx";
        case Reg::RDX: return "rdx";
        case Reg::RSI: return "rsi";
        case Reg::RDI: return "rdi";
        case Reg::RBP: return "rbp";
        case Reg::RSP: return "rsp";
        case Reg::R8: return "r8";
        case Reg::R9: return "r9";
        case Reg::R10: return "r10";
        case Reg::R11: return "r11";
        case Reg::R12: return "r12";
        case Reg::R13: return "r13";
        case Reg::R14: return "r14";
        case Reg::R15: return "r15";
        case Reg::RIP: return "rip";
        case Reg::RFLAGS: return "rflags";
        case Reg::XMM0: return "xmm0";
        case Reg::XMM1: return "xmm1";
        case Reg::XMM2: return "xmm2";
        case Reg::XMM3: return "xmm3";
        case Reg::XMM4: return "xmm4";
        case Reg::XMM5: return "xmm5";
        case Reg::XMM6: return "xmm6";
        case Reg::XMM7: return "xmm7";
        case Reg::XMM8: return "xmm8";
        case Reg::XMM9: return "xmm9";
        case Reg::XMM10: return "xmm10";
        case Reg::XMM11: return "xmm11";
        case Reg::XMM12: return "xmm12";
        case Reg::XMM13: return "xmm13";
        case Reg::XMM14: return "xmm14";
        case Reg::XMM15: return "xmm15";
        default: return "unknown";
    }
}

} // namespace emulation
} // namespace orpheus
