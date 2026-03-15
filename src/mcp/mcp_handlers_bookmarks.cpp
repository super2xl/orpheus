/**
 * MCP Handlers - Bookmarks
 *
 * User annotation handlers:
 * - HandleBookmarkList
 * - HandleBookmarkAdd
 * - HandleBookmarkRemove
 * - HandleBookmarkUpdate
 */

#include "mcp_server.h"
#include "core/orpheus_core.h"
#include "utils/bookmarks.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleBookmarkList(const std::string&) {
    try {
        auto* bookmarks = core_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        const auto& all = bookmarks->GetAll();

        json result;
        result["count"] = all.size();

        json bookmarks_array = json::array();
        for (size_t i = 0; i < all.size(); i++) {
            const auto& bm = all[i];
            json entry;
            entry["index"] = i;
            entry["address"] = FormatAddress(bm.address);
            entry["label"] = bm.label;
            entry["notes"] = bm.notes;
            entry["category"] = bm.category;
            entry["module"] = bm.module;
            entry["created_at"] = bm.created_at;
            bookmarks_array.push_back(entry);
        }
        result["bookmarks"] = bookmarks_array;

        // Include categories for convenience
        json categories = json::array();
        for (const auto& cat : bookmarks->GetCategories()) {
            categories.push_back(cat);
        }
        result["categories"] = categories;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleBookmarkAdd(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* bookmarks = core_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        // Parse address (required)
        if (!req.contains("address")) {
            return CreateErrorResponse("Missing required parameter: address");
        }
        uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);

        // Parse optional fields
        std::string label = req.value("label", "");
        std::string notes = req.value("notes", "");
        std::string category = req.value("category", "");
        std::string module = req.value("module", "");

        // Check if already bookmarked
        if (bookmarks->IsBookmarked(address)) {
            return CreateErrorResponse("Address already bookmarked: " + FormatAddress(address));
        }

        size_t index = bookmarks->Add(address, label, notes, category, module);

        json result;
        result["index"] = index;
        result["address"] = FormatAddress(address);
        result["label"] = label;
        result["total_bookmarks"] = bookmarks->Count();

        LOG_INFO("MCP: Added bookmark '{}' at {}", label, FormatAddress(address));

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleBookmarkRemove(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* bookmarks = core_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        bool removed = false;
        std::string removed_info;

        // Can remove by index or by address
        if (req.contains("index")) {
            size_t index = req["index"];
            if (index >= bookmarks->Count()) {
                return CreateErrorResponse("Invalid bookmark index: " + std::to_string(index));
            }
            const auto& bm = bookmarks->GetAll()[index];
            removed_info = FormatAddress(bm.address) + " (" + bm.label + ")";
            removed = bookmarks->Remove(index);
        } else if (req.contains("address")) {
            uint64_t address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
            const auto* bm = bookmarks->FindByAddress(address);
            if (bm) {
                removed_info = FormatAddress(address) + " (" + bm->label + ")";
            }
            removed = bookmarks->RemoveByAddress(address);
        } else {
            return CreateErrorResponse("Missing parameter: provide 'index' or 'address'");
        }

        if (!removed) {
            return CreateErrorResponse("Bookmark not found");
        }

        json result;
        result["removed"] = removed_info;
        result["remaining"] = bookmarks->Count();

        LOG_INFO("MCP: Removed bookmark {}", removed_info);

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleBookmarkUpdate(const std::string& body) {
    try {
        auto req = json::parse(body);

        auto* bookmarks = core_->GetBookmarks();
        if (!bookmarks) {
            return CreateErrorResponse("Bookmark manager not initialized");
        }

        // Require index to update
        if (!req.contains("index")) {
            return CreateErrorResponse("Missing required parameter: index");
        }

        size_t index = req["index"];
        if (index >= bookmarks->Count()) {
            return CreateErrorResponse("Invalid bookmark index: " + std::to_string(index));
        }

        // Get existing bookmark
        Bookmark bm = bookmarks->GetAll()[index];

        // Update fields if provided
        if (req.contains("address")) {
            bm.address = std::stoull(req["address"].get<std::string>(), nullptr, 16);
        }
        if (req.contains("label")) {
            bm.label = req["label"].get<std::string>();
        }
        if (req.contains("notes")) {
            bm.notes = req["notes"].get<std::string>();
        }
        if (req.contains("category")) {
            bm.category = req["category"].get<std::string>();
        }
        if (req.contains("module")) {
            bm.module = req["module"].get<std::string>();
        }

        if (!bookmarks->Update(index, bm)) {
            return CreateErrorResponse("Failed to update bookmark");
        }

        json result;
        result["index"] = index;
        result["address"] = FormatAddress(bm.address);
        result["label"] = bm.label;
        result["notes"] = bm.notes;
        result["category"] = bm.category;
        result["module"] = bm.module;

        LOG_INFO("MCP: Updated bookmark {} at {}", bm.label, FormatAddress(bm.address));

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
