#include "utils/search_history.h"
#include "core/runtime_manager.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace orpheus {

using json = nlohmann::json;

const std::vector<std::string> SearchHistory::empty_;

void SearchHistory::Add(const std::string& category, const std::string& query) {
    if (query.empty()) return;

    auto& vec = entries_[category];

    // Remove existing duplicate
    vec.erase(std::remove(vec.begin(), vec.end(), query), vec.end());

    // Insert at front (MRU)
    vec.insert(vec.begin(), query);

    // Trim to max
    if (vec.size() > MAX_PER_CATEGORY) {
        vec.resize(MAX_PER_CATEGORY);
    }

    dirty_ = true;
}

const std::vector<std::string>& SearchHistory::Get(const std::string& category) const {
    auto it = entries_.find(category);
    if (it != entries_.end()) return it->second;
    return empty_;
}

void SearchHistory::Clear(const std::string& category) {
    entries_.erase(category);
    dirty_ = true;
}

void SearchHistory::ClearAll() {
    entries_.clear();
    dirty_ = true;
}

std::string SearchHistory::GetDefaultFilepath() {
    auto config_dir = RuntimeManager::Instance().GetConfigDirectory();
    return (config_dir / "search_history.json").string();
}

bool SearchHistory::Save(const std::string& filepath) {
    std::string path = filepath.empty() ? GetDefaultFilepath() : filepath;

    try {
        auto dir = std::filesystem::path(path).parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }

        json j = json::object();
        for (const auto& [category, queries] : entries_) {
            j[category] = queries;
        }

        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << j.dump(2);
        file.close();

        dirty_ = false;
        LOG_INFO("Search history saved to {}", path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save search history: {}", e.what());
        return false;
    }
}

bool SearchHistory::Load(const std::string& filepath) {
    std::string path = filepath.empty() ? GetDefaultFilepath() : filepath;

    try {
        if (!std::filesystem::exists(path)) return false;

        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j;
        file >> j;
        file.close();

        entries_.clear();
        for (auto& [key, value] : j.items()) {
            if (value.is_array()) {
                std::vector<std::string> queries;
                for (const auto& item : value) {
                    if (item.is_string()) {
                        queries.push_back(item.get<std::string>());
                    }
                }
                if (!queries.empty()) {
                    entries_[key] = std::move(queries);
                }
            }
        }

        dirty_ = false;
        LOG_INFO("Search history loaded from {} ({} categories)", path, entries_.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load search history: {}", e.what());
        return false;
    }
}

} // namespace orpheus
