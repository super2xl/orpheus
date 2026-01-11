/**
 * MCP Handlers - RTTI
 *
 * Runtime Type Information handlers:
 * - HandleRTTIParseVTable
 * - HandleRTTIScan
 * - HandleRTTIScanModule
 * - HandleRTTICacheList
 * - HandleRTTICacheQuery
 * - HandleRTTICacheGet
 * - HandleRTTICacheClear
 */

#include "mcp_server.h"
#include "ui/application.h"
#include "core/dma_interface.h"
#include "analysis/rtti_parser.h"
#include "analysis/disassembler.h"
#include "utils/cache_manager.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <chrono>

using json = nlohmann::json;

namespace orpheus::mcp {

std::string MCPServer::HandleRTTIParseVTable(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t vtable_address = std::stoull(req["vtable_address"].get<std::string>(), nullptr, 16);
        uint64_t module_base = 0;
        if (req.contains("module_base")) {
            module_base = std::stoull(req["module_base"].get<std::string>(), nullptr, 16);
        }

        // Validate parameters
        if (vtable_address == 0) {
            return CreateErrorResponse("Invalid vtable_address: cannot parse NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // If module_base not provided, try to find the containing module
        if (module_base == 0) {
            auto modules = dma->GetModuleList(pid);
            for (const auto& mod : modules) {
                if (vtable_address >= mod.base_address && vtable_address < mod.base_address + mod.size) {
                    module_base = mod.base_address;
                    break;
                }
            }
            if (module_base == 0) {
                return CreateErrorResponse("Could not determine module base for vtable address. Please provide module_base parameter.");
            }
        }

        // Create RTTI parser with DMA read function
        analysis::RTTIParser parser(
            [dma, pid](uint64_t addr, size_t size) {
                return dma->ReadMemory(pid, addr, size);
            },
            module_base
        );

        auto info = parser.ParseVTable(vtable_address);
        if (!info) {
            return CreateErrorResponse("No valid RTTI found at vtable address " + FormatAddress(vtable_address) +
                                        " - address may not point to a vtable with RTTI");
        }

        json result;
        result["vtable_address"] = FormatAddress(info->vtable_address);
        result["col_address"] = FormatAddress(info->col_address);
        result["mangled_name"] = info->mangled_name;
        result["demangled_name"] = info->demangled_name;
        result["vftable_offset"] = info->vftable_offset;
        result["has_virtual_base"] = info->has_virtual_base;
        result["is_multiple_inheritance"] = info->is_multiple_inheritance;
        result["base_classes"] = info->base_classes;

        // ClassInformer-style enhanced output
        result["method_count"] = info->method_count;
        result["flags"] = info->GetFlags();           // "M", "V", "MV", or ""
        result["hierarchy"] = info->GetHierarchyString();  // "ClassName: Base1, Base2, ..."

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTIScan(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t base = std::stoull(req["base"].get<std::string>(), nullptr, 16);
        size_t size = req["size"];
        size_t max_results = req.value("max_results", 100);

        // Validate parameters
        if (base == 0) {
            return CreateErrorResponse("Invalid base address: cannot scan from NULL (0x0)");
        }
        if (size == 0) {
            return CreateErrorResponse("Invalid size: cannot scan 0 bytes");
        }
        if (size > 256 * 1024 * 1024) {  // 256MB limit for RTTI scan
            return CreateErrorResponse("Size too large: maximum RTTI scan region is 256MB");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // Create RTTI parser - pass 0 to auto-detect module base from COL self_rva
        analysis::RTTIParser parser(
            [dma, pid](uint64_t addr, size_t sz) {
                return dma->ReadMemory(pid, addr, sz);
            },
            0  // Auto-detect from COL's self_rva
        );

        // Scan for vtables
        std::vector<analysis::RTTIClassInfo> found_classes;
        parser.ScanForVTables(base, size, [&](const analysis::RTTIClassInfo& info) {
            if (found_classes.size() < max_results) {
                found_classes.push_back(info);
            }
        });

        json result;
        result["base"] = FormatAddress(base);
        result["size"] = size;
        result["count"] = found_classes.size();

        json classes_array = json::array();
        for (const auto& info : found_classes) {
            json cls;
            cls["vtable_address"] = FormatAddress(info.vtable_address);
            cls["method_count"] = info.method_count;
            cls["flags"] = info.GetFlags();
            cls["demangled_name"] = info.demangled_name;
            cls["hierarchy"] = info.GetHierarchyString();
            classes_array.push_back(cls);
        }
        result["classes"] = classes_array;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTIScanModule(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t module_base = std::stoull(req["module_base"].get<std::string>(), nullptr, 16);
        bool force_rescan = req.value("force_rescan", false);

        // Validate parameters
        if (module_base == 0) {
            return CreateErrorResponse("Invalid module_base: cannot scan from NULL (0x0)");
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // Find module info for name and size
        auto modules = dma->GetModuleList(pid);
        std::string module_name;
        uint32_t module_size = 0;
        for (const auto& mod : modules) {
            if (mod.base_address == module_base) {
                module_name = mod.name;
                module_size = mod.size;
                break;
            }
        }
        if (module_name.empty()) {
            module_name = "unknown";
            module_size = 0;
        }

        // Check cache first (unless force_rescan)
        if (!force_rescan && module_size > 0 && rtti_cache_.Exists(module_name, module_size)) {
            std::string cached = rtti_cache_.Load(module_name, module_size);
            if (!cached.empty()) {
                json cache_data = json::parse(cached);

                // Return summary only
                json result;
                result["status"] = "cached";
                result["module"] = module_name;
                result["module_base"] = FormatAddress(module_base);
                result["module_size"] = module_size;
                result["cache_file"] = rtti_cache_.GetFilePath(module_name, module_size);
                result["summary"] = cache_data.value("summary", json::object());
                result["count"] = cache_data.contains("classes") ? cache_data["classes"].size() : 0;
                result["hint"] = "Use rtti_cache_query to search classes by name";

                LOG_INFO("RTTI cache hit for {} ({} classes)", module_name,
                    cache_data.contains("classes") ? cache_data["classes"].size() : 0);

                return CreateSuccessResponse(result.dump());
            }
        }

        // Cache miss or force_rescan - perform scan
        LOG_INFO("RTTI scanning {} at 0x{:X}...", module_name, module_base);

        analysis::RTTIParser parser(
            [dma, pid](uint64_t addr, size_t sz) {
                return dma->ReadMemory(pid, addr, sz);
            },
            0  // Auto-detect from COL's self_rva
        );

        // Get PE sections for info
        auto sections = parser.GetPESections(module_base);

        // Scan entire module (no limit for caching)
        std::vector<analysis::RTTIClassInfo> found_classes;
        parser.ScanModule(module_base, [&](const analysis::RTTIClassInfo& info) {
            found_classes.push_back(info);
        });

        // Build summary stats
        int multiple_inheritance = 0, virtual_bases = 0;
        uint32_t max_methods = 0, total_methods = 0;
        std::string largest_class;
        for (const auto& info : found_classes) {
            if (info.is_multiple_inheritance) multiple_inheritance++;
            if (info.has_virtual_base) virtual_bases++;
            total_methods += info.method_count;
            if (info.method_count > max_methods) {
                max_methods = info.method_count;
                largest_class = info.demangled_name;
            }
        }

        json summary = {
            {"total_classes", found_classes.size()},
            {"multiple_inheritance", multiple_inheritance},
            {"virtual_bases", virtual_bases},
            {"largest_vtable", max_methods},
            {"largest_class", largest_class},
            {"avg_methods", found_classes.empty() ? 0 : total_methods / found_classes.size()}
        };

        // Build cache data (full class list)
        json cache_data;
        cache_data["module"] = module_name;
        cache_data["module_base"] = FormatAddress(module_base);
        cache_data["module_size"] = module_size;
        cache_data["summary"] = summary;

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream time_ss;
        time_ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        cache_data["cached_at"] = time_ss.str();

        json sections_array = json::array();
        for (const auto& section : sections) {
            if (section.name == ".rdata" || section.name == ".data") {
                json sec;
                sec["name"] = section.name;
                sec["address"] = FormatAddress(section.virtual_address);
                sec["size"] = section.virtual_size;
                sections_array.push_back(sec);
            }
        }
        cache_data["sections_scanned"] = sections_array;

        // Store scan_base so we can compute RVAs and convert back later
        cache_data["scan_base"] = FormatAddress(module_base);

        json classes_array = json::array();
        for (const auto& info : found_classes) {
            json cls;
            // Store RVA (relative to module base) instead of absolute address
            // This allows cache to work across game restarts with different ASLR bases
            uint64_t rva = info.vtable_address - module_base;
            cls["vtable_rva"] = rva;
            cls["methods"] = info.method_count;
            cls["flags"] = info.GetFlags();
            cls["type"] = info.demangled_name;
            cls["hierarchy"] = info.GetHierarchyString();
            classes_array.push_back(cls);
        }
        cache_data["classes"] = classes_array;

        // Save to cache
        if (module_size > 0) {
            rtti_cache_.Save(module_name, module_size, cache_data.dump(2));
        }

        // Return summary only (not full class list)
        json result;
        result["status"] = "scanned";
        result["module"] = module_name;
        result["module_base"] = FormatAddress(module_base);
        result["module_size"] = module_size;
        result["count"] = found_classes.size();
        result["summary"] = summary;
        if (module_size > 0) {
            result["cache_file"] = rtti_cache_.GetFilePath(module_name, module_size);
        }
        result["hint"] = "Use rtti_cache_query to search classes by name";

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheList(const std::string& body) {
    try {
        auto entries = rtti_cache_.ListEntries("classes");

        json result;
        json modules = json::array();

        for (const auto& entry : entries) {
            json mod;
            mod["module"] = entry.name;
            mod["size"] = entry.size;
            mod["classes"] = entry.item_count;
            mod["cache_file"] = entry.filepath;
            mod["cached_at"] = entry.cached_at;
            modules.push_back(mod);
        }

        result["count"] = modules.size();
        result["modules"] = modules;
        result["cache_directory"] = rtti_cache_.GetDirectory();

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheQuery(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string query = req.value("query", "");
        std::string module_filter = req.value("module", "");
        int max_results = req.value("max_results", 100);
        uint32_t pid = req.value("pid", 0);  // Optional PID to resolve RVAs to absolute addresses

        if (query.empty()) {
            return CreateErrorResponse("Missing required parameter: query");
        }

        // Build module base lookup map if PID provided
        std::map<std::string, uint64_t> current_bases;
        if (pid > 0) {
            auto* dma = app_->GetDMA();
            if (dma && dma->IsConnected()) {
                auto modules = dma->GetModuleList(pid);
                for (const auto& mod : modules) {
                    current_bases[utils::CacheManager::ToLower(mod.name)] = mod.base_address;
                }
            }
        }

        // Convert query to lowercase for case-insensitive search
        std::string query_lower = utils::CacheManager::ToLower(query);

        namespace fs = std::filesystem;
        std::string cache_dir = rtti_cache_.GetDirectory();

        json result;
        json matches = json::array();
        int total_searched = 0;

        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().filename().string();
            size_t last_underscore = filename.rfind('_');
            if (last_underscore == std::string::npos) continue;

            std::string module_name = filename.substr(0, last_underscore);

            // Filter by module if specified
            if (!module_filter.empty()) {
                std::string filter_lower = utils::CacheManager::ToLower(module_filter);
                std::string mod_lower = utils::CacheManager::ToLower(module_name);
                if (mod_lower.find(filter_lower) == std::string::npos) continue;
            }

            std::ifstream in(entry.path());
            if (!in.is_open()) continue;

            json cache_data = json::parse(in);
            if (!cache_data.contains("classes")) continue;

            // Get current base for this module (if PID provided)
            std::string mod_lower = utils::CacheManager::ToLower(module_name);
            uint64_t current_base = 0;
            if (current_bases.count(mod_lower)) {
                current_base = current_bases[mod_lower];
            }

            for (const auto& cls : cache_data["classes"]) {
                total_searched++;
                std::string type = cls.value("type", "");
                std::string type_lower = utils::CacheManager::ToLower(type);

                if (type_lower.find(query_lower) != std::string::npos) {
                    json match;
                    match["module"] = module_name;

                    // Convert RVA to absolute address if we have current base
                    // Support both new format (vtable_rva) and old format (vtable)
                    if (cls.contains("vtable_rva")) {
                        uint64_t rva = cls["vtable_rva"].get<uint64_t>();
                        if (current_base > 0) {
                            match["vtable"] = FormatAddress(current_base + rva);
                        } else {
                            // No PID provided - return RVA with prefix
                            std::stringstream ss;
                            ss << "RVA:0x" << std::hex << std::uppercase << rva;
                            match["vtable"] = ss.str();
                        }
                        match["vtable_rva"] = rva;
                    } else {
                        // Old cache format - return as-is
                        match["vtable"] = cls.value("vtable", "");
                    }

                    match["methods"] = cls.value("methods", 0);
                    match["flags"] = cls.value("flags", "");
                    match["type"] = type;
                    match["hierarchy"] = cls.value("hierarchy", "");
                    matches.push_back(match);

                    if ((int)matches.size() >= max_results) break;
                }
            }

            if ((int)matches.size() >= max_results) break;
        }

        result["query"] = query;
        result["count"] = matches.size();
        result["total_searched"] = total_searched;
        result["matches"] = matches;
        if (pid > 0) {
            result["pid"] = pid;
            result["addresses_resolved"] = !current_bases.empty();
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheGet(const std::string& body) {
    try {
        json req = json::parse(body);

        std::string module_name = req.value("module", "");
        int max_results = req.value("max_results", 1000);
        uint32_t pid = req.value("pid", 0);  // Optional PID to resolve RVAs

        if (module_name.empty()) {
            return CreateErrorResponse("Missing required parameter: module");
        }

        // Get current module base if PID provided
        uint64_t current_base = 0;
        if (pid > 0) {
            auto* dma = app_->GetDMA();
            if (dma && dma->IsConnected()) {
                auto modules = dma->GetModuleList(pid);
                std::string mod_name_lower = utils::CacheManager::ToLower(module_name);
                for (const auto& mod : modules) {
                    if (utils::CacheManager::ToLower(mod.name) == mod_name_lower) {
                        current_base = mod.base_address;
                        break;
                    }
                }
            }
        }

        namespace fs = std::filesystem;
        std::string cache_dir = rtti_cache_.GetDirectory();

        // Find matching cache file
        std::string query_lower = utils::CacheManager::ToLower(module_name);
        for (const auto& entry : fs::directory_iterator(cache_dir)) {
            if (entry.path().extension() != ".json") continue;

            std::string filename = entry.path().filename().string();
            size_t last_underscore = filename.rfind('_');
            if (last_underscore == std::string::npos) continue;

            std::string cached_module = filename.substr(0, last_underscore);

            // Case-insensitive match
            std::string mod_lower = utils::CacheManager::ToLower(cached_module);

            if (mod_lower == query_lower) {
                std::ifstream in(entry.path());
                if (!in.is_open()) continue;

                json cache_data = json::parse(in);

                // Convert RVAs to absolute addresses if PID provided and we have current base
                if (current_base > 0 && cache_data.contains("classes")) {
                    cache_data["current_base"] = FormatAddress(current_base);
                    cache_data["addresses_resolved"] = true;
                    for (auto& cls : cache_data["classes"]) {
                        if (cls.contains("vtable_rva")) {
                            uint64_t rva = cls["vtable_rva"].get<uint64_t>();
                            cls["vtable"] = FormatAddress(current_base + rva);
                        }
                    }
                }

                // Limit results if needed
                if (cache_data.contains("classes") && (int)cache_data["classes"].size() > max_results) {
                    json limited = cache_data;
                    json limited_classes = json::array();
                    for (int i = 0; i < max_results && i < (int)cache_data["classes"].size(); i++) {
                        limited_classes.push_back(cache_data["classes"][i]);
                    }
                    limited["classes"] = limited_classes;
                    limited["truncated"] = true;
                    limited["total_classes"] = cache_data["classes"].size();
                    return CreateSuccessResponse(limited.dump());
                }

                return CreateSuccessResponse(cache_data.dump());
            }
        }

        return CreateErrorResponse("Cache not found for module: " + module_name);

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleRTTICacheClear(const std::string& body) {
    try {
        json req = json::parse(body);
        std::string module_filter = req.value("module", "");

        size_t cleared = rtti_cache_.Clear(module_filter);

        json result;
        result["cleared"] = cleared;
        result["filter"] = module_filter.empty() ? "all" : module_filter;

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

std::string MCPServer::HandleReadVTable(const std::string& body) {
    try {
        auto req = json::parse(body);
        uint32_t pid = req["pid"];
        uint64_t vtable_address = std::stoull(req["vtable_address"].get<std::string>(), nullptr, 16);
        int count = req.value("count", 20);  // Default to 20 entries
        bool disasm = req.value("disassemble", false);  // Optional: disassemble first instructions
        int disasm_count = req.value("disasm_instructions", 5);  // Instructions to show per entry

        // Validate parameters
        if (vtable_address == 0) {
            return CreateErrorResponse("Invalid vtable_address: cannot read from NULL (0x0)");
        }
        if (count < 1 || count > 500) {
            return CreateErrorResponse("Invalid count: must be between 1 and 500");
        }
        if (disasm_count < 1 || disasm_count > 20) {
            disasm_count = 5;  // Default to 5 if out of range
        }

        auto* dma = app_->GetDMA();
        if (!dma || !dma->IsConnected()) {
            return CreateErrorResponse("DMA not connected - check hardware connection");
        }

        // Verify process exists
        auto proc_info = dma->GetProcessInfo(pid);
        if (!proc_info) {
            return CreateErrorResponse("Process not found: PID " + std::to_string(pid) + " does not exist or has terminated");
        }

        // Get modules for context resolution
        std::vector<ModuleInfo> modules;
        {
            std::lock_guard<std::mutex> lock(modules_mutex_);
            if (cached_modules_pid_ != pid) {
                cached_modules_ = dma->GetModuleList(pid);
                cached_modules_pid_ = pid;
            }
            modules = cached_modules_;
        }

        // Read vtable entries (array of 8-byte pointers)
        size_t bytes_to_read = count * 8;
        auto vtable_data = dma->ReadMemory(pid, vtable_address, bytes_to_read);
        if (vtable_data.size() < 8) {
            return CreateErrorResponse("Failed to read vtable at " + FormatAddress(vtable_address) +
                                        " - memory may be unmapped or protected");
        }

        // Parse function pointers
        json result;
        result["vtable_address"] = FormatAddress(vtable_address);
        result["context"] = FormatAddressWithContext(pid, vtable_address);

        json entries = json::array();
        int valid_count = 0;
        int null_count = 0;

        for (int i = 0; i < count && (size_t)(i * 8 + 8) <= vtable_data.size(); i++) {
            uint64_t func_ptr = *reinterpret_cast<uint64_t*>(vtable_data.data() + i * 8);

            json entry;
            entry["index"] = i;
            entry["offset"] = i * 8;

            if (func_ptr == 0) {
                entry["address"] = "0x0";
                entry["status"] = "null";
                null_count++;
                entries.push_back(entry);
                continue;
            }

            entry["address"] = FormatAddress(func_ptr);
            entry["context"] = FormatAddressWithContext(pid, func_ptr);

            // Check if this looks like a valid code pointer
            bool is_valid = false;
            for (const auto& mod : modules) {
                if (func_ptr >= mod.base_address && func_ptr < mod.base_address + mod.size) {
                    is_valid = true;
                    break;
                }
            }

            if (!is_valid) {
                entry["status"] = "invalid";
                entries.push_back(entry);
                continue;
            }

            entry["status"] = "valid";
            valid_count++;

            // Optionally disassemble first few instructions
            if (disasm) {
                auto code = dma->ReadMemory(pid, func_ptr, 64);  // Read enough for a few instructions
                if (!code.empty()) {
                    analysis::Disassembler disassembler(true);  // x64
                    analysis::DisassemblyOptions opts;
                    opts.max_instructions = disasm_count;

                    auto instructions = disassembler.Disassemble(code, func_ptr, opts);

                    json disasm_arr = json::array();
                    for (const auto& insn : instructions) {
                        json insn_obj;
                        std::stringstream addr_ss;
                        addr_ss << "0x" << std::hex << std::uppercase << insn.address;
                        insn_obj["address"] = addr_ss.str();

                        // Format bytes
                        std::stringstream bytes_ss;
                        for (size_t j = 0; j < insn.bytes.size(); j++) {
                            if (j > 0) bytes_ss << " ";
                            bytes_ss << std::hex << std::setw(2) << std::setfill('0') << (int)insn.bytes[j];
                        }
                        insn_obj["bytes"] = bytes_ss.str();

                        insn_obj["mnemonic"] = insn.mnemonic;
                        insn_obj["operands"] = insn.operands;
                        disasm_arr.push_back(insn_obj);
                    }
                    entry["disassembly"] = disasm_arr;
                }
            }

            entries.push_back(entry);
        }

        result["entries"] = entries;
        result["count"] = entries.size();
        result["valid_count"] = valid_count;
        result["null_count"] = null_count;
        result["invalid_count"] = entries.size() - valid_count - null_count;

        // Try to get class name via RTTI if available
        // RTTI info is typically at vtable[-1]
        auto rtti_ptr_data = dma->ReadMemory(pid, vtable_address - 8, 8);
        if (rtti_ptr_data.size() == 8) {
            uint64_t rtti_ptr = *reinterpret_cast<uint64_t*>(rtti_ptr_data.data());
            if (rtti_ptr != 0) {
                // Find module base for RTTI parsing
                uint64_t module_base = 0;
                for (const auto& mod : modules) {
                    if (vtable_address >= mod.base_address && vtable_address < mod.base_address + mod.size) {
                        module_base = mod.base_address;
                        break;
                    }
                }

                if (module_base != 0) {
                    analysis::RTTIParser parser(
                        [dma, pid](uint64_t addr, size_t size) {
                            return dma->ReadMemory(pid, addr, size);
                        },
                        module_base
                    );

                    auto info = parser.ParseVTable(vtable_address);
                    if (info) {
                        result["class_name"] = info->demangled_name;
                        result["hierarchy"] = info->GetHierarchyString();
                    }
                }
            }
        }

        return CreateSuccessResponse(result.dump());

    } catch (const std::exception& e) {
        return CreateErrorResponse(std::string("Error: ") + e.what());
    }
}

} // namespace orpheus::mcp
