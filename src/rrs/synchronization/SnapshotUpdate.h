#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

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

struct PlayerSnapshotUpdate {
    PlayerId player_id;
    std::uint16_t visible_ball_mask{0};
    std::uint16_t changed_ball_mask{0};
    std::array<QuantizedBallState, kMaxBallsPerPlayer> balls{};
};

struct FoodSnapshotUpdate {
    FoodIndex food_index{0};
    QuantizedPosition position;

    friend bool operator==(const FoodSnapshotUpdate&, const FoodSnapshotUpdate&) = default;
};

struct SnapshotUpdate {
    TickSeq tick_seq{0};
    bool full_reset{false};
    std::vector<PlayerSnapshotUpdate> player_updates;
    std::vector<PlayerId> removed_player_ids;
    std::optional<PlayerId> winner_player_id;
};

QuantizedPosition QuantizeSnapshotPosition(Vector2 position);
QuantizedBallState QuantizeSnapshotBall(const PlayerBall& ball);

} // namespace rrs
