#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <functional>
#include <cstring>

#include "memory_cache.h"

namespace orpheus {

// Opaque handle type for VMM - use void* to avoid header conflicts
using VMMHandle = void*;

/**
 * ProcessInfo - Basic process information
 */
struct ProcessInfo {
    uint32_t pid;
    uint32_t ppid;            // Parent PID
    std::string name;
    std::string path;
    uint64_t base_address;
    uint64_t peb_address;
    uint64_t dtb;             // Directory Table Base
    bool is_64bit;
    bool is_wow64;            // 32-bit on 64-bit
    uint32_t state;           // Process state
};

/**
 * ModuleInfo - Module/DLL information
 */
struct ModuleInfo {
    std::string name;
    std::string path;
    uint64_t base_address;
    uint64_t entry_point;
    uint32_t size;
    bool is_64bit;
};

/**
 * MemoryRegion - Memory region information
 */
struct MemoryRegion {
    uint64_t base_address;
    uint64_t size;
    std::string protection;
    std::string type;
    std::string info;
};

/**
 * ScatterRequest - For batch memory reads
 */
struct ScatterRequest {
    uint64_t address;
    uint32_t size;
    std::vector<uint8_t> data;
    bool success;
};

/**
 * DMAInterface - Main interface to MemProcFS/LeechCore
 *
 * This class provides a C++ wrapper around the vmm.dll C API.
 * It handles DMA initialization, process enumeration, and memory operations.
 */
class DMAInterface {
public:
    DMAInterface();
    ~DMAInterface();

    // Delete copy operations
    DMAInterface(const DMAInterface&) = delete;
    DMAInterface& operator=(const DMAInterface&) = delete;

    // Allow move operations
    DMAInterface(DMAInterface&&) noexcept;
    DMAInterface& operator=(DMAInterface&&) noexcept;

    /**
     * Initialize DMA connection
     * @param device Device string (e.g., "fpga" for FPGA DMA, "file://dump.raw" for file)
     * @return true on success
     */
    bool Initialize(const std::string& device = "fpga");

    /**
     * Close DMA connection
     */
    void Close();

    /**
     * Check if connected
     */
    [[nodiscard]] bool IsConnected() const { return vmm_handle_ != nullptr; }

    /**
     * Get the device type string (e.g., "fpga", "file://...")
     */
    [[nodiscard]] const std::string& GetDeviceType() const { return device_type_; }

    // =========================================================================
    // Process Operations
    // =========================================================================

    /**
     * Get list of all processes
     */
    [[nodiscard]] std::vector<ProcessInfo> GetProcessList();

    /**
     * Get process info by PID
     */
    [[nodiscard]] std::optional<ProcessInfo> GetProcessInfo(uint32_t pid);

    /**
     * Get process info by name (first match)
     */
    [[nodiscard]] std::optional<ProcessInfo> GetProcessByName(const std::string& name);

    /**
     * Get list of modules for a process
     */
    [[nodiscard]] std::vector<ModuleInfo> GetModuleList(uint32_t pid);

    /**
     * Get module info by name
     */
    [[nodiscard]] std::optional<ModuleInfo> GetModuleByName(uint32_t pid,
                                                             const std::string& name);

    /**
     * Get memory regions for a process
     */
    [[nodiscard]] std::vector<MemoryRegion> GetMemoryRegions(uint32_t pid);

    // =========================================================================
    // Memory Operations
    // =========================================================================

    /**
     * Read memory from process
     * @param pid Process ID
     * @param address Virtual address to read
     * @param size Number of bytes to read
     * @return Vector of bytes, empty on failure
     */
    [[nodiscard]] std::vector<uint8_t> ReadMemory(uint32_t pid,
                                                   uint64_t address,
                                                   size_t size);

    /**
     * Read memory with template type
     */
    template<typename T>
    [[nodiscard]] std::optional<T> Read(uint32_t pid, uint64_t address) {
        auto data = ReadMemory(pid, address, sizeof(T));
        if (data.size() == sizeof(T)) {
            T value;
            std::memcpy(&value, data.data(), sizeof(T));
            return value;
        }
        return std::nullopt;
    }

    /**
     * Write memory to process
     * @param pid Process ID
     * @param address Virtual address to write
     * @param data Data to write
     * @return true on success
     */
    bool WriteMemory(uint32_t pid, uint64_t address, const std::vector<uint8_t>& data);

    /**
     * Write memory with template type
     */
    template<typename T>
    bool Write(uint32_t pid, uint64_t address, const T& value) {
        std::vector<uint8_t> data(sizeof(T));
        std::memcpy(data.data(), &value, sizeof(T));
        return WriteMemory(pid, address, data);
    }

    /**
     * Scatter-gather read (batch read multiple regions efficiently)
     * @param pid Process ID
     * @param requests Vector of scatter requests (will be filled with data)
     * @return Number of successful reads
     */
    size_t ScatterRead(uint32_t pid, std::vector<ScatterRequest>& requests);

    /**
     * Read null-terminated string
     */
    [[nodiscard]] std::string ReadString(uint32_t pid, uint64_t address,
                                          size_t max_length = 256);

    /**
     * Read wide string (UTF-16)
     */
    [[nodiscard]] std::wstring ReadWideString(uint32_t pid, uint64_t address,
                                               size_t max_length = 256);

    // =========================================================================
    // Virtual Address Translation
    // =========================================================================

    /**
     * Translate virtual address to physical address
     */
    [[nodiscard]] std::optional<uint64_t> VirtualToPhysical(uint32_t pid,
                                                             uint64_t virtual_addr);

    /**
     * Read physical memory directly
     */
    [[nodiscard]] std::vector<uint8_t> ReadPhysical(uint64_t physical_addr, size_t size);

    // =========================================================================
    // Utility
    // =========================================================================

    /**
     * Get last error message
     */
    [[nodiscard]] const std::string& GetLastError() const { return last_error_; }

    /**
     * Get VMM handle for direct API access
     */
    [[nodiscard]] VMMHandle GetVMMHandle() const { return vmm_handle_; }

    /**
     * Set error callback
     */
    void SetErrorCallback(std::function<void(const std::string&)> callback) {
        error_callback_ = std::move(callback);
    }

    // =========================================================================
    // Memory Cache
    // =========================================================================

    /**
     * Enable/disable read caching (reduces DMA reads by ~50-80%)
     */
    void SetCacheEnabled(bool enabled) { cache_.SetEnabled(enabled); }
    [[nodiscard]] bool IsCacheEnabled() const { return cache_.IsEnabled(); }

    /**
     * Configure cache parameters
     */
    void SetCacheConfig(const MemoryCache::Config& config) { cache_.SetConfig(config); }
    [[nodiscard]] MemoryCache::Config GetCacheConfig() const { return cache_.GetConfig(); }

    /**
     * Get cache statistics (hits, misses, hit rate)
     */
    [[nodiscard]] MemoryCache::Stats GetCacheStats() const { return cache_.GetStats(); }

    /**
     * Clear cache
     */
    void ClearCache() { cache_.Clear(); }

    /**
     * Invalidate cache for specific address range
     */
    void InvalidateCache(uint32_t pid, uint64_t address, size_t size) {
        cache_.Invalidate(pid, address, size);
    }

private:
    void ReportError(const std::string& message);

    // Uncached read (bypasses cache)
    std::vector<uint8_t> ReadMemoryDirect(uint32_t pid, uint64_t address, size_t size);

    VMMHandle vmm_handle_ = nullptr;
    std::string device_type_;
    std::string last_error_;
    std::function<void(const std::string&)> error_callback_;
    mutable MemoryCache cache_;
};

} // namespace orpheus
