#include "rrs/synchronization/SnapshotUpdate.h"

#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomRules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rrs {

namespace {

std::int16_t QuantizePositionComponent(float value)
{
    const auto clamped = std::clamp(value, -room_rules::kRoomHalfExtent, room_rules::kRoomHalfExtent);
    const auto normalized = (clamped + room_rules::kRoomHalfExtent) / (room_rules::kRoomHalfExtent * 2.0F);
    const auto encoded = std::lround(normalized * static_cast<float>(std::numeric_limits<std::uint16_t>::max()))
        + static_cast<long>(std::numeric_limits<std::int16_t>::min());
    return static_cast<std::int16_t>(encoded);
}

std::uint16_t QuantizeRadius(float value)
{
    constexpr float kMaximumRadius = 1024.0F;
    const auto clamped = std::clamp(value, 0.0F, kMaximumRadius);
    return static_cast<std::uint16_t>(
        std::lround(clamped / kMaximumRadius * static_cast<float>(std::numeric_limits<std::uint16_t>::max())));
}

} // namespace

QuantizedPosition QuantizeSnapshotPosition(Vector2 position)
{
    return QuantizedPosition{
        .x = QuantizePositionComponent(position.x),
        .y = QuantizePositionComponent(position.y),
    };
}

QuantizedBallState QuantizeSnapshotBall(const PlayerBall& ball)
{
    return QuantizedBallState{
        .position = QuantizeSnapshotPosition(ball.position),
        .radius = QuantizeRadius(ball.radius),
    };
}

} // namespace rrs
