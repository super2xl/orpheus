#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace orpheus {

/**
 * Bookmark - Saved address with label and notes
 */
struct Bookmark {
    uint64_t address = 0;
    std::string label;
    std::string notes;
    std::string category;  // Optional grouping (e.g., "Entity", "Player", "Weapons")
    std::string module;    // Module name for context
    int64_t created_at = 0;  // Unix timestamp

    // For UI selection
    bool operator==(const Bookmark& other) const {
        return address == other.address && label == other.label;
    }
};

/**
 * BookmarkManager - Handles bookmark storage and persistence
 */
class BookmarkManager {
public:
    BookmarkManager() = default;

    // Add a new bookmark (returns index)
    size_t Add(const Bookmark& bookmark);

    // Add with convenience params
    size_t Add(uint64_t address, const std::string& label,
               const std::string& notes = "", const std::string& category = "",
               const std::string& module = "");

    // Remove bookmark by index
    bool Remove(size_t index);

    // Remove bookmark by address
    bool RemoveByAddress(uint64_t address);

    // Update bookmark at index
    bool Update(size_t index, const Bookmark& bookmark);

    // Get all bookmarks
    const std::vector<Bookmark>& GetAll() const { return bookmarks_; }

    // Get bookmarks by category
    std::vector<Bookmark> GetByCategory(const std::string& category) const;

    // Find bookmark by address (returns nullptr if not found)
    const Bookmark* FindByAddress(uint64_t address) const;

    // Get unique categories
    std::vector<std::string> GetCategories() const;

    // Check if address is bookmarked
    bool IsBookmarked(uint64_t address) const;

    // Clear all bookmarks
    void Clear();

    // Persistence
    bool Save(const std::string& filepath = "");
    bool Load(const std::string& filepath = "");

    // Get default filepath
    static std::string GetDefaultFilepath();

    // Mark as dirty (needs save)
    bool IsDirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

    size_t Count() const { return bookmarks_.size(); }

private:
    std::vector<Bookmark> bookmarks_;
    bool dirty_ = false;
    std::string last_filepath_;
};

} // namespace orpheus
