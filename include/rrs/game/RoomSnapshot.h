#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/MathTypes.h"

#include <vector>

namespace rrs {

struct PlayerStateSnapshot {
    PlayerId player_id;
    Vector2 position;
    float radius{0.0F};
    bool alive{false};
};

struct FoodStateSnapshot {
    FoodId food_id;
    Vector2 position;
};

struct RoomSnapshot {
    RoomId room_id;
    TickSeq tick_seq{0};
    std::vector<PlayerStateSnapshot> players;
    std::vector<FoodStateSnapshot> foods;
    bool match_over{false};
    PlayerId winner_player_id;
};

} // namespace rrs
