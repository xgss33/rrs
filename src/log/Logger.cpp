#include "rrs/log/Logger.h"

#include <string>
#include <string_view>
#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>
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

} // namespace

void Logger::Initialize(std::string_view app_name, LogLevel log_level)
{
    g_log_level = log_level;
    auto logger = spdlog::stdout_color_mt(std::string(app_name));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%t] %v");
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
    LogMetrics(std::format("{} logger Initialized log_level={}", app_name, LogLevelName(log_level)));
}

void Logger::Log(Level level, std::string_view message)
{
    const auto should_log = [level] {
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
    }();

    if (!should_log) {
        return;
    }

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
