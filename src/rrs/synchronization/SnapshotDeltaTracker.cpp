#include "rrs/synchronization/SnapshotDeltaTracker.h"

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/PlayerVisibilityTracker.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rrs {

namespace {

const PlayerEntity& FindPlayer(std::span<const PlayerEntity> players, PlayerId player_id)
{
    return *std::find_if(players.begin(), players.end(), [player_id](const PlayerEntity& player) {
        return player.player_id == player_id;
    });
}

bool HasPayloadChanges(const SnapshotUpdate& update)
{
    return !update.player_updates.empty()
        || !update.removed_player_ids.empty()
        || update.winner_player_id.has_value();
}

} // namespace

std::optional<SnapshotUpdate> SnapshotDeltaTracker::BuildUpdate(
    PlayerId observer_player_id,
    TickSeq tick_seq,
    const PlayerVisibilitySet& player_visibility,
    std::span<const PlayerEntity> players,
    std::optional<PlayerId> winner_player_id,
    bool full_reset)
{
    auto current = ObserverSnapshotState{};
    current.players.reserve(player_visibility.players.size());
    for (const auto& visible_player : player_visibility.players) {
        const auto& player = FindPlayer(players, visible_player.player_id);
        auto state = PlayerSnapshotState{
            .player_id = player.player_id,
            .visible_ball_mask = visible_player.ball_mask,
            .balls = {},
        };
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            const auto ball_mask = static_cast<std::uint16_t>(1U << ball_index);
            if ((state.visible_ball_mask & ball_mask) != 0) {
                state.balls[ball_index] = QuantizeSnapshotBall(player.balls[ball_index]);
            }
        }
        current.players.push_back(state);
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
