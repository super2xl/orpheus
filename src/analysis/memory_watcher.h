#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace orpheus::analysis {

/**
 * WatchType - Type of memory watch
 */
enum class WatchType {
    Read,       // Log reads (requires hardware support)
    Write,      // Detect writes
    ReadWrite,  // Detect any access
    Value       // Watch for specific value change
};

/**
 * MemoryChange - Detected change in memory
 */
struct MemoryChange {
    uint64_t address;
    std::vector<uint8_t> old_value;
    std::vector<uint8_t> new_value;
    std::chrono::system_clock::time_point timestamp;
    uint32_t change_count;      // How many times this address changed
};

/**
 * WatchRegion - A monitored memory region
 */
struct WatchRegion {
    uint64_t address;
    size_t size;
    WatchType type;
    std::string name;
    bool enabled;

    // Internal state
    std::vector<uint8_t> last_value;
    uint32_t change_count;
};

/**
 * MemoryWatcher - Monitor memory regions for changes
 *
 * Features:
 * - Add watch regions to monitor
 * - Detect value changes between scans
 * - Log change history
 * - Callback notifications
 */
class MemoryWatcher {
public:
    using ReadMemoryFunc = std::function<std::vector<uint8_t>(uint64_t address, size_t size)>;
    using ChangeCallback = std::function<void(const MemoryChange& change)>;

    /**
     * Create watcher with memory read function
     */
    explicit MemoryWatcher(ReadMemoryFunc read_func);
    ~MemoryWatcher();

    // Disable copy
    MemoryWatcher(const MemoryWatcher&) = delete;
    MemoryWatcher& operator=(const MemoryWatcher&) = delete;

    /**
     * Add a watch region
     * @param address Start address
     * @param size Number of bytes to watch
     * @param type Watch type
     * @param name Optional name for the watch
     * @return Watch ID
     */
    uint32_t AddWatch(uint64_t address, size_t size, WatchType type = WatchType::Write,
                       const std::string& name = "");

    /**
     * Remove a watch region
     */
    bool RemoveWatch(uint32_t watch_id);

    /**
     * Enable/disable a watch
     */
    void SetWatchEnabled(uint32_t watch_id, bool enabled);

    /**
     * Clear all watches
     */
    void ClearAllWatches();

    /**
     * Get all watch regions
     */
    std::vector<WatchRegion> GetWatches() const;

    /**
     * Perform a single scan of all watched regions
     * @return Vector of detected changes
     */
    std::vector<MemoryChange> Scan();

    /**
     * Start automatic scanning in background thread
     * @param interval_ms Scan interval in milliseconds
     */
    void StartAutoScan(uint32_t interval_ms = 100);

    /**
     * Stop automatic scanning
     */
    void StopAutoScan();

    /**
     * Check if auto-scanning is active
     */
    bool IsScanning() const { return scanning_.load(); }

    /**
     * Set callback for change notifications
     */
    void SetChangeCallback(ChangeCallback callback);

    /**
     * Get recent changes
     */
    std::vector<MemoryChange> GetRecentChanges(size_t max_count = 100) const;

    /**
     * Clear change history
     */
    void ClearHistory();

    /**
     * Get total number of detected changes
     */
    size_t GetTotalChangeCount() const { return total_changes_; }

    /**
     * Freeze/unfreeze a memory value (prevent changes by writing back)
     */
    void SetFrozen(uint32_t watch_id, bool frozen);

private:
    void ScanThread();
    void RecordChange(uint32_t watch_id, const WatchRegion& region,
                      const std::vector<uint8_t>& old_val,
                      const std::vector<uint8_t>& new_val);

    ReadMemoryFunc read_memory_;
    ChangeCallback change_callback_;

    mutable std::mutex mutex_;
    std::map<uint32_t, WatchRegion> watches_;
    std::map<uint32_t, bool> frozen_;
    uint32_t next_watch_id_ = 1;

    std::vector<MemoryChange> change_history_;
    static constexpr size_t MAX_HISTORY = 10000;
    size_t total_changes_ = 0;

    std::atomic<bool> scanning_{false};
    std::thread scan_thread_;
    uint32_t scan_interval_ms_ = 100;
};

} // namespace orpheus::analysis
