#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/MathTypes.h"

#include <vector>

namespace rrs {

struct FoodEntity;
struct PlayerEntity;

struct PlayerStateSnapshot {
    PlayerId player_id;
    Vector2 position;
    float radius{0.0F};
    bool alive{false};
};

struct FoodStateSnapshot {
    FoodId food_id;
    Vector2 position;
    float radius{0.0F};
};

struct RoomSnapshot {
    RoomId room_id;
    TickSeq tick_seq{0};
    std::vector<PlayerStateSnapshot> players;
    std::vector<FoodStateSnapshot> foods;
    bool match_over{false};
    PlayerId winner_player_id;

    [[nodiscard]] static RoomSnapshot FromRoomState(RoomId room_id,
                                                    TickSeq tick_seq,
                                                    const std::vector<PlayerEntity>& players,
                                                    const std::vector<FoodEntity>& foods,
                                                    bool match_over,
                                                    PlayerId winner_player_id);
};

} // namespace rrs
