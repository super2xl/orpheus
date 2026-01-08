#include "runtime_manager.h"
#include "embedded_resources.h"

#include <fstream>
#include <random>
#include <iostream>
#include <chrono>

#ifdef PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <cstdlib>
#endif

namespace orpheus {

RuntimeManager& RuntimeManager::Instance() {
    static RuntimeManager instance;
    return instance;
}

RuntimeManager::~RuntimeManager() {
    Cleanup();
}

bool RuntimeManager::Initialize() {
    if (initialized_) {
        return true;
    }

    try {
        // Get AppData path - %APPDATA%/Orpheus
#ifdef PLATFORM_WINDOWS
        char* appdata = nullptr;
        size_t len = 0;
        if (_dupenv_s(&appdata, &len, "APPDATA") != 0 || appdata == nullptr) {
            ReportError("Failed to get APPDATA environment variable");
            return false;
        }
        app_data_dir_ = std::filesystem::path(appdata) / "Orpheus";
        free(appdata);
#else
        const char* home = getenv("HOME");
        if (home == nullptr) {
            ReportError("Failed to get HOME environment variable");
            return false;
        }
        app_data_dir_ = std::filesystem::path(home) / ".orpheus";
#endif

        // Create directory structure
        auto dll_dir = app_data_dir_ / "dlls";
        auto cache_dir = app_data_dir_ / "cache";
        auto config_dir = app_data_dir_ / "config";

        std::filesystem::create_directories(app_data_dir_);
        std::filesystem::create_directories(dll_dir);
        std::filesystem::create_directories(cache_dir);
        std::filesystem::create_directories(config_dir);

    } catch (const std::filesystem::filesystem_error& e) {
        ReportError("Filesystem error creating AppData directory: " + std::string(e.what()));
        return false;
    }

    // Extract embedded DLLs (only if missing)
    bool all_success = true;
    auto dll_dir = app_data_dir_ / "dlls";

    for (const auto& resource : embedded::resources) {
        auto target_path = dll_dir / resource.name;

        // Check if file exists and has correct size
        bool needs_extract = true;
        if (std::filesystem::exists(target_path)) {
            auto existing_size = std::filesystem::file_size(target_path);
            if (existing_size == resource.size) {
                needs_extract = false;
                extracted_files_.push_back(target_path);
            }
        }

        if (needs_extract) {
            if (!ExtractDLL(resource.name, resource.data, resource.size)) {
                ReportError("Failed to extract: " + std::string(resource.name));
                all_success = false;
            }
        }
    }

    if (!all_success) {
        return false;
    }

    // Set up DLL search path
    if (!SetupDLLSearchPath()) {
        ReportError("Failed to set up DLL search path");
        return false;
    }

    initialized_ = true;
    return true;
}

bool RuntimeManager::ExtractDLL(std::string_view name,
                                const unsigned char* data,
                                size_t size) {
    auto dll_path = app_data_dir_ / "dlls" / name;

    try {
        std::ofstream out(dll_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            ReportError("Failed to open file for writing: " + dll_path.string());
            return false;
        }

        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        out.close();

        if (!out.good()) {
            ReportError("Failed to write file: " + dll_path.string());
            return false;
        }

        extracted_files_.push_back(dll_path);
        return true;

    } catch (const std::exception& e) {
        ReportError("Exception extracting " + std::string(name) + ": " + e.what());
        return false;
    }
}

bool RuntimeManager::SetupDLLSearchPath() {
    auto dll_dir = app_data_dir_ / "dlls";

#ifdef PLATFORM_WINDOWS
    // Add DLL directory to DLL search path
    // SetDllDirectoryA sets the search path for LoadLibrary calls
    if (!SetDllDirectoryA(dll_dir.string().c_str())) {
        return false;
    }

    // Also add to PATH environment variable for child processes
    std::string current_path;
    char* path_env = nullptr;
    size_t path_len = 0;
    if (_dupenv_s(&path_env, &path_len, "PATH") == 0 && path_env != nullptr) {
        current_path = path_env;
        free(path_env);
    }

    std::string new_path = dll_dir.string() + ";" + current_path;
    _putenv_s("PATH", new_path.c_str());

    return true;
#else
    // On Linux, add to LD_LIBRARY_PATH
    const char* current_path = getenv("LD_LIBRARY_PATH");
    std::string new_path = dll_dir.string();
    if (current_path != nullptr) {
        new_path += ":" + std::string(current_path);
    }
    return setenv("LD_LIBRARY_PATH", new_path.c_str(), 1) == 0;
#endif
}

void RuntimeManager::Cleanup() {
    if (!initialized_) {
        return;
    }

    // Unload any loaded DLLs first
    for (auto* handle : loaded_dlls_) {
        if (handle != nullptr) {
            UnloadDLL(handle);
        }
    }
    loaded_dlls_.clear();

    // Small delay to ensure DLLs are fully unloaded
#ifdef PLATFORM_WINDOWS
    Sleep(100);
#else
    usleep(100000);
#endif

    // Note: We do NOT delete AppData files - they are persistent
    // This allows faster startup on subsequent runs
    extracted_files_.clear();
    initialized_ = false;
}

std::filesystem::path RuntimeManager::GetDLLPath(std::string_view dll_name) const {
    if (!initialized_) {
        return {};
    }

    auto path = app_data_dir_ / "dlls" / dll_name;
    if (std::filesystem::exists(path)) {
        return path;
    }
    return {};
}

void* RuntimeManager::LoadExtractedDLL(std::string_view dll_name) {
    auto path = GetDLLPath(dll_name);
    if (path.empty()) {
        ReportError("DLL not found: " + std::string(dll_name));
        return nullptr;
    }

#ifdef PLATFORM_WINDOWS
    HMODULE handle = LoadLibraryA(path.string().c_str());
    if (handle == nullptr) {
        DWORD error = GetLastError();
        ReportError("LoadLibrary failed for " + std::string(dll_name) +
                   ", error: " + std::to_string(error));
        return nullptr;
    }
    loaded_dlls_.push_back(handle);
    return handle;
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        ReportError("dlopen failed for " + std::string(dll_name) +
                   ": " + std::string(dlerror()));
        return nullptr;
    }
    loaded_dlls_.push_back(handle);
    return handle;
#endif
}

void RuntimeManager::UnloadDLL(void* handle) {
    if (handle == nullptr) {
        return;
    }

#ifdef PLATFORM_WINDOWS
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif

    // Remove from tracked list
    auto it = std::find(loaded_dlls_.begin(), loaded_dlls_.end(), handle);
    if (it != loaded_dlls_.end()) {
        loaded_dlls_.erase(it);
    }
}

void RuntimeManager::ReportError(const std::string& message) {
    if (error_callback_) {
        error_callback_(message);
    } else {
        std::cerr << "[RuntimeManager ERROR] " << message << std::endl;
    }
}

} // namespace orpheus
