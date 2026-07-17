#include "rrs/config/ServerConfig.h"

#include "rrs/observability/Logger.h"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

rrs::LogLevel ParseLogLevel(std::string_view value)
{
    if (value == "info") {
        return rrs::LogLevel::kInfo;
    }
    if (value == "warn") {
        return rrs::LogLevel::kWarn;
    }
    if (value == "error") {
        return rrs::LogLevel::kError;
    }
    if (value == "off") {
        return rrs::LogLevel::kOff;
    }
    throw std::runtime_error("invalid --log, expected info|warn|error|off");
}

std::uint32_t ParsePositiveU32(std::string_view value, std::string_view option_name)
{
    std::uint32_t result{0};
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, result);
    if (error != std::errc{} || parsed_end != end || result == 0) {
        throw std::runtime_error("invalid value for " + std::string{option_name} + ", expected positive integer");
    }
    return result;
}

std::string_view TakeOptionValue(int argc, char* argv[], int& index, std::string_view option_name)
{
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string{option_name});
    }
    ++index;
    return std::string_view{argv[index]};
}

} // namespace

namespace rrs {

ServerConfig ParseServerConfig(int argc, char* argv[])
{
    auto config = ServerConfig{};
    for (int index = 1; index < argc; ++index) {
        const auto argument = std::string_view{argv[index]};
        if (argument == "--io") {
            config.io_thread_count = ParsePositiveU32(TakeOptionValue(argc, argv, index, argument), argument);
            continue;
        }
        if (argument == "--worker") {
            config.worker_thread_count = ParsePositiveU32(TakeOptionValue(argc, argv, index, argument), argument);
            continue;
        }
        if (argument == "--log") {
            config.log_level = ParseLogLevel(TakeOptionValue(argc, argv, index, argument));
            continue;
        }

        throw std::runtime_error("unknown startup option: " + std::string{argument});
    }
    return config;
}

} // namespace rrs
