#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace rrs {

struct VisiblePlayerBallMask {
    PlayerId player_id;
    std::uint16_t ball_mask{0};
};

struct PlayerVisibilitySet {
    std::vector<VisiblePlayerBallMask> players;
};

class PlayerVisibilityTracker {
public:
    [[nodiscard]] const PlayerVisibilitySet& UpdateForObserver(
        std::size_t observer_player_index,
        std::span<const PlayerEntity> players,
        PlayerBallSpatialIndex& player_ball_spatial_index);

    void RemoveObserver(PlayerId player_id);

private:
    std::unordered_map<PlayerId, PlayerVisibilitySet> visibility_by_observer_;  // 保存每位观察者上一Tick可见球
    std::vector<std::uint16_t> working_ball_masks_;
};

} // namespace rrs
