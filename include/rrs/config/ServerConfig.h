#pragma once

#include "rrs/log/Logger.h"

#include <cstdint>
#include <string>

namespace rrs {

struct ServerConfig {
    std::string app_name{"rrs"};
    std::uint16_t listen_port{9000};
    std::uint32_t io_thread_count{2};
    std::uint32_t worker_thread_count{2};
    std::uint32_t target_tick_hz{30};
    std::uint32_t room_capacity{16};
    std::uint32_t max_catch_up_ticks{2};
    std::uint32_t outbound_queue_limit{16};
    LogLevel log_level{LogLevel::kInfo};
};

[[nodiscard]] ServerConfig ParseServerConfig(int argc, char* argv[]);

} // namespace rrs
