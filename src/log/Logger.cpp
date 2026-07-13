#include "rrs/log/Logger.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace rrs {

namespace {

LogLevel g_log_level = LogLevel::kInfo;

[[nodiscard]] std::string_view LogLevelName(LogLevel log_level)
{
    switch (log_level) {
    case LogLevel::kInfo:
        return "info";
    case LogLevel::kWarn:
        return "warn";
    case LogLevel::kError:
        return "error";
    case LogLevel::kOff:
        return "off";
    }
    return "info";
}

[[nodiscard]] std::string MakeLogFilePath(std::uint32_t io_thread_count, std::uint32_t worker_thread_count)
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto local_time = std::chrono::zoned_time{std::chrono::current_zone(), now};
    return std::format("logs/{:%Y%m%d_%H%M%S}_io{}_worker{}.log", local_time, io_thread_count, worker_thread_count);
}

} // namespace

void Logger::Initialize(std::string_view app_name,
                        LogLevel log_level,
                        std::uint32_t io_thread_count,
                        std::uint32_t worker_thread_count)
{
    g_log_level = log_level;
    std::filesystem::create_directories("logs");
    auto logger = spdlog::basic_logger_mt(
        std::string(app_name),
        MakeLogFilePath(io_thread_count, worker_thread_count),
        true);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%t] %v");
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
    LogMetrics(std::format("{} logger Initialized log_level={}", app_name, LogLevelName(log_level)));
}

bool Logger::ShouldLog(Level level) noexcept
{
    switch (g_log_level) {
    case LogLevel::kInfo:
        return true;
    case LogLevel::kWarn:
        return level == Level::kWarning || level == Level::kError;
    case LogLevel::kError:
        return level == Level::kError;
    case LogLevel::kOff:
        return false;
    }
    return true;
}

void Logger::Log(Level level, std::string_view message)
{
    switch (level) {
    case Level::kInfo:
        spdlog::info("{}", message);
        return;
    case Level::kWarning:
        spdlog::warn("{}", message);
        return;
    case Level::kError:
        spdlog::error("{}", message);
        return;
    }
}

void Logger::LogMetrics(std::string_view message)
{
    if (auto logger = spdlog::default_logger()) {
        logger->log(spdlog::source_loc{}, spdlog::level::info, "{}", message);
    }
}

} // namespace rrs
