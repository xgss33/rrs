#pragma once

#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"

#include <cstdint>

namespace rrs {

struct QuantizedPosition {
    std::int16_t x{0};
    std::int16_t y{0};

    friend bool operator==(const QuantizedPosition&, const QuantizedPosition&) = default;
};

struct QuantizedBallState {
    QuantizedPosition position;
    std::uint16_t radius{0};

    friend bool operator==(const QuantizedBallState&, const QuantizedBallState&) = default;
};

QuantizedPosition QuantizeSnapshotPosition(Vector2 position);
QuantizedBallState QuantizeSnapshotBall(const PlayerBall& ball);

} // namespace rrs
