#include "telemetry.h"
#include "logger.h"
#include "version.h"

#include <sstream>
#include <random>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace orpheus {

// Generate a simple session ID for deduplication
static std::string GenerateSessionId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::stringstream ss;
    ss << std::hex << dist(gen) << dist(gen);
    return ss.str().substr(0, 16);
}

Telemetry::Telemetry()
    : m_sessionId(GenerateSessionId())
    , m_startTime(std::chrono::steady_clock::now())
{
}

Telemetry::~Telemetry() {
    WaitForPendingRequests();
}

void Telemetry::WaitForPendingRequests() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::string Telemetry::FormatDuration(int seconds) {
    if (seconds < 60) {
        return std::to_string(seconds) + "s";
    } else if (seconds < 3600) {
        int mins = seconds / 60;
        int secs = seconds % 60;
        return std::to_string(mins) + "m " + std::to_string(secs) + "s";
    } else {
        int hours = seconds / 3600;
        int mins = (seconds % 3600) / 60;
        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    }
}

std::string Telemetry::BuildStartupEmbed() {
    std::stringstream json;
    json << "{";
    json << "\"embeds\": [{";
    json << "\"title\": \"Orpheus Startup\",";
    json << "\"color\": 5814783,";  // Blue
    json << "\"fields\": [";
    json << "{\"name\": \"Version\", \"value\": \"" << orpheus::version::VERSION_FULL << "\", \"inline\": true},";
    json << "{\"name\": \"Platform\", \"value\": \"" << orpheus::version::PLATFORM << "\", \"inline\": true},";
    json << "{\"name\": \"Build\", \"value\": \"" << orpheus::version::BUILD_TYPE << "\", \"inline\": true},";
    json << "{\"name\": \"Git\", \"value\": \"" << orpheus::version::GIT_HASH_SHORT << "\", \"inline\": true},";
    json << "{\"name\": \"Session\", \"value\": \"`" << m_sessionId << "`\", \"inline\": true}";
    json << "],";
    json << "\"timestamp\": \"" << orpheus::version::BUILD_TIMESTAMP << "\"";
    json << "}]";
    json << "}";
    return json.str();
}

std::string Telemetry::BuildShutdownEmbed() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();
    std::string durationStr = FormatDuration(static_cast<int>(duration));

    std::stringstream json;
    json << "{";
    json << "\"embeds\": [{";
    json << "\"title\": \"Orpheus Shutdown\",";
    json << "\"color\": 15158332,";  // Red
    json << "\"fields\": [";
    json << "{\"name\": \"Version\", \"value\": \"" << orpheus::version::VERSION_FULL << "\", \"inline\": true},";
    json << "{\"name\": \"Session\", \"value\": \"`" << m_sessionId << "`\", \"inline\": true},";
    json << "{\"name\": \"Duration\", \"value\": \"" << durationStr << "\", \"inline\": true}";
    json << "]";
    json << "}]";
    json << "}";
    return json.str();
}

void Telemetry::SendToWorker(const std::string& type, const std::string& discordPayload, bool async) {
#ifdef PLATFORM_WINDOWS
    auto sendFunc = [type, discordPayload]() {
        try {
            // Build wrapper payload for worker: {"type": "usage", "payload": <discord_embed>}
            std::string workerPayload = "{\"type\": \"" + type + "\", \"payload\": " + discordPayload + "}";

            // Parse worker endpoint URL
            std::wstring wUrl = L"https://orpheus-telemetry.sdhaf8.workers.dev";

            HINTERNET hSession = WinHttpOpen(
                L"Orpheus/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

            if (!hSession) {
                LOG_DEBUG("Telemetry: WinHttpOpen failed");
                return;
            }

            HINTERNET hConnect = WinHttpConnect(
                hSession,
                L"orpheus-telemetry.sdhaf8.workers.dev",
                INTERNET_DEFAULT_HTTPS_PORT,
                0
            );

            if (!hConnect) {
                WinHttpCloseHandle(hSession);
                LOG_DEBUG("Telemetry: WinHttpConnect failed");
                return;
            }

            HINTERNET hRequest = WinHttpOpenRequest(
                hConnect,
                L"POST",
                L"/",
                NULL,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );

            if (!hRequest) {
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                LOG_DEBUG("Telemetry: WinHttpOpenRequest failed");
                return;
            }

            LPCWSTR headers = L"Content-Type: application/json";

            BOOL result = WinHttpSendRequest(
                hRequest,
                headers,
                (DWORD)-1L,
                (LPVOID)workerPayload.c_str(),
                (DWORD)workerPayload.length(),
                (DWORD)workerPayload.length(),
                0
            );

            if (result) {
                WinHttpReceiveResponse(hRequest, NULL);
                LOG_DEBUG("Telemetry: Sent to worker successfully");
            } else {
                LOG_DEBUG("Telemetry: WinHttpSendRequest failed: {}", GetLastError());
            }

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);

        } catch (...) {
            LOG_DEBUG("Telemetry: Exception during send");
        }
    };

    if (async) {
        WaitForPendingRequests();
        m_thread = std::thread(sendFunc);
    } else {
        sendFunc();
    }
#else
    (void)type;
    (void)discordPayload;
    (void)async;
    LOG_DEBUG("Telemetry: Not implemented for this platform");
#endif
}

void Telemetry::SendStartupPing() {
    std::string embed = BuildStartupEmbed();
    SendToWorker("usage", embed, true);  // Async
    m_startupSent = true;
    LOG_DEBUG("Telemetry: Startup ping queued");
}

void Telemetry::SendShutdownPing() {
    if (!m_startupSent) {
        return;
    }

    WaitForPendingRequests();

    std::string embed = BuildShutdownEmbed();
    SendToWorker("usage", embed, false);  // Blocking
    LOG_DEBUG("Telemetry: Shutdown ping sent");
}

} // namespace orpheus
