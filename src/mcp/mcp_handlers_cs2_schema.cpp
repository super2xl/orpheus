/**
 * MCP Handlers - CS2 Schema
 *
 * Counter-Strike 2 schema system handlers:
 * - GetModuleSizeForScope (helper)
 * - HandleCS2SchemaGetOffset
 * - HandleCS2SchemaFindClass
 * - HandleCS2SchemaCacheList
 * - HandleCS2SchemaCacheQuery
 * - HandleCS2SchemaCacheGet
 * - HandleCS2SchemaCacheClear
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "dumper/cs2_schema.h"
#include "utils/cache_manager.h"
#include "utils/string_utils.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace orpheus::mcp {

uint32_t MCPServer::GetModuleSizeForScope(const std::string& scope_name) {
    // Scope names are typically like "client.dll", "server.dll", etc.
    // We need to get the module size from DMA
    if (!cs2_schema_ || cs2_schema_pid_ == 0) return 0;

    auto* dma = app_->GetDMA();
    if (!dma || !dma->IsConnected()) return 0;

    // GlobalTypeScope is not a real module - use schemasystem.dll size as proxy
    // This ensures cache invalidates when schemasystem.dll updates
    std::string module_name = scope_name;
    if (scope_name == "GlobalTypeScope") {
        module_name = "schemasystem.dll";
    }

    auto mod = dma->GetModuleByName(cs2_schema_pid_, module_name);
    if (mod) {
        return mod->size;
    }
    return 0;
}

std::string MCPServer::HandleCS2SchemaGetOffset(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string class_name = req.value("class_name", "");
        std::string field_name = req.value("field_name", "");

        if (class_name.empty() || field_name.empty()) {
            return CreateErrorResponse("Missing required parameters: class_name, field_name");
        }

        // Try live dumper first
        if (cs2_schema_) {
            auto* dumper = cs2_schema_.get();
            if (dumper->IsInitialized()) {
                uint32_t offset = dumper->GetOffset(class_name, field_name);
                if (offset > 0) {
                    json result;
                    result["class"] = class_name;
                    result["field"] = field_name;
                    result["offset"] = offset;
                    std::stringstream ss;
                    ss << "0x" << std::hex << std::uppercase << offset;
                    result["offset_hex"] = ss.str();
                    return CreateSuccessResponse(result.dump());
                }
            }
        }

        // Search cache
        namespace fs = std::filesystem;
        std::string cache_dir = cs2_schema_cache_.GetDirectory();

        std::string class_lower = utils::CacheManager::ToLower(class_name);
        std::string field_lower = utils::CacheManager::ToLower(field_name);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            for (const auto& cls : cache_data["classes"]) {
                std::string cls_name = cls.value("name", "");
                std::string cls_name_lower = utils::string_utils::ToLower(cls_name);

                if (cls_name_lower != class_lower) continue;

                if (cls.contains("fields")) {
                    for (const auto& field : cls["fields"]) {
                        std::string fld_name = field.value("name", "");
                        std::string fld_name_lower = utils::string_utils::ToLower(fld_name);

                        if (fld_name_lower == field_lower) {
                            json result;
                            result["class"] = cls_name;
                            result["field"] = fld_name;
                            result["offset"] = field.value("offset", 0);
                            result["type"] = field.value("type", "");
                            result["size"] = field.value("size", 0);
                            std::stringstream ss;
                            ss << "0x" << std::hex << std::uppercase << field.value("offset", 0);
                            result["offset_hex"] = ss.str();
                            result["from_cache"] = true;
                            return CreateSuccessResponse(result.dump());
                        }
                    }
                }
            }
        }

        return CreateErrorResponse("Offset not found for " + class_name + "::" + field_name);

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaFindClass(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string class_name = req.value("class_name", "");
        if (class_name.empty()) {
            return CreateErrorResponse("Missing required parameter: class_name");
        }

        // Try live dumper first
        if (cs2_schema_) {
            auto* dumper = cs2_schema_.get();
            if (dumper->IsInitialized()) {
                const orpheus::dumper::SchemaClass* cls = dumper->FindClass(class_name);
                if (cls) {
                    json result;
                    result["name"] = cls->name;
                    result["module"] = cls->module;
                    result["size"] = cls->size;
                    result["base_class"] = cls->base_class;

                    json fields = json::array();
                    for (const auto& field : cls->fields) {
                        json f;
                        f["name"] = field.name;
                        f["type"] = field.type_name;
                        f["offset"] = field.offset;
                        std::stringstream ss;
                        ss << "0x" << std::hex << std::uppercase << field.offset;
                        f["offset_hex"] = ss.str();
                        f["size"] = field.size;
                        fields.push_back(f);
                    }
                    result["fields"] = fields;
                    result["field_count"] = cls->fields.size();
                    return CreateSuccessResponse(result.dump());
                }
            }
        }

        // Search cache
        namespace fs = std::filesystem;
        std::string cache_dir = cs2_schema_cache_.GetDirectory();

        std::string class_lower = utils::string_utils::ToLower(class_name);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            for (const auto& cls : cache_data["classes"]) {
                std::string cls_name = cls.value("name", "");
                std::string cls_name_lower = utils::string_utils::ToLower(cls_name);

                if (cls_name_lower == class_lower) {
                    json result;
                    result["name"] = cls_name;
                    result["module"] = cls.value("module", "");
                    result["size"] = cls.value("size", 0);
                    result["base_class"] = cls.value("base_class", "");

                    if (cls.contains("fields")) {
                        json fields = json::array();
                        for (const auto& field : cls["fields"]) {
                            json f;
                            f["name"] = field.value("name", "");
                            f["type"] = field.value("type", "");
                            f["offset"] = field.value("offset", 0);
                            std::stringstream ss;
                            ss << "0x" << std::hex << std::uppercase << field.value("offset", 0);
                            f["offset_hex"] = ss.str();
                            f["size"] = field.value("size", 0);
                            fields.push_back(f);
                        }
                        result["fields"] = fields;
                        result["field_count"] = fields.size();
                    }
                    result["from_cache"] = true;
                    return CreateSuccessResponse(result.dump());
                }
            }
        }

        return CreateErrorResponse("Class not found: " + class_name);

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheList(const std::string& body) {
    try {
        namespace fs = std::filesystem;
        std::string cache_dir = cs2_schema_cache_.GetDirectory();

        json result;
        json scopes = json::array();

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();

                // Parse scope name and module size from filename (format: name_size.json)
                std::string scope_name = filename;
                uint32_t module_size = 0;
                size_t last_underscore = filename.rfind('_');
                if (last_underscore != std::string::npos) {
                    scope_name = filename.substr(0, last_underscore);
                    std::string size_str = filename.substr(last_underscore + 1);
                    try {
                        module_size = std::stoul(size_str);
                    } catch (...) {
                        // Old format without size - keep the full filename as scope name
                        scope_name = filename;
                        module_size = 0;
                    }
                }

                std::ifstream in(entry.path());
                if (in.is_open()) {
                    json cache_data = json::parse(in);
                    json scope;
                    scope["scope"] = scope_name;
                    scope["module_size"] = module_size;
                    scope["classes"] = cache_data.contains("classes") ? cache_data["classes"].size() : 0;
                    scope["cached_at"] = cache_data.value("cached_at", "unknown");
                    scope["cache_file"] = entry.path().string();

                    // Count total fields
                    size_t field_count = 0;
                    if (cache_data.contains("classes")) {
                        for (const auto& cls : cache_data["classes"]) {
                            if (cls.contains("fields")) {
                                field_count += cls["fields"].size();
                            }
                        }
                    }
                    scope["fields"] = field_count;
                    scopes.push_back(scope);
                }
            }
        }

        result["count"] = scopes.size();
        result["scopes"] = scopes;
        result["cache_directory"] = cache_dir;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheQuery(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string query = req.value("query", "");
        std::string scope_filter = req.value("scope", "");
        int max_results = req.value("max_results", 100);

        if (query.empty()) {
            return CreateErrorResponse("Missing required parameter: query");
        }

        std::string query_lower = utils::string_utils::ToLower(query);

        namespace fs = std::filesystem;
        std::string cache_dir = cs2_schema_cache_.GetDirectory();

        json result;
        json matches = json::array();
        int total_searched = 0;

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().stem().string();

            // Parse scope name from filename (format: name_size.json)
            std::string scope_name = filename;
            size_t last_underscore = filename.rfind('_');
            if (last_underscore != std::string::npos) {
                scope_name = filename.substr(0, last_underscore);
            }

            // Filter by scope if specified
            if (!scope_filter.empty()) {
                std::string filter_lower = utils::string_utils::ToLower(scope_filter);
                std::string scope_lower = utils::string_utils::ToLower(scope_name);
                if (scope_lower.find(filter_lower) == std::string::npos) continue;
            }

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            for (const auto& cls : cache_data["classes"]) {
                total_searched++;

                std::string cls_name = cls.value("name", "");
                std::string cls_name_lower = utils::string_utils::ToLower(cls_name);

                if (cls_name_lower.find(query_lower) != std::string::npos) {
                    json match;
                    match["name"] = cls_name;
                    match["module"] = cls.value("module", "");
                    match["size"] = cls.value("size", 0);
                    match["base_class"] = cls.value("base_class", "");
                    match["scope"] = scope_name;
                    match["field_count"] = cls.contains("fields") ? cls["fields"].size() : 0;
                    matches.push_back(match);

                    if ((int)matches.size() >= max_results) break;
                }
            }

            if ((int)matches.size() >= max_results) break;
        }

        result["query"] = query;
        result["matches"] = matches;
        result["match_count"] = matches.size();
        result["total_searched"] = total_searched;
        result["truncated"] = (int)matches.size() >= max_results;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheGet(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string scope = req.value("scope", "");
        int max_results = req.value("max_results", 1000);

        if (scope.empty()) {
            return CreateErrorResponse("Missing required parameter: scope");
        }

        // Find cache file for this scope (filename format: scope_size.json)
        namespace fs = std::filesystem;
        std::string cache_dir = cs2_schema_cache_.GetDirectory();
        std::string cache_file;

        std::string scope_lower = utils::string_utils::ToLower(scope);

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().stem().string();

            // Parse scope name from filename (format: name_size.json)
            size_t last_underscore = filename.rfind('_');
            if (last_underscore != std::string::npos) {
                std::string file_scope = filename.substr(0, last_underscore);
                std::string file_scope_lower = utils::string_utils::ToLower(file_scope);

                if (file_scope_lower == scope_lower) {
                    cache_file = entry.path().string();
                    break;
                }
            }
        }

        if (cache_file.empty()) {
            return CreateErrorResponse("Cache not found for scope: " + scope);
        }

        std::ifstream in(cache_file);
        if (!in.is_open()) {
            return CreateErrorResponse("Failed to open cache file for scope: " + scope);
        }

        std::stringstream ss;
        ss << in.rdbuf();
        std::string cached = ss.str();

        json cache_data = json::parse(cached);
        json result;
        result["scope"] = scope;
        result["module_size"] = cache_data.value("module_size", 0);
        result["cached_at"] = cache_data.value("cached_at", "unknown");
        result["cache_file"] = cache_file;

        if (cache_data.contains("classes")) {
            json classes = json::array();
            int count = 0;
            for (const auto& cls : cache_data["classes"]) {
                classes.push_back(cls);
                count++;
                if (count >= max_results) break;
            }
            result["classes"] = classes;
            result["class_count"] = classes.size();
            result["total_classes"] = cache_data["classes"].size();
            result["truncated"] = count >= max_results && cache_data["classes"].size() > (size_t)max_results;
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleCS2SchemaCacheClear(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string scope = req.value("scope", "");
        namespace fs = std::filesystem;
        std::string cache_dir = cs2_schema_cache_.GetDirectory();

        int deleted = 0;

        if (scope.empty()) {
            // Clear all
            for (const auto& entry : fs::directory_iterator(cache_dir)) {
                if (entry.path().extension() == ".json") {
                    fs::remove(entry.path());
                    deleted++;
                }
            }
            LOG_INFO("CS2 schema cache cleared: {} files deleted", deleted);
        } else {
            // Clear specific scope - find all files matching scope prefix
            std::string scope_lower = utils::string_utils::ToLower(scope);

            for (const auto& entry : fs::directory_iterator(cache_dir)) {
                if (entry.path().extension() != ".json") continue;

                std::string filename = entry.path().stem().string();

                // Parse scope name from filename (format: name_size.json)
                size_t last_underscore = filename.rfind('_');
                if (last_underscore != std::string::npos) {
                    std::string file_scope = filename.substr(0, last_underscore);
                    std::string file_scope_lower = utils::string_utils::ToLower(file_scope);

                    if (file_scope_lower == scope_lower) {
                        fs::remove(entry.path());
                        deleted++;
                        LOG_INFO("CS2 schema cache cleared: {}", entry.path().string());
                    }
                }
            }

            if (deleted > 0) {
                LOG_INFO("CS2 schema cache cleared for scope {}: {} files deleted", scope, deleted);
            }
        }

        json result;
        result["deleted"] = deleted;
        result["scope"] = scope.empty() ? "all" : scope;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
