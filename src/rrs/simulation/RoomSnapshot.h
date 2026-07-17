#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rrs {

struct PlayerStateSnapshot {
    PlayerId player_id;
    std::uint16_t active_ball_mask{0};
    std::array<PlayerBall, kMaxBallsPerPlayer> balls{};
};

struct RoomSnapshot {
    TickSeq tick_seq{0};
    std::vector<PlayerStateSnapshot> players;
    std::vector<FoodEntity> foods;
    PlayerId winner_player_id;
};

} // namespace rrs
