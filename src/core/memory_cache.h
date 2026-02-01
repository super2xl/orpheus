#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <list>
#include <chrono>
#include <mutex>
#include <optional>

namespace orpheus {

/**
 * MemoryCache - Page-aligned LRU cache for DMA reads
 *
 * Reduces redundant DMA reads by caching recently accessed memory pages.
 * Thread-safe with mutex protection.
 *
 * Usage:
 *   MemoryCache cache;
 *   cache.SetEnabled(true);
 *
 *   // Check cache first
 *   if (auto cached = cache.Get(pid, address, size)) {
 *       return *cached;
 *   }
 *   // Cache miss - do actual DMA read
 *   auto data = dma->ReadMemory(pid, address, size);
 *   cache.Put(pid, address, data);
 */
class MemoryCache {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr size_t DEFAULT_MAX_PAGES = 1024;  // 4MB default
    static constexpr uint32_t DEFAULT_TTL_MS = 100;    // 100ms TTL

    struct Config {
        size_t max_pages = DEFAULT_MAX_PAGES;
        uint32_t ttl_ms = DEFAULT_TTL_MS;
        bool enabled = false;  // Disabled by default
    };

    struct Stats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t evictions = 0;
        size_t current_pages = 0;
        size_t current_bytes = 0;

        double HitRate() const {
            uint64_t total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    MemoryCache() = default;
    ~MemoryCache() = default;

    // Configuration
    void SetConfig(const Config& config);
    Config GetConfig() const;
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return config_.enabled; }
    void SetTTL(uint32_t ttl_ms) { config_.ttl_ms = ttl_ms; }
    void SetMaxPages(size_t max_pages) { config_.max_pages = max_pages; }

    /**
     * Try to get data from cache
     * @param pid Process ID
     * @param address Virtual address
     * @param size Number of bytes
     * @return Cached data if available and valid, nullopt otherwise
     */
    std::optional<std::vector<uint8_t>> Get(uint32_t pid, uint64_t address, size_t size);

    /**
     * Store data in cache
     * @param pid Process ID
     * @param address Virtual address (will be page-aligned)
     * @param data Data to cache
     */
    void Put(uint32_t pid, uint64_t address, const std::vector<uint8_t>& data);

    /**
     * Invalidate cache entries for an address range
     */
    void Invalidate(uint32_t pid, uint64_t address, size_t size);

    /**
     * Invalidate all entries for a process
     */
    void InvalidateProcess(uint32_t pid);

    /**
     * Clear entire cache
     */
    void Clear();

    /**
     * Get cache statistics
     */
    Stats GetStats() const;

    /**
     * Reset statistics
     */
    void ResetStats();

private:
    struct CacheKey {
        uint32_t pid;
        uint64_t page_address;  // Page-aligned address

        bool operator==(const CacheKey& other) const {
            return pid == other.pid && page_address == other.page_address;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey& key) const {
            // Combine pid and page_address into a single hash
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(key.pid) << 32) ^ (key.page_address >> 12)
            );
        }
    };

    struct CachePage {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
    };

    // Align address down to page boundary
    static uint64_t AlignToPage(uint64_t address) {
        return address & ~(PAGE_SIZE - 1);
    }

    // Check if a page is still valid (within TTL)
    bool IsPageValid(const CachePage& page) const;

    // Evict oldest pages until under limit
    void EvictIfNeeded();

    // Move key to front of LRU list
    void TouchLRU(const CacheKey& key);

    mutable std::mutex mutex_;
    Config config_;
    Stats stats_;

    // Cache storage: key -> page data
    std::unordered_map<CacheKey, CachePage, CacheKeyHash> pages_;

    // LRU tracking: front = most recent, back = least recent
    std::list<CacheKey> lru_list_;

    // Map from key to position in LRU list for O(1) updates
    std::unordered_map<CacheKey, std::list<CacheKey>::iterator, CacheKeyHash> lru_map_;
};

} // namespace orpheus
