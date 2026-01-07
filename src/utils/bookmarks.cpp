#include "bookmarks.h"
#include "logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <set>

using json = nlohmann::json;

namespace orpheus {

size_t BookmarkManager::Add(const Bookmark& bookmark) {
    bookmarks_.push_back(bookmark);

    // Set timestamp if not already set
    if (bookmarks_.back().created_at == 0) {
        bookmarks_.back().created_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    dirty_ = true;
    LOG_INFO("Added bookmark '{}' at 0x{:X}", bookmark.label, bookmark.address);
    return bookmarks_.size() - 1;
}

size_t BookmarkManager::Add(uint64_t address, const std::string& label,
                            const std::string& notes, const std::string& category,
                            const std::string& module) {
    Bookmark bm;
    bm.address = address;
    bm.label = label;
    bm.notes = notes;
    bm.category = category;
    bm.module = module;
    return Add(bm);
}

bool BookmarkManager::Remove(size_t index) {
    if (index >= bookmarks_.size()) {
        return false;
    }

    LOG_INFO("Removed bookmark '{}' at 0x{:X}",
             bookmarks_[index].label, bookmarks_[index].address);

    bookmarks_.erase(bookmarks_.begin() + index);
    dirty_ = true;
    return true;
}

bool BookmarkManager::RemoveByAddress(uint64_t address) {
    auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
        [address](const Bookmark& bm) { return bm.address == address; });

    if (it != bookmarks_.end()) {
        LOG_INFO("Removed bookmark '{}' at 0x{:X}", it->label, it->address);
        bookmarks_.erase(it);
        dirty_ = true;
        return true;
    }
    return false;
}

bool BookmarkManager::Update(size_t index, const Bookmark& bookmark) {
    if (index >= bookmarks_.size()) {
        return false;
    }

    bookmarks_[index] = bookmark;
    dirty_ = true;
    return true;
}

std::vector<Bookmark> BookmarkManager::GetByCategory(const std::string& category) const {
    std::vector<Bookmark> result;
    for (const auto& bm : bookmarks_) {
        if (bm.category == category) {
            result.push_back(bm);
        }
    }
    return result;
}

const Bookmark* BookmarkManager::FindByAddress(uint64_t address) const {
    auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
        [address](const Bookmark& bm) { return bm.address == address; });

    return (it != bookmarks_.end()) ? &(*it) : nullptr;
}

std::vector<std::string> BookmarkManager::GetCategories() const {
    std::set<std::string> unique_cats;
    for (const auto& bm : bookmarks_) {
        if (!bm.category.empty()) {
            unique_cats.insert(bm.category);
        }
    }
    return std::vector<std::string>(unique_cats.begin(), unique_cats.end());
}

bool BookmarkManager::IsBookmarked(uint64_t address) const {
    return FindByAddress(address) != nullptr;
}

void BookmarkManager::Clear() {
    bookmarks_.clear();
    dirty_ = true;
}

std::string BookmarkManager::GetDefaultFilepath() {
    return "orpheus_bookmarks.json";
}

bool BookmarkManager::Save(const std::string& filepath) {
    std::string path = filepath.empty() ? GetDefaultFilepath() : filepath;

    try {
        json j = json::array();

        for (const auto& bm : bookmarks_) {
            json entry;
            entry["address"] = bm.address;
            entry["label"] = bm.label;
            entry["notes"] = bm.notes;
            entry["category"] = bm.category;
            entry["module"] = bm.module;
            entry["created_at"] = bm.created_at;
            j.push_back(entry);
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open bookmarks file for writing: {}", path);
            return false;
        }

        file << j.dump(2);
        file.close();

        last_filepath_ = path;
        dirty_ = false;
        LOG_INFO("Saved {} bookmarks to {}", bookmarks_.size(), path);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save bookmarks: {}", e.what());
        return false;
    }
}

bool BookmarkManager::Load(const std::string& filepath) {
    std::string path = filepath.empty() ? GetDefaultFilepath() : filepath;

    if (!std::filesystem::exists(path)) {
        LOG_INFO("No bookmarks file found at {}", path);
        return false;
    }

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open bookmarks file: {}", path);
            return false;
        }

        json j = json::parse(file);
        file.close();

        bookmarks_.clear();

        for (const auto& entry : j) {
            Bookmark bm;
            bm.address = entry.value("address", 0ULL);
            bm.label = entry.value("label", "");
            bm.notes = entry.value("notes", "");
            bm.category = entry.value("category", "");
            bm.module = entry.value("module", "");
            bm.created_at = entry.value("created_at", 0LL);
            bookmarks_.push_back(bm);
        }

        last_filepath_ = path;
        dirty_ = false;
        LOG_INFO("Loaded {} bookmarks from {}", bookmarks_.size(), path);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load bookmarks: {}", e.what());
        return false;
    }
}

} // namespace orpheus
