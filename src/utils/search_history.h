#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace orpheus {

/**
 * SearchHistory - Persists recent search queries per input category.
 * Categories: "pattern", "address", "string_filter", etc.
 * Each category stores up to MAX_PER_CATEGORY entries in MRU order.
 */
class SearchHistory {
public:
    static constexpr size_t MAX_PER_CATEGORY = 20;

    SearchHistory() = default;

    // Add a query to a category (moves to front if exists, trims to max)
    void Add(const std::string& category, const std::string& query);

    // Get recent queries for a category (most recent first)
    const std::vector<std::string>& Get(const std::string& category) const;

    // Clear a specific category or all
    void Clear(const std::string& category);
    void ClearAll();

    // Persistence
    bool Save(const std::string& filepath = "");
    bool Load(const std::string& filepath = "");
    static std::string GetDefaultFilepath();

    bool IsDirty() const { return dirty_; }

private:
    std::unordered_map<std::string, std::vector<std::string>> entries_;
    bool dirty_ = false;
    static const std::vector<std::string> empty_;
};

} // namespace orpheus
