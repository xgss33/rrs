#include "rrs/simulation/PlayerVisibilityTracker.h"

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomRules.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"
#include "rrs/spatial/UniformGrid.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

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
    // 先尝试取得当前观察者上一Tick的可见性
    const auto& observer = players[observer_player_index];
    auto [iterator, inserted] = visibility_by_observer_.try_emplace(observer.player_id);
    auto& visibility = iterator->second;
    const auto* previous = inserted ? nullptr : &visibility;

    // 建立本Tick的临时球掩码
    working_ball_masks_.assign(players.size(), 0);
    working_ball_masks_[observer_player_index] = observer.active_ball_mask;

    // 遍历观察者的每个活跃球
    auto observer_query_bounds = std::array<Aabb, kMaxBallsPerPlayer>{};
    std::size_t query_bound_count = 0;
    for (std::size_t observer_ball_index = 0; observer_ball_index < kMaxBallsPerPlayer; ++observer_ball_index) {
        const auto observer_ball_mask = static_cast<std::uint16_t>(1U << observer_ball_index);
        if ((observer.active_ball_mask & observer_ball_mask) == 0) {
            continue;
        }

        const auto& observer_ball = observer.balls[observer_ball_index];
        const auto query_radius = observer_ball.radius + room_rules::kAoiLeaveDistance;
        observer_query_bounds[query_bound_count++] = AabbForCircle(observer_ball.position, query_radius);
    }

    // 所有活跃球范围共享一次查询, 相同目标球只会进入一次候选集合
    const auto query_bounds = std::span<const Aabb>{observer_query_bounds.data(), query_bound_count};
    for (const auto candidate : player_ball_spatial_index.QueryCandidates(query_bounds)) {
        const auto target_player_index = static_cast<std::size_t>(candidate.player_index);
        if (target_player_index == observer_player_index) {
            continue;
        }

        const auto target_ball_index = static_cast<std::size_t>(candidate.ball_index);
        const auto target_ball_mask = static_cast<std::uint16_t>(1U << target_ball_index);
        const auto& target_player = players[target_player_index];
        const auto& target_ball = target_player.balls[target_ball_index];
        const auto visibility_distance = WasPlayerBallVisible(
            previous,
            target_player.player_id,
            target_ball_index)
            ? room_rules::kAoiLeaveDistance
            : room_rules::kAoiEnterDistance;

        for (std::size_t observer_ball_index = 0; observer_ball_index < kMaxBallsPerPlayer; ++observer_ball_index) {
            const auto observer_ball_mask = static_cast<std::uint16_t>(1U << observer_ball_index);
            if ((observer.active_ball_mask & observer_ball_mask) == 0) {
                continue;
            }

            // 精筛
            const auto& observer_ball = observer.balls[observer_ball_index];
            if (IsVisibleFromBall(observer_ball, target_ball.position, target_ball.radius, visibility_distance)) {
                working_ball_masks_[target_player_index] = static_cast<std::uint16_t>(
                    working_ball_masks_[target_player_index] | target_ball_mask);
                break;
            }
        }
    }

    visibility.players.clear();
    visibility.players.reserve(players.size());
    for (std::size_t player_index = 0; player_index < players.size(); ++player_index) {
        if (player_index != observer_player_index && working_ball_masks_[player_index] == 0) {
            continue;
        }
        visibility.players.push_back(VisiblePlayerBallMask{
            .player_id = players[player_index].player_id,
            .ball_mask = working_ball_masks_[player_index],
        });
    }
    std::sort(visibility.players.begin(), visibility.players.end(), [](const VisiblePlayerBallMask& left, const VisiblePlayerBallMask& right) {
        return left.player_id < right.player_id;
    });

    return visibility;
}

void PlayerVisibilityTracker::RemoveObserver(PlayerId player_id)
{
    visibility_by_observer_.erase(player_id);
}

} // namespace rrs
