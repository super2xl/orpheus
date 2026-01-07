#include "memory_watcher.h"
#include <algorithm>

namespace orpheus::analysis {

MemoryWatcher::MemoryWatcher(ReadMemoryFunc read_func)
    : read_memory_(std::move(read_func)) {
}

MemoryWatcher::~MemoryWatcher() {
    StopAutoScan();
}

uint32_t MemoryWatcher::AddWatch(uint64_t address, size_t size, WatchType type,
                                  const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    WatchRegion region;
    region.address = address;
    region.size = size;
    region.type = type;
    region.name = name.empty() ? ("Watch_" + std::to_string(next_watch_id_)) : name;
    region.enabled = true;
    region.change_count = 0;

    // Read initial value
    region.last_value = read_memory_(address, size);

    uint32_t id = next_watch_id_++;
    watches_[id] = std::move(region);
    frozen_[id] = false;

    return id;
}

bool MemoryWatcher::RemoveWatch(uint32_t watch_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_.erase(watch_id);
    return watches_.erase(watch_id) > 0;
}

void MemoryWatcher::SetWatchEnabled(uint32_t watch_id, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = watches_.find(watch_id);
    if (it != watches_.end()) {
        it->second.enabled = enabled;
    }
}

void MemoryWatcher::ClearAllWatches() {
    std::lock_guard<std::mutex> lock(mutex_);
    watches_.clear();
    frozen_.clear();
}

std::vector<WatchRegion> MemoryWatcher::GetWatches() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WatchRegion> result;
    result.reserve(watches_.size());
    for (const auto& [id, region] : watches_) {
        result.push_back(region);
    }
    return result;
}

void MemoryWatcher::RecordChange(uint32_t watch_id, const WatchRegion& region,
                                  const std::vector<uint8_t>& old_val,
                                  const std::vector<uint8_t>& new_val) {
    MemoryChange change;
    change.address = region.address;
    change.old_value = old_val;
    change.new_value = new_val;
    change.timestamp = std::chrono::system_clock::now();
    change.change_count = region.change_count;

    // Add to history
    if (change_history_.size() >= MAX_HISTORY) {
        change_history_.erase(change_history_.begin());
    }
    change_history_.push_back(change);
    total_changes_++;

    // Notify callback (without holding lock)
    if (change_callback_) {
        change_callback_(change);
    }
}

std::vector<MemoryChange> MemoryWatcher::Scan() {
    std::vector<MemoryChange> changes;

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, region] : watches_) {
        if (!region.enabled) continue;

        // Read current value
        auto current_value = read_memory_(region.address, region.size);
        if (current_value.empty()) continue;

        // Check for changes
        bool changed = false;
        if (current_value.size() == region.last_value.size()) {
            for (size_t i = 0; i < current_value.size(); i++) {
                if (current_value[i] != region.last_value[i]) {
                    changed = true;
                    break;
                }
            }
        } else {
            changed = true;
        }

        if (changed) {
            region.change_count++;

            MemoryChange change;
            change.address = region.address;
            change.old_value = region.last_value;
            change.new_value = current_value;
            change.timestamp = std::chrono::system_clock::now();
            change.change_count = region.change_count;

            changes.push_back(change);

            // Record to history
            if (change_history_.size() >= MAX_HISTORY) {
                change_history_.erase(change_history_.begin());
            }
            change_history_.push_back(change);
            total_changes_++;

            // Update last value
            region.last_value = std::move(current_value);

            // Call callback outside lock would be better, but for simplicity...
            if (change_callback_) {
                change_callback_(change);
            }
        }
    }

    return changes;
}

void MemoryWatcher::ScanThread() {
    while (scanning_.load()) {
        Scan();
        std::this_thread::sleep_for(std::chrono::milliseconds(scan_interval_ms_));
    }
}

void MemoryWatcher::StartAutoScan(uint32_t interval_ms) {
    if (scanning_.load()) {
        StopAutoScan();
    }

    scan_interval_ms_ = interval_ms;
    scanning_.store(true);
    scan_thread_ = std::thread(&MemoryWatcher::ScanThread, this);
}

void MemoryWatcher::StopAutoScan() {
    scanning_.store(false);
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
}

void MemoryWatcher::SetChangeCallback(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_callback_ = std::move(callback);
}

std::vector<MemoryChange> MemoryWatcher::GetRecentChanges(size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (change_history_.empty()) {
        return {};
    }

    size_t start = change_history_.size() > max_count ?
                   change_history_.size() - max_count : 0;

    return std::vector<MemoryChange>(
        change_history_.begin() + start,
        change_history_.end()
    );
}

void MemoryWatcher::ClearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    change_history_.clear();
    total_changes_ = 0;
}

void MemoryWatcher::SetFrozen(uint32_t watch_id, bool frozen) {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_[watch_id] = frozen;
}

} // namespace orpheus::analysis
