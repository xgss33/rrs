#include "rrs/synchronization/SnapshotDeltaTracker.h"

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomRules.h"
#include "rrs/simulation/RoomVisibility.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rrs {

namespace {

std::int16_t QuantizePosition(float value)
{
    const auto clamped = std::clamp(value, -room_rules::kRoomHalfExtent, room_rules::kRoomHalfExtent);
    const auto normalized = (clamped + room_rules::kRoomHalfExtent) / (room_rules::kRoomHalfExtent * 2.0F);
    const auto encoded = std::lround(normalized * static_cast<float>(std::numeric_limits<std::uint16_t>::max()))
        + static_cast<long>(std::numeric_limits<std::int16_t>::min());
    return static_cast<std::int16_t>(encoded);
}

std::uint16_t QuantizeRadius(float value)
{
    constexpr float kMaximumRadius = 1024.0F;
    const auto clamped = std::clamp(value, 0.0F, kMaximumRadius);
    return static_cast<std::uint16_t>(
        std::lround(clamped / kMaximumRadius * static_cast<float>(std::numeric_limits<std::uint16_t>::max())));
}

QuantizedPosition QuantizePosition(Vector2 position)
{
    return QuantizedPosition{
        .x = QuantizePosition(position.x),
        .y = QuantizePosition(position.y),
    };
}

QuantizedBallState QuantizeBall(const PlayerBall& ball)
{
    return QuantizedBallState{
        .position = QuantizePosition(ball.position),
        .radius = QuantizeRadius(ball.radius),
    };
}

const PlayerEntity& FindPlayer(std::span<const PlayerEntity> players, PlayerId player_id)
{
    return *std::find_if(players.begin(), players.end(), [player_id](const PlayerEntity& player) {
        return player.player_id == player_id;
    });
}

const FoodEntity& FindFood(std::span<const FoodEntity> foods, FoodId food_id)
{
    return foods[static_cast<std::size_t>(food_id.value() - 1)];
}

bool HasPayloadChanges(const SnapshotUpdate& update)
{
    return !update.player_updates.empty()
        || !update.removed_player_ids.empty()
        || !update.food_updates.empty()
        || !update.removed_food_ids.empty()
        || update.winner_player_id.has_value();
}

} // namespace

