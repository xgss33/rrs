#include "rrs/simulation/PlayerVisibilityTracker.h"

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomRules.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace rrs {

namespace {

bool WasPlayerBallVisible(const PlayerVisibilitySet* previous, PlayerId player_id, std::size_t ball_index)
{
    if (previous == nullptr) {
        return false;
    }

    const auto iterator = std::lower_bound(
        previous->players.begin(),
        previous->players.end(),
        player_id,
        [](const VisiblePlayerBallMask& player, PlayerId id) {
            return player.player_id < id;
        });
    return iterator != previous->players.end()
        && iterator->player_id == player_id
        && (iterator->ball_mask & static_cast<std::uint16_t>(1U << ball_index)) != 0;
}

bool IsVisibleFromBall(
    const PlayerBall& observer_ball,
    Vector2 target_position,
    float target_radius,
    float visibility_distance)
{
    const auto center_distance = observer_ball.radius + target_radius + visibility_distance;
    return DistanceSquared(observer_ball.position, target_position) <= center_distance * center_distance;
}

} // namespace

const PlayerVisibilitySet& PlayerVisibilityTracker::UpdateForObserver(
    std::size_t observer_player_index,
    std::span<const PlayerEntity> players,
    PlayerBallSpatialIndex& player_ball_spatial_index)
{
    const auto& observer = players[observer_player_index];
    auto iterator = visibility_by_observer_.find(observer.player_id);
    const auto* previous = iterator != visibility_by_observer_.end()
        ? &iterator->second
        : nullptr;

    working_ball_masks_.assign(players.size(), 0);
    working_ball_masks_[observer_player_index] = observer.active_ball_mask;

    for (std::size_t observer_ball_index = 0; observer_ball_index < kMaxBallsPerPlayer; ++observer_ball_index) {
        const auto observer_ball_mask = static_cast<std::uint16_t>(1U << observer_ball_index);
        if ((observer.active_ball_mask & observer_ball_mask) == 0) {
            continue;
        }

        const auto& observer_ball = observer.balls[observer_ball_index];
        const auto query_radius = observer_ball.radius + room_rules::kAoiLeaveDistance;

        for (const auto candidate : player_ball_spatial_index.QueryCandidates(observer_ball.position, query_radius)) {
            const auto target_player_index = static_cast<std::size_t>(candidate.player_index);
            if (target_player_index == observer_player_index) {
                continue;
            }

            const auto target_ball_index = static_cast<std::size_t>(candidate.ball_index);
            const auto& target_player = players[target_player_index];
            const auto& target_ball = target_player.balls[target_ball_index];
            const auto visibility_distance = WasPlayerBallVisible(
                previous,
                target_player.player_id,
                target_ball_index)
                ? room_rules::kAoiLeaveDistance
                : room_rules::kAoiEnterDistance;
            if (IsVisibleFromBall(observer_ball, target_ball.position, target_ball.radius, visibility_distance)) {
                working_ball_masks_[target_player_index] = static_cast<std::uint16_t>(
                    working_ball_masks_[target_player_index] | static_cast<std::uint16_t>(1U << target_ball_index));
            }
        }

    }

    auto next = PlayerVisibilitySet{};
    next.players.reserve(players.size());
    for (std::size_t player_index = 0; player_index < players.size(); ++player_index) {
        if (player_index != observer_player_index && working_ball_masks_[player_index] == 0) {
            continue;
        }
        next.players.push_back(VisiblePlayerBallMask{
            .player_id = players[player_index].player_id,
            .ball_mask = working_ball_masks_[player_index],
        });
    }
    std::sort(next.players.begin(), next.players.end(), [](const VisiblePlayerBallMask& left, const VisiblePlayerBallMask& right) {
        return left.player_id < right.player_id;
    });

    if (iterator == visibility_by_observer_.end()) {
        iterator = visibility_by_observer_.emplace(observer.player_id, PlayerVisibilitySet{}).first;
    }
    iterator->second = std::move(next);
    return iterator->second;
}

void PlayerVisibilityTracker::RemoveObserver(PlayerId player_id)
{
    visibility_by_observer_.erase(player_id);
}

} // namespace rrs
