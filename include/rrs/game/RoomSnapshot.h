#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/MathTypes.h"
#include "rrs/game/PlayerEntity.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rrs {

struct PlayerStateSnapshot {
    PlayerId player_id;
    std::uint16_t active_ball_mask{0};
    std::array<PlayerBall, kMaxBallsPerPlayer> balls{};
};

struct FoodStateSnapshot {
    FoodId food_id;
    Vector2 position;
};

struct RoomSnapshot {
    TickSeq tick_seq{0};
    std::vector<PlayerStateSnapshot> players;
    std::vector<FoodStateSnapshot> foods;
    PlayerId winner_player_id;
};

} // namespace rrs
