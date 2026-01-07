#include "logger.h"
#include <spdlog/sinks/callback_sink.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace orpheus {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

bool Logger::Initialize(const std::string& log_file) {
    try {
        std::vector<spdlog::sink_ptr> sinks;

#ifdef PLATFORM_WINDOWS
        // On Windows GUI apps, skip console sink to avoid allocating a console window.
        // We have the callback sink for UI logging and file sink for persistent logs.
        // Only add console sink if we actually have a console attached (e.g., debug builds
        // launched from command line).
        if (GetConsoleWindow() != nullptr) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::info);
            console_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");
            sinks.push_back(console_sink);
        }
#else
        // On other platforms, always add console sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        sinks.push_back(console_sink);
#endif

        // File sink if specified
        if (!log_file.empty()) {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
            file_sink->set_level(spdlog::level::trace);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(file_sink);
        }

        // Callback sink for UI buffer
        auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [this](const spdlog::details::log_msg& msg) {
                LogEntry entry;
                entry.level = msg.level;
                entry.message = std::string(msg.payload.data(), msg.payload.size());

                // Format timestamp
                auto time = std::chrono::system_clock::to_time_t(msg.time);
                std::stringstream ss;
                ss << std::put_time(std::localtime(&time), "%H:%M:%S");
                entry.timestamp = ss.str();

                std::lock_guard<std::mutex> lock(buffer_mutex_);
                log_buffer_.push_back(std::move(entry));

                // Trim buffer if needed
                while (log_buffer_.size() > MAX_BUFFER_SIZE) {
                    log_buffer_.pop_front();
                }
            }
        );
        callback_sink->set_level(spdlog::level::trace);
        sinks.push_back(callback_sink);

        // Create logger
        logger_ = std::make_shared<spdlog::logger>("orpheus", sinks.begin(), sinks.end());
        logger_->set_level(spdlog::level::debug);
        logger_->flush_on(spdlog::level::warn);

        // Set as default
        spdlog::set_default_logger(logger_);

        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        return false;
    }
}

std::vector<LogEntry> Logger::GetRecentEntries(size_t count) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    std::vector<LogEntry> result;
    size_t start = log_buffer_.size() > count ? log_buffer_.size() - count : 0;

    for (size_t i = start; i < log_buffer_.size(); i++) {
        result.push_back(log_buffer_[i]);
    }

    return result;
}

void Logger::ClearBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    log_buffer_.clear();
}

void Logger::SetLevel(spdlog::level::level_enum level) {
    if (logger_) {
        logger_->set_level(level);
    }
}

} // namespace orpheus
