#pragma once

#include "rrs/observability/Logger.h"

#include <cstdint>
#include <string>

namespace rrs {

struct ServerConfig {
    std::string app_name{"rrs"};
    std::uint16_t listen_port{9000};
    std::uint32_t io_thread_count{2};
    std::uint32_t worker_thread_count{2};
    LogLevel log_level{LogLevel::kInfo};
};

[[nodiscard]] ServerConfig ParseServerConfig(int argc, char* argv[]);

} // namespace rrs
