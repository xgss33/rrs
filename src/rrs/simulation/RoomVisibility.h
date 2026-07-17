#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/spatial/FoodSpatialIndex.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace rrs {

struct VisiblePlayerBalls {
    PlayerId player_id;
    std::uint16_t ball_mask{0};
};

struct VisibleEntitySet {
    std::vector<VisiblePlayerBalls> players;
    std::vector<FoodId> food_ids;
};

class RoomVisibility {
public:
    [[nodiscard]] const VisibleEntitySet& Update(
        std::size_t observer_player_index,
        std::span<const PlayerEntity> players,
        std::span<const FoodEntity> foods,
        PlayerBallSpatialIndex& player_ball_spatial_index,
        FoodSpatialIndex& food_spatial_index);

    void RemoveObserver(PlayerId player_id);

private:
    std::unordered_map<PlayerId, VisibleEntitySet> visible_entities_by_observer_;
    std::vector<std::uint16_t> visible_ball_masks_;
    std::vector<std::uint8_t> visible_food_flags_;
};

} // namespace rrs
