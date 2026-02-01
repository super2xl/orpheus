#pragma once

#include <string>
#include <thread>
#include <chrono>
#include <atomic>

namespace orpheus {

// Telemetry via Cloudflare Worker relay
// Sends startup/shutdown pings to track usage analytics
// Endpoint is public, webhook URLs are stored server-side (secure)
class Telemetry {
public:
    static Telemetry& Instance() {
        static Telemetry instance;
        return instance;
    }

    // Send startup ping with version info (async, non-blocking)
    void SendStartupPing();

    // Send shutdown ping with session duration (blocking - called at exit)
    void SendShutdownPing();

    // Get session ID
    const std::string& GetSessionId() const { return m_sessionId; }

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

    static constexpr const char* WORKER_ENDPOINT = "https://orpheus-telemetry.sdhaf8.workers.dev";

    std::string m_sessionId;
    std::chrono::steady_clock::time_point m_startTime;
    std::thread m_thread;
    std::atomic<bool> m_startupSent{false};
};

} // namespace orpheus
