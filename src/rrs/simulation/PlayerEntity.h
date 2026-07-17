#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rrs {

inline constexpr std::size_t kMaxBallsPerPlayer = 16;

struct PlayerBall {
    Vector2 position;
    float radius{0.0F};
};

struct PlayerEntity {
    PlayerId player_id;
    Vector2 input_direction;
    TickSeq respawn_tick{0};
    std::uint16_t active_ball_mask{0};
    std::array<PlayerBall, kMaxBallsPerPlayer> balls{};
};

[[nodiscard]] inline bool IsAlive(const PlayerEntity& player) noexcept
{
    return player.active_ball_mask != 0;
}

} // namespace rrs
