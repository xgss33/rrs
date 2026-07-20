#pragma once

#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/spatial/UniformGrid.h"
#include "rrs/spatial/UniformGridAabbIndex.h"

#include <cstdint>
#include <span>
#include <vector>

namespace rrs {

struct PlayerBallLocator {
    std::uint32_t player_index{0};
    std::uint8_t ball_index{0};
};

class PlayerBallSpatialIndex {
public:
    explicit PlayerBallSpatialIndex(UniformGridLayout layout);

    void Rebuild(std::span<const PlayerEntity> players);
    [[nodiscard]] std::span<const PlayerBallLocator> QueryCandidates(Vector2 center, float radius);
    [[nodiscard]] std::span<const PlayerBallLocator> QueryCandidates(std::span<const Aabb> query_bounds);

private:
    UniformGridAabbIndex grid_;
    std::vector<Aabb> ball_bounds_;
    std::vector<PlayerBallLocator> ball_locators_;
    std::vector<PlayerBallLocator> candidate_ball_locators_;

public:
    std::size_t indexed_ball_count() const { return ball_locators_.size(); }
};

} // namespace rrs
