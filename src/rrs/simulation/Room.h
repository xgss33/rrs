#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/PlayerInput.h"
#include "rrs/simulation/PlayerVisibilityTracker.h"
#include "rrs/simulation/spatial/FoodSpatialIndex.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"
#include "rrs/synchronization/SnapshotDeltaTracker.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace rrs {

class Room {
public:
    using Clock = std::chrono::steady_clock;

    Room(RoomId room_id, Clock::time_point first_tick_time);

    RoomId id() const { return room_id_; }
    Clock::time_point next_tick_time() const { return next_tick_time_; }
    std::size_t player_count() const { return players_.size(); }
    bool accepts_joins() const { return !match_over_; }

    enum class CommandType {
        kJoin,
        kPlayerInput,
        kLeave,
    };

    struct Command {
        CommandType type{CommandType::kPlayerInput};
        PlayerId player_id;
        PlayerInput input;
        Clock::time_point entered_at;
    };

    struct TickResult {
        std::vector<FoodSnapshotUpdate> food_updates;
        std::vector<PlayerId> joined_players;
        bool match_ended{false};
    };

    void EnqueueCommand(Command command);
    [[nodiscard]] TickResult Tick();
    [[nodiscard]] std::optional<SnapshotUpdate> BuildSnapshot(
        PlayerId observer_player_id,
        bool force_full_reset,
        bool has_food_updates);
    [[nodiscard]] std::vector<FoodSnapshotUpdate> BuildFoodSnapshotBaseline() const;
    void RemoveSnapshotObserver(PlayerId player_id);

private:
    struct AggregatedPlayerInput {
        PlayerId player_id;
        PlayerInput input;
    };

    static void ClampBallPosition(PlayerBall& ball);

    void InitializeFoods();

    [[nodiscard]] std::vector<Command> TakeCommandsForTick(Clock::time_point tick_start);
    [[nodiscard]] std::vector<AggregatedPlayerInput> ProcessCommands(const std::vector<Command>& commands, TickResult& result);
    void JoinPlayer(PlayerId player_id, TickResult& result);
    void ApplyMovementInputs(const std::vector<AggregatedPlayerInput>& inputs);
    void LeavePlayer(PlayerId player_id);

    void ApplySplitInputs(const std::vector<AggregatedPlayerInput>& inputs);
    void SplitPlayer(PlayerEntity& player);
    void MovePlayerBalls();
    void ResolveFoodEating(std::vector<FoodSnapshotUpdate>& food_updates);
    void ResolvePlayerBallEating();
    [[nodiscard]] bool TryEatPlayerBall(PlayerEntity& attacker,
                                        std::size_t attacker_ball_index,
                                        PlayerEntity& victim,
                                        std::size_t victim_ball_index);
    void RespawnDuePlayers();
    void UpdateMatchState();

    Vector2 FindPlayerSpawnPosition();
    PlayerEntity* FindPlayer(PlayerId player_id);

    RoomId room_id_;
    std::uint64_t tick_seq_{0};
    Clock::time_point next_tick_time_;
    bool match_over_{false};
    PlayerId winner_player_id_;
    std::mt19937 rng_;
    std::vector<PlayerEntity> players_;
    std::vector<FoodEntity> foods_;
    FoodSpatialIndex food_spatial_index_;
    PlayerBallSpatialIndex player_ball_spatial_index_;
    PlayerVisibilityTracker player_visibility_tracker_;
    SnapshotDeltaTracker snapshot_delta_tracker_;
    std::vector<Command> pending_commands_;

public:
    std::size_t static_entity_count() const { return foods_.size(); }
    std::size_t dynamic_entity_count() const;
    std::size_t visibility_observer_count() const { return player_visibility_tracker_.observer_count(); }
    std::size_t visible_other_player_ball_count() const { return visible_other_player_ball_count_; }

private:
    std::size_t visible_other_player_ball_count_{0};
};

} // namespace rrs
