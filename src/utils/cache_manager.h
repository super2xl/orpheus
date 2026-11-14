#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <optional>
#include <functional>
#include "logger.h"
#include "string_utils.h"
#include "../core/runtime_manager.h"

namespace orpheus::utils {

/**
 * CacheEntry represents a cached item found on disk
 */
struct CacheEntry {
    std::string name;           // Extracted from filename (before _size.json)
    uint32_t size;              // Module/scope size used for versioning
    std::string filepath;       // Full path to cache file
    std::string cached_at;      // Timestamp if available
    size_t item_count;          // Number of items in cache (classes, etc.)
};

/**
 * Generic cache manager for JSON-based caches.
 * Used by the RTTI cache, the function-recovery cache, and any future cache systems.
 *
 * Template parameter T is not used directly but allows type-safe factory creation.
 */
class CacheManager {
public:
    /**
     * Create a cache manager for a specific subdirectory.
     * @param subdirectory Name of subdirectory under AppData/cache/ (e.g., "rtti", "functions")
     * @param log_prefix Prefix for log messages (e.g., "RTTI", "Function recovery")
     */
    explicit CacheManager(const std::string& subdirectory, const std::string& log_prefix = "Cache")
        : subdirectory_(subdirectory)
        , log_prefix_(log_prefix)
    {}

    /**
     * Get the cache directory path, creating it if necessary.
     */
    std::string GetDirectory() const {
        namespace fs = std::filesystem;
        fs::path cache_dir = RuntimeManager::Instance().GetCacheDirectory() / subdirectory_;
        if (!fs::exists(cache_dir)) {
            fs::create_directories(cache_dir);
        }
        return cache_dir.string();
    }

    /**
     * Get the full file path for a cached item.
     * @param name Item name (module name, scope name, etc.)
     * @param size Size used for cache versioning (module size)
     */
    std::string GetFilePath(const std::string& name, uint32_t size) const {
        namespace fs = std::filesystem;
        std::string safe_name = string_utils::SanitizeFilename(name);
        std::stringstream ss;
        ss << safe_name << "_" << size << ".json";
        return (fs::path(GetDirectory()) / ss.str()).string();
    }

    /**
     * Save JSON data to cache.
     * @return true on success
     */
    bool Save(const std::string& name, uint32_t size, const std::string& json_data) {
        std::string filepath = GetFilePath(name, size);
        std::ofstream out(filepath);
        if (!out.is_open()) return false;
        out << json_data;
        out.close();
        LOG_INFO("{} cache saved: {}", log_prefix_, filepath);
        return true;
    }

    /**
     * Load JSON data from cache.
     * @return JSON string or empty string if not found
     */
    std::string Load(const std::string& name, uint32_t size) const {
        std::string filepath = GetFilePath(name, size);
        std::ifstream in(filepath);
        if (!in.is_open()) return "";
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    /**
     * Check if cache entry exists.
     */
    bool Exists(const std::string& name, uint32_t size) const {
        return std::filesystem::exists(GetFilePath(name, size));
    }

    /**
     * List all cached entries in this cache directory.
     * @param count_key JSON key to count items (e.g., "classes" for RTTI)
     */
    std::vector<CacheEntry> ListEntries(const std::string& count_key = "classes") const {
        namespace fs = std::filesystem;
        std::vector<CacheEntry> entries;
        std::string cache_dir = GetDirectory();

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().filename().string();
            size_t last_underscore = filename.rfind('_');
            if (last_underscore == std::string::npos) continue;

            CacheEntry ce;
            ce.name = filename.substr(0, last_underscore);
            std::string size_str = filename.substr(last_underscore + 1);
            size_str = size_str.substr(0, size_str.length() - 5); // remove .json

            try {
                ce.size = std::stoul(size_str);
            } catch (...) {
                continue;
            }

            ce.filepath = entry.path().string();

            // Read cache file to get item count and timestamp
            std::ifstream in(entry.path());
            if (in.is_open()) {
                try {
                    std::stringstream ss;
                    ss << in.rdbuf();
                    // Quick JSON parsing for metadata only
                    std::string content = ss.str();

                    // Extract item count (look for count_key array)
                    size_t key_pos = content.find("\"" + count_key + "\"");
                    if (key_pos != std::string::npos) {
                        // Find opening bracket and count items
                        size_t arr_start = content.find('[', key_pos);
                        if (arr_start != std::string::npos) {
                            size_t arr_end = content.find(']', arr_start);
                            if (arr_end != std::string::npos) {
                                // Count top-level objects in array
                                std::string arr_content = content.substr(arr_start + 1, arr_end - arr_start - 1);
                                size_t count = 0;
                                int brace_depth = 0;
                                for (char c : arr_content) {
                                    if (c == '{') {
                                        if (brace_depth == 0) count++;
                                        brace_depth++;
                                    } else if (c == '}') {
                                        brace_depth--;
                                    }
                                }
                                ce.item_count = count;
                            }
                        }
                    }

                    // Extract cached_at timestamp
                    size_t ts_pos = content.find("\"cached_at\"");
                    if (ts_pos != std::string::npos) {
                        size_t val_start = content.find(':', ts_pos);
                        if (val_start != std::string::npos) {
                            size_t quote_start = content.find('"', val_start);
                            size_t quote_end = content.find('"', quote_start + 1);
                            if (quote_start != std::string::npos && quote_end != std::string::npos) {
                                ce.cached_at = content.substr(quote_start + 1, quote_end - quote_start - 1);
                            }
                        }
                    }
                } catch (...) {
                    ce.item_count = 0;
                    ce.cached_at = "unknown";
                }
            }

            entries.push_back(ce);
        }

        return entries;
    }

    /**
     * Clear cache entries.
     * @param name If empty, clears all entries. Otherwise clears matching entries.
     * @return Number of entries cleared
     */
    size_t Clear(const std::string& name = "") const {
        namespace fs = std::filesystem;
        size_t cleared = 0;
        std::string cache_dir = GetDirectory();

        std::string name_lower = string_utils::ToLower(name);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            if (name.empty()) {
                fs::remove(entry.path());
                cleared++;
            } else {
                std::string filename = entry.path().filename().string();
                std::string filename_lower = string_utils::ToLower(filename);
                if (filename_lower.find(name_lower) != std::string::npos) {
                    fs::remove(entry.path());
                    cleared++;
                }
            }
        }

        if (cleared > 0) {
            LOG_INFO("{} cache cleared: {} entries", log_prefix_, cleared);
        }

        return cleared;
    }

    // Accessor for subdirectory name
    const std::string& GetSubdirectory() const { return subdirectory_; }
    const std::string& GetLogPrefix() const { return log_prefix_; }

    // Static utility functions (delegates to string_utils for backward compatibility)
    static std::string ToLower(const std::string& input) {
        return string_utils::ToLower(input);
    }

    static std::string SanitizeFilename(const std::string& name) {
        return string_utils::SanitizeFilename(name);
    }

private:
    std::string subdirectory_;
    std::string log_prefix_;
};

// Factory functions for common cache types
inline CacheManager CreateRTTICache() {
    return CacheManager("rtti", "RTTI");
}

} // namespace orpheus::utils
