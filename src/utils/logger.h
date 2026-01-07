#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <deque>

namespace orpheus {

/**
 * LogEntry - Single log entry for UI display
 */
struct LogEntry {
    spdlog::level::level_enum level;
    std::string message;
    std::string timestamp;
};

/**
 * Logger - Central logging system
 *
 * Provides both console and file logging, plus an in-memory
 * buffer for displaying logs in the ImGui console.
 */
class Logger {
public:
    static Logger& Instance();

    // Initialize logging
    bool Initialize(const std::string& log_file = "");

    // Log methods
    template<typename... Args>
    void Info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->trace(fmt, std::forward<Args>(args)...);
    }

    // Get recent log entries for UI display
    std::vector<LogEntry> GetRecentEntries(size_t count = 100);

    // Clear log buffer
    void ClearBuffer();

    // Set log level
    void SetLevel(spdlog::level::level_enum level);

private:
    Logger() = default;

    std::shared_ptr<spdlog::logger> logger_;
    std::deque<LogEntry> log_buffer_;
    std::mutex buffer_mutex_;
    static constexpr size_t MAX_BUFFER_SIZE = 1000;
};

// Convenience macros
#define LOG_INFO(...) orpheus::Logger::Instance().Info(__VA_ARGS__)
#define LOG_WARN(...) orpheus::Logger::Instance().Warn(__VA_ARGS__)
#define LOG_ERROR(...) orpheus::Logger::Instance().Error(__VA_ARGS__)
#define LOG_DEBUG(...) orpheus::Logger::Instance().Debug(__VA_ARGS__)
#define LOG_TRACE(...) orpheus::Logger::Instance().Trace(__VA_ARGS__)

} // namespace orpheus
