#pragma once

#include <cstdint>

namespace rrs {

struct PlayerInput {
    static constexpr std::uint8_t kSplitFlag = 1U << 0U;

    std::int16_t move_x{0};
    std::int16_t move_y{0};
    std::uint8_t input_flags{0};
};

} // namespace rrs
