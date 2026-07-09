#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/MathTypes.h"

namespace rrs {

struct PlayerEntity {
    PlayerId player_id;
    Vector2 position;
    Vector2 input_direction;
    float radius{12.0F};
    TickSeq respawn_tick{0};
};

[[nodiscard]] inline bool IsAlive(const PlayerEntity& player) noexcept
{
    return player.respawn_tick == 0;
}

} // namespace rrs
