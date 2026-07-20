#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"

#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/spatial/UniformGrid.h"
#include "rrs/spatial/UniformGridAabbIndex.h"

#include <algorithm>
#include <cstddef>

namespace rrs {

PlayerBallSpatialIndex::PlayerBallSpatialIndex(UniformGridLayout layout)
    : grid_(layout)
{
}

void PlayerBallSpatialIndex::Rebuild(std::span<const PlayerEntity> players)
{
    ball_bounds_.clear();
    ball_locators_.clear();

    for (std::size_t player_index = 0; player_index < players.size(); ++player_index) {
        const auto& player = players[player_index];
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            const auto ball_mask = static_cast<std::uint16_t>(1U << ball_index);
            if ((player.active_ball_mask & ball_mask) == 0) {
                continue;
            }

            const auto& ball = player.balls[ball_index];
            ball_bounds_.push_back(AabbForCircle(ball.position, ball.radius));
            ball_locators_.push_back(PlayerBallLocator{
                .player_index = static_cast<std::uint32_t>(player_index),
                .ball_index = static_cast<std::uint8_t>(ball_index),
            });
        }
    }

    candidate_ball_locators_.reserve(ball_locators_.size());
    grid_.Rebuild(ball_bounds_);
}

std::span<const PlayerBallLocator> PlayerBallSpatialIndex::QueryCandidates(Vector2 center, float radius)
{
    const auto query_bounds = AabbForCircle(center, radius);
    return QueryCandidates(std::span<const Aabb>{&query_bounds, 1});
}

std::span<const PlayerBallLocator> PlayerBallSpatialIndex::QueryCandidates(std::span<const Aabb> query_bounds)
{
    const auto candidate_record_indices = grid_.QueryCandidates(query_bounds);

    candidate_ball_locators_.clear();
    for (const auto record_index : candidate_record_indices) {
        candidate_ball_locators_.push_back(ball_locators_[record_index]);
    }

    std::sort(
        candidate_ball_locators_.begin(),
        candidate_ball_locators_.end(),
        [](const PlayerBallLocator& left, const PlayerBallLocator& right) {
            if (left.player_index != right.player_index) {
                return left.player_index < right.player_index;
            }
            return left.ball_index < right.ball_index;
        });
    return candidate_ball_locators_;
}

} // namespace rrs
