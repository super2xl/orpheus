#include "memory_cache.h"
#include <algorithm>

namespace orpheus {

void MemoryCache::SetConfig(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    if (!config_.enabled) {
        // Clear cache when disabled
        pages_.clear();
        lru_list_.clear();
        lru_map_.clear();
    }
}

MemoryCache::Config MemoryCache::GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void MemoryCache::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.enabled = enabled;
    if (!enabled) {
        pages_.clear();
        lru_list_.clear();
        lru_map_.clear();
    }
}

bool MemoryCache::IsPageValid(const CachePage& page) const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - page.timestamp);
    return age.count() < static_cast<int64_t>(config_.ttl_ms);
}

void MemoryCache::EvictIfNeeded() {
    // Evict from back of LRU list (oldest) until under limit
    while (pages_.size() > config_.max_pages && !lru_list_.empty()) {
        const auto& oldest_key = lru_list_.back();
        pages_.erase(oldest_key);
        lru_map_.erase(oldest_key);
        lru_list_.pop_back();
        stats_.evictions++;
    }
}

void MemoryCache::TouchLRU(const CacheKey& key) {
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        // Move to front
        lru_list_.erase(it->second);
        lru_list_.push_front(key);
        it->second = lru_list_.begin();
    }
}

std::optional<std::vector<uint8_t>> MemoryCache::Get(uint32_t pid, uint64_t address, size_t size) {
    if (!config_.enabled || size == 0) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Calculate page range
    uint64_t start_page = AlignToPage(address);
    uint64_t end_page = AlignToPage(address + size - 1);
    size_t offset_in_first_page = address - start_page;

    // For simplicity, only handle single-page reads from cache
    // Multi-page reads will miss and be cached on put
    if (start_page != end_page) {
        stats_.misses++;
        return std::nullopt;
    }

    CacheKey key{pid, start_page};
    auto it = pages_.find(key);

    if (it == pages_.end()) {
        stats_.misses++;
        return std::nullopt;
    }

    // Check TTL
    if (!IsPageValid(it->second)) {
        // Expired - remove and return miss
        lru_map_.erase(key);
        auto lru_it = std::find(lru_list_.begin(), lru_list_.end(), key);
        if (lru_it != lru_list_.end()) {
            lru_list_.erase(lru_it);
        }
        pages_.erase(it);
        stats_.misses++;
        return std::nullopt;
    }

    // Cache hit!
    stats_.hits++;
    TouchLRU(key);

    // Extract requested range from cached page
    const auto& page_data = it->second.data;
    if (offset_in_first_page + size > page_data.size()) {
        // Requested range extends beyond cached data
        stats_.misses++;
        return std::nullopt;
    }

    std::vector<uint8_t> result(size);
    std::copy(page_data.begin() + offset_in_first_page,
              page_data.begin() + offset_in_first_page + size,
              result.begin());

    return result;
}

void MemoryCache::Put(uint32_t pid, uint64_t address, const std::vector<uint8_t>& data) {
    if (!config_.enabled || data.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Store data page by page
    uint64_t current_addr = address;
    size_t data_offset = 0;

    while (data_offset < data.size()) {
        uint64_t page_addr = AlignToPage(current_addr);
        size_t offset_in_page = current_addr - page_addr;
        size_t bytes_in_page = std::min(PAGE_SIZE - offset_in_page, data.size() - data_offset);

        CacheKey key{pid, page_addr};

        // Check if we already have this page
        auto it = pages_.find(key);
        if (it != pages_.end()) {
            // Update existing page
            auto& page = it->second;

            // Ensure page buffer is large enough
            if (page.data.size() < offset_in_page + bytes_in_page) {
                page.data.resize(offset_in_page + bytes_in_page);
            }

            // Copy new data into page
            std::copy(data.begin() + data_offset,
                      data.begin() + data_offset + bytes_in_page,
                      page.data.begin() + offset_in_page);

            page.timestamp = std::chrono::steady_clock::now();
            TouchLRU(key);
        } else {
            // New page
            EvictIfNeeded();

            CachePage page;
            page.data.resize(offset_in_page + bytes_in_page);
            std::copy(data.begin() + data_offset,
                      data.begin() + data_offset + bytes_in_page,
                      page.data.begin() + offset_in_page);
            page.timestamp = std::chrono::steady_clock::now();

            pages_[key] = std::move(page);

            // Add to LRU
            lru_list_.push_front(key);
            lru_map_[key] = lru_list_.begin();
        }

        current_addr += bytes_in_page;
        data_offset += bytes_in_page;
    }

    stats_.current_pages = pages_.size();
    stats_.current_bytes = pages_.size() * PAGE_SIZE;  // Approximate
}

void MemoryCache::Invalidate(uint32_t pid, uint64_t address, size_t size) {
    if (size == 0) return;

    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t start_page = AlignToPage(address);
    uint64_t end_page = AlignToPage(address + size - 1);

    for (uint64_t page = start_page; page <= end_page; page += PAGE_SIZE) {
        CacheKey key{pid, page};
        auto it = pages_.find(key);
        if (it != pages_.end()) {
            auto lru_it = lru_map_.find(key);
            if (lru_it != lru_map_.end()) {
                lru_list_.erase(lru_it->second);
                lru_map_.erase(lru_it);
            }
            pages_.erase(it);
        }
    }

    stats_.current_pages = pages_.size();
}

void MemoryCache::InvalidateProcess(uint32_t pid) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Find and remove all pages for this PID
    for (auto it = pages_.begin(); it != pages_.end(); ) {
        if (it->first.pid == pid) {
            auto lru_it = lru_map_.find(it->first);
            if (lru_it != lru_map_.end()) {
                lru_list_.erase(lru_it->second);
                lru_map_.erase(lru_it);
            }
            it = pages_.erase(it);
        } else {
            ++it;
        }
    }

    stats_.current_pages = pages_.size();
}

void MemoryCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pages_.clear();
    lru_list_.clear();
    lru_map_.clear();
    stats_.current_pages = 0;
    stats_.current_bytes = 0;
}

MemoryCache::Stats MemoryCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s = stats_;
    s.current_pages = pages_.size();
    s.current_bytes = pages_.size() * PAGE_SIZE;
    return s;
}

void MemoryCache::ResetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.hits = 0;
    stats_.misses = 0;
    stats_.evictions = 0;
}

} // namespace orpheus