std::optional<SnapshotUpdate> SnapshotDeltaTracker::BuildUpdate(
    PlayerId observer_player_id,
    TickSeq tick_seq,
    const VisibleEntitySet& visible_entities,
    std::span<const PlayerEntity> players,
    std::span<const FoodEntity> foods,
    std::optional<PlayerId> winner_player_id,
    bool full_reset)
{
    auto current = ObserverSnapshotState{};
    current.players.reserve(visible_entities.players.size());
    for (const auto& visible_player : visible_entities.players) {
        const auto& player = FindPlayer(players, visible_player.player_id);
        auto state = PlayerSnapshotState{
            .player_id = player.player_id,
            .visible_ball_mask = visible_player.ball_mask,
            .balls = {},
        };
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            const auto ball_mask = static_cast<std::uint16_t>(1U << ball_index);
            if ((state.visible_ball_mask & ball_mask) != 0) {
                state.balls[ball_index] = QuantizeBall(player.balls[ball_index]);
            }
        }
        current.players.push_back(state);
    }

    current.foods.reserve(visible_entities.food_ids.size());
    for (const auto food_id : visible_entities.food_ids) {
        const auto& food = FindFood(foods, food_id);
        current.foods.push_back(FoodSnapshotUpdate{
            .food_id = food.food_id,
            .position = QuantizePosition(food.position),
        });
    }
    current.winner_player_id = winner_player_id;

    auto [state_iterator, inserted] = states_by_observer_.try_emplace(observer_player_id);
    auto& previous = state_iterator->second;
    full_reset = full_reset || inserted;

    auto update = SnapshotUpdate{
        .tick_seq = tick_seq,
        .full_reset = full_reset,
        .player_updates = {},
        .removed_player_ids = {},
        .food_updates = {},
        .removed_food_ids = {},
        .winner_player_id = std::nullopt,
    };

    if (full_reset) {
        update.player_updates.reserve(current.players.size());
        for (const auto& player : current.players) {
            update.player_updates.push_back(PlayerSnapshotUpdate{
                .player_id = player.player_id,
                .visible_ball_mask = player.visible_ball_mask,
                .changed_ball_mask = player.visible_ball_mask,
                .balls = player.balls,
            });
        }
        update.food_updates = current.foods;
        update.winner_player_id = current.winner_player_id;
        previous = std::move(current);
        return update;
    }

    std::size_t previous_player_index = 0;
    std::size_t current_player_index = 0;
    while (previous_player_index < previous.players.size() || current_player_index < current.players.size()) {
        if (current_player_index >= current.players.size()
            || (previous_player_index < previous.players.size()
                && previous.players[previous_player_index].player_id < current.players[current_player_index].player_id)) {
            update.removed_player_ids.push_back(previous.players[previous_player_index++].player_id);
            continue;
        }

        const auto& current_player = current.players[current_player_index];
        if (previous_player_index >= previous.players.size()
            || current_player.player_id < previous.players[previous_player_index].player_id) {
            update.player_updates.push_back(PlayerSnapshotUpdate{
                .player_id = current_player.player_id,
                .visible_ball_mask = current_player.visible_ball_mask,
                .changed_ball_mask = current_player.visible_ball_mask,
                .balls = current_player.balls,
            });
            ++current_player_index;
            continue;
        }

        const auto& previous_player = previous.players[previous_player_index];
        auto changed_ball_mask = std::uint16_t{0};
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            const auto ball_mask = static_cast<std::uint16_t>(1U << ball_index);
            if ((current_player.visible_ball_mask & ball_mask) == 0) {
                continue;
            }
            if ((previous_player.visible_ball_mask & ball_mask) == 0
                || current_player.balls[ball_index] != previous_player.balls[ball_index]) {
                changed_ball_mask = static_cast<std::uint16_t>(changed_ball_mask | ball_mask);
            }
        }

        if (current_player.visible_ball_mask != previous_player.visible_ball_mask || changed_ball_mask != 0) {
            update.player_updates.push_back(PlayerSnapshotUpdate{
                .player_id = current_player.player_id,
                .visible_ball_mask = current_player.visible_ball_mask,
                .changed_ball_mask = changed_ball_mask,
                .balls = current_player.balls,
            });
        }
        ++previous_player_index;
        ++current_player_index;
    }

    std::size_t previous_food_index = 0;
    std::size_t current_food_index = 0;
    while (previous_food_index < previous.foods.size() || current_food_index < current.foods.size()) {
        if (current_food_index >= current.foods.size()
            || (previous_food_index < previous.foods.size()
                && previous.foods[previous_food_index].food_id < current.foods[current_food_index].food_id)) {
            update.removed_food_ids.push_back(previous.foods[previous_food_index++].food_id);
            continue;
        }

        const auto& current_food = current.foods[current_food_index];
        if (previous_food_index >= previous.foods.size()
            || current_food.food_id < previous.foods[previous_food_index].food_id) {
            update.food_updates.push_back(current_food);
            ++current_food_index;
            continue;
        }

        if (current_food != previous.foods[previous_food_index]) {
            update.food_updates.push_back(current_food);
        }
        ++previous_food_index;
        ++current_food_index;
    }

    if (current.winner_player_id != previous.winner_player_id) {
        update.winner_player_id = current.winner_player_id;
    }

    previous = std::move(current);
    if (!HasPayloadChanges(update)) {
        return std::nullopt;
    }
    return update;
}

void SnapshotDeltaTracker::RemoveObserver(PlayerId observer_player_id)
{
    states_by_observer_.erase(observer_player_id);
}

} // namespace rrs
