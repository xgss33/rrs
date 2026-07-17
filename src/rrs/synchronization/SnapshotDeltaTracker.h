#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomVisibility.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace rrs {

class SnapshotDeltaTracker {
public:
    [[nodiscard]] std::optional<SnapshotUpdate> BuildUpdate(
        PlayerId observer_player_id,
        TickSeq tick_seq,
        const VisibleEntitySet& visible_entities,
        std::span<const PlayerEntity> players,
        std::span<const FoodEntity> foods,
        std::optional<PlayerId> winner_player_id,
        bool full_reset);

    void RemoveObserver(PlayerId observer_player_id);

private:
    struct PlayerSnapshotState {
        PlayerId player_id;
        std::uint16_t visible_ball_mask{0};
        std::array<QuantizedBallState, kMaxBallsPerPlayer> balls{};
    };

    struct ObserverSnapshotState {
        std::vector<PlayerSnapshotState> players;
        std::vector<FoodSnapshotUpdate> foods;
        std::optional<PlayerId> winner_player_id;
    };

    std::unordered_map<PlayerId, ObserverSnapshotState> states_by_observer_;
};

} // namespace rrs
