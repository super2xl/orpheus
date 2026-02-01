#pragma once

#include <string>
#include <thread>
#include <chrono>
#include <atomic>

namespace orpheus {

// Telemetry via Cloudflare Worker relay
// Sends startup/shutdown pings to track usage analytics
// Enabled by default - can disable in Settings > Telemetry
//
// Just want to see if this thing is actually useful - not intrusive at all!
// Only sends: version, platform, session duration, approximate region (via CF)
class Telemetry {
public:
    static Telemetry& Instance() {
        static Telemetry instance;
        return instance;
    }

    // Enable/disable telemetry (persisted to config)
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }

    // Load enabled state from config file
    // Call after RuntimeManager::Initialize()
    void LoadFromConfig();

    // Save enabled state to config file
    void SaveToConfig();

    // Send startup ping with version info (async, non-blocking)
    // Does nothing if telemetry is disabled
    void SendStartupPing();

    // Send shutdown ping with session duration (blocking - called at exit)
    // Does nothing if telemetry is disabled
    void SendShutdownPing();

    // Get session ID
    const std::string& GetSessionId() const { return session_id_; }

private:
    Telemetry();
    ~Telemetry();

    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;

    void SendToWorker(const std::string& type, const std::string& discordPayload, bool async);
    std::string BuildStartupEmbed();
    std::string BuildShutdownEmbed();
    std::string FormatDuration(int seconds);
    void WaitForPendingRequests();

    static constexpr const char* kWorkerEndpoint = "https://orpheus-telemetry.sdhaf8.workers.dev";

    std::string session_id_;
    std::chrono::steady_clock::time_point start_time_;
    std::thread thread_;
    std::atomic<bool> startup_sent_{false};
    std::atomic<bool> enabled_{true};  // Enabled by default - can disable in Settings
};

} // namespace orpheus
