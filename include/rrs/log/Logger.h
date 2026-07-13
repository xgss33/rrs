#pragma once

#include <format>
#include <cstdint>
#include <string_view>
#include <utility>

namespace rrs {

enum class LogLevel {
    kInfo,
    kWarn,
    kError,
    kOff,
};

class Logger {
public:
    static void Initialize(std::string_view app_name,
                           LogLevel log_level,
                           std::uint32_t io_thread_count,
                           std::uint32_t worker_thread_count);

    template <typename... Args>
    static void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        if (!ShouldLog(Level::kInfo)) {
            return;
        }
        Log(Level::kInfo, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        if (!ShouldLog(Level::kWarning)) {
            return;
        }
        Log(Level::kWarning, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        if (!ShouldLog(Level::kError)) {
            return;
        }
        Log(Level::kError, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Metrics(std::format_string<Args...> fmt, Args&&... args)
    {
        LogMetrics(std::format(fmt, std::forward<Args>(args)...));
    }

private:
    enum class Level {
        kInfo,
        kWarning,
        kError,
    };

    [[nodiscard]] static bool ShouldLog(Level level) noexcept;
    static void Log(Level level, std::string_view message);
    static void LogMetrics(std::string_view message);
};

} // namespace rrs
