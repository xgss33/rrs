#include "rrs/game/RoomSnapshot.h"

#include "rrs/game/FoodEntity.h"
#include "rrs/game/PlayerEntity.h"

namespace rrs {

RoomSnapshot RoomSnapshot::FromRoomState(RoomId room_id,
                                         TickSeq tick_seq,
                                         const std::vector<PlayerEntity>& players,
                                         const std::vector<FoodEntity>& foods,
                                         bool match_over,
                                         PlayerId winner_player_id)
{
    RoomSnapshot snapshot{
        .room_id = room_id,
        .tick_seq = tick_seq,
        .players = {},
        .foods = {},
        .match_over = match_over,
        .winner_player_id = winner_player_id,
    };
    snapshot.players.reserve(players.size());
    snapshot.foods.reserve(foods.size());

    for (const auto& player : players) {
        snapshot.players.push_back(PlayerStateSnapshot{
            .player_id = player.player_id,
            .position = player.position,
            .radius = player.radius,
            .alive = IsAlive(player),
        });
    }

    for (const auto& food : foods) {
        snapshot.foods.push_back(FoodStateSnapshot{
            .food_id = food.food_id,
            .position = food.position,
            .radius = food.radius,
        });
    }

    return snapshot;
}

} // namespace rrs
