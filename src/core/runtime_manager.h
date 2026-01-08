#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <functional>

namespace orpheus {

/**
 * RuntimeManager - Handles embedded resource extraction and AppData management
 *
 * This singleton class is responsible for:
 * - Creating %APPDATA%/Orpheus directory structure
 * - Extracting embedded DLLs (only if missing or outdated)
 * - Setting up DLL search paths
 * - Providing paths for cache, config, and MCP bridge
 *
 * CRITICAL: Initialize() MUST be called before any DMA operations
 */
class RuntimeManager {
public:
    // Singleton access
    static RuntimeManager& Instance();

    // Delete copy/move constructors
    RuntimeManager(const RuntimeManager&) = delete;
    RuntimeManager& operator=(const RuntimeManager&) = delete;
    RuntimeManager(RuntimeManager&&) = delete;
    RuntimeManager& operator=(RuntimeManager&&) = delete;

    /**
     * Initialize the runtime environment
     * - Creates temporary directory
     * - Extracts all embedded DLLs
     * - Sets up DLL search path
     * @return true on success, false on failure
     */
    bool Initialize();

    /**
     * Clean up all extracted files and temporary directory
     * Called automatically on destruction, but can be called manually
     */
    void Cleanup();

    /**
     * Check if the runtime is initialized
     */
    [[nodiscard]] bool IsInitialized() const { return initialized_; }

    /**
     * Get the path to a specific extracted DLL
     * @param dll_name Name of the DLL (e.g., "vmm.dll")
     * @return Full path to the extracted DLL, or empty path if not found
     */
    [[nodiscard]] std::filesystem::path GetDLLPath(std::string_view dll_name) const;

    /**
     * Get the Orpheus AppData directory path
     */
    [[nodiscard]] const std::filesystem::path& GetAppDataDirectory() const { return app_data_dir_; }

    /**
     * Get the DLLs directory path
     */
    [[nodiscard]] std::filesystem::path GetDLLDirectory() const { return app_data_dir_ / "dlls"; }

    /**
     * Get the cache directory path
     */
    [[nodiscard]] std::filesystem::path GetCacheDirectory() const { return app_data_dir_ / "cache"; }

    /**
     * Get the config directory path
     */
    [[nodiscard]] std::filesystem::path GetConfigDirectory() const { return app_data_dir_ / "config"; }

    /**
     * Get list of all extracted files
     */
    [[nodiscard]] const std::vector<std::filesystem::path>& GetExtractedFiles() const {
        return extracted_files_;
    }

    /**
     * Set error callback for logging
     */
    void SetErrorCallback(std::function<void(const std::string&)> callback) {
        error_callback_ = std::move(callback);
    }

    /**
     * Load a DLL dynamically from the extracted location
     * @param dll_name Name of the DLL
     * @return Handle to loaded DLL, or nullptr on failure
     */
    void* LoadExtractedDLL(std::string_view dll_name);

    /**
     * Unload a previously loaded DLL
     */
    void UnloadDLL(void* handle);

private:
    RuntimeManager() = default;
    ~RuntimeManager();

    /**
     * Extract a single DLL from embedded resources
     * @param name Filename for the DLL
     * @param data Pointer to embedded data
     * @param size Size of embedded data
     * @return true on success
     */
    bool ExtractDLL(std::string_view name, const unsigned char* data, size_t size);

    /**
     * Set up platform-specific DLL search path
     */
    bool SetupDLLSearchPath();

    /**
     * Report an error through the callback or stderr
     */
    void ReportError(const std::string& message);

    std::filesystem::path app_data_dir_;
    std::vector<std::filesystem::path> extracted_files_;
    std::vector<void*> loaded_dlls_;
    std::function<void(const std::string&)> error_callback_;
    bool initialized_ = false;
};

} // namespace orpheus
