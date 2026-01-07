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

    // Create unique temp directory name using timestamp and random number
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 99999);

    std::string dirname = "orpheus_" + std::to_string(timestamp % 1000000) +
                          "_" + std::to_string(dis(gen));

    try {
        temp_dir_ = std::filesystem::temp_directory_path() / dirname;

        if (!std::filesystem::create_directories(temp_dir_)) {
            ReportError("Failed to create temp directory: " + temp_dir_.string());
            return false;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        ReportError("Filesystem error creating temp directory: " + std::string(e.what()));
        return false;
    }

    // Extract all embedded DLLs
    bool all_success = true;

    for (const auto& resource : embedded::resources) {
        if (!ExtractDLL(resource.name, resource.data, resource.size)) {
            ReportError("Failed to extract: " + std::string(resource.name));
            all_success = false;
        }
    }

    if (!all_success) {
        Cleanup();
        return false;
    }

    // Set up DLL search path
    if (!SetupDLLSearchPath()) {
        ReportError("Failed to set up DLL search path");
        Cleanup();
        return false;
    }

    initialized_ = true;
    return true;
}

bool RuntimeManager::ExtractDLL(std::string_view name,
                                const unsigned char* data,
                                size_t size) {
    auto dll_path = temp_dir_ / name;

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
#ifdef PLATFORM_WINDOWS
    // Add temp directory to DLL search path
    // SetDllDirectoryA sets the search path for LoadLibrary calls
    if (!SetDllDirectoryA(temp_dir_.string().c_str())) {
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

    std::string new_path = temp_dir_.string() + ";" + current_path;
    _putenv_s("PATH", new_path.c_str());

    return true;
#else
    // On Linux, add to LD_LIBRARY_PATH
    const char* current_path = getenv("LD_LIBRARY_PATH");
    std::string new_path = temp_dir_.string();
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

    // Remove all extracted files
    for (const auto& file : extracted_files_) {
        try {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Ignore errors during cleanup - file may be in use
        }
    }
    extracted_files_.clear();

    // Remove temp directory
    try {
        if (std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Ignore errors - best effort cleanup
    }

    temp_dir_.clear();
    initialized_ = false;
}

std::filesystem::path RuntimeManager::GetDLLPath(std::string_view dll_name) const {
    if (!initialized_) {
        return {};
    }

    auto path = temp_dir_ / dll_name;
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
