#include "rrs/simulation/RoomVisibility.h"

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomRules.h"
#include "rrs/simulation/spatial/FoodSpatialIndex.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace rrs {

namespace {

bool WasPlayerBallVisible(const VisibleEntitySet* previous, PlayerId player_id, std::size_t ball_index)
{
    if (previous == nullptr) {
        return false;
    }

    const auto iterator = std::lower_bound(
        previous->players.begin(),
        previous->players.end(),
        player_id,
        [](const VisiblePlayerBalls& player, PlayerId id) {
            return player.player_id < id;
        });
    return iterator != previous->players.end()
        && iterator->player_id == player_id
        && (iterator->ball_mask & static_cast<std::uint16_t>(1U << ball_index)) != 0;
}

bool WasFoodVisible(const VisibleEntitySet* previous, FoodId food_id)
{
    return previous != nullptr
        && std::binary_search(previous->food_ids.begin(), previous->food_ids.end(), food_id);
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

const VisibleEntitySet& RoomVisibility::Update(
    std::size_t observer_player_index,
    std::span<const PlayerEntity> players,
    std::span<const FoodEntity> foods,
    PlayerBallSpatialIndex& player_ball_spatial_index,
    FoodSpatialIndex& food_spatial_index)
{
    const auto& observer = players[observer_player_index];
    const auto previous_iterator = visible_entities_by_observer_.find(observer.player_id);
    const auto* previous = previous_iterator != visible_entities_by_observer_.end()
        ? &previous_iterator->second
        : nullptr;

    visible_ball_masks_.assign(players.size(), 0);
    visible_food_flags_.assign(foods.size(), 0);
    visible_ball_masks_[observer_player_index] = observer.active_ball_mask;

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
                visible_ball_masks_[target_player_index] = static_cast<std::uint16_t>(
                    visible_ball_masks_[target_player_index] | static_cast<std::uint16_t>(1U << target_ball_index));
            }
        }

        for (const auto food_index : food_spatial_index.QueryCandidates(observer_ball.position, query_radius)) {
            const auto& food = foods[food_index];
            const auto visibility_distance = WasFoodVisible(previous, food.food_id)
                ? room_rules::kAoiLeaveDistance
                : room_rules::kAoiEnterDistance;
            if (IsVisibleFromBall(observer_ball, food.position, room_rules::kFoodRadius, visibility_distance)) {
                visible_food_flags_[food_index] = 1;
            }
        }
    }

    auto next = VisibleEntitySet{};
    next.players.reserve(players.size());
    for (std::size_t player_index = 0; player_index < players.size(); ++player_index) {
        if (player_index != observer_player_index && visible_ball_masks_[player_index] == 0) {
            continue;
        }
        next.players.push_back(VisiblePlayerBalls{
            .player_id = players[player_index].player_id,
            .ball_mask = visible_ball_masks_[player_index],
        });
    }
    std::sort(next.players.begin(), next.players.end(), [](const VisiblePlayerBalls& left, const VisiblePlayerBalls& right) {
        return left.player_id < right.player_id;
    });

    next.food_ids.reserve(foods.size());
    for (std::size_t food_index = 0; food_index < foods.size(); ++food_index) {
        if (visible_food_flags_[food_index] != 0) {
            next.food_ids.push_back(foods[food_index].food_id);
        }
    }
    std::sort(next.food_ids.begin(), next.food_ids.end());

    auto iterator = visible_entities_by_observer_.find(observer.player_id);
    if (iterator == visible_entities_by_observer_.end()) {
        iterator = visible_entities_by_observer_.emplace(observer.player_id, VisibleEntitySet{}).first;
    }
    iterator->second = std::move(next);
    return iterator->second;
}

void RoomVisibility::RemoveObserver(PlayerId player_id)
{
    visible_entities_by_observer_.erase(player_id);
}

} // namespace rrs
