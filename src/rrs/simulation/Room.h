#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/runtime/Session.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/PlayerInput.h"
#include "rrs/simulation/RoomVisibility.h"
#include "rrs/simulation/spatial/FoodSpatialIndex.h"
#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"
#include "rrs/spatial/UniformGrid.h"
#include "rrs/synchronization/SnapshotDeltaTracker.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace rrs {

class Room {
public:
    using Clock = std::chrono::steady_clock;

    Room(RoomId room_id, Clock::time_point first_tick_time, std::chrono::nanoseconds tick_interval);

    RoomId id() const { return room_id_; }
    Clock::time_point next_tick_time() const { return next_tick_time_; }
    std::size_t player_count() const { return players_.size(); }

    enum class CommandType {
        kJoin,
        kReconnect,
        kPlayerInput,
        kLeave,
    };

    struct Command {
        CommandType type{CommandType::kPlayerInput};
        Session session;
        PlayerInput input;
        Clock::time_point entered_at;
    };

    enum class EventType {
        kJoinAccepted,
        kReconnectAccepted,
        kReconnectRejected,
        kPlayerLeft,
    };

    struct Event {
        EventType type{EventType::kJoinAccepted};
        Session session;
    };

    struct ObserverSnapshotUpdate {
        PlayerId observer_player_id;
        SnapshotUpdate update;
    };

    struct TickResult {
        std::vector<ObserverSnapshotUpdate> snapshot_updates;
        std::vector<Event> events;
    };

    void EnqueueCommand(Command command);
    [[nodiscard]] TickResult Tick();

private:
    struct AggregatedPlayerInput {
        PlayerId player_id;
        PlayerInput input;
    };

    static void ClampBallPosition(PlayerBall& ball);
    static UniformGridLayout MakeRoomSpatialGridLayout();

    void InitializeFoods();

    [[nodiscard]] std::vector<Command> TakeCommandsForTick(Clock::time_point tick_start);
    [[nodiscard]] std::vector<AggregatedPlayerInput> ProcessCommands(const std::vector<Command>& commands, TickResult& result);
    void JoinPlayer(const Session& session, TickResult& result);
    void ReconnectPlayer(const Session& session, TickResult& result);
    void ApplyPlayerInputs(const std::vector<AggregatedPlayerInput>& inputs);
    void LeavePlayer(const Session& session, TickResult& result);

    void SplitPlayers(const std::vector<AggregatedPlayerInput>& inputs);
    void SplitPlayer(PlayerEntity& player);
    void MovePlayers();
    void ResolveFoodEating();
    void ResolvePlayerBallEating();
    [[nodiscard]] bool TryEatPlayerBall(PlayerEntity& attacker,
                                        std::size_t attacker_ball_index,
                                        PlayerEntity& victim,
                                        std::size_t victim_ball_index);
    void RespawnDuePlayers();
    void UpdateMatchState();

    [[nodiscard]] std::vector<ObserverSnapshotUpdate> BuildSnapshotUpdates(const std::vector<Event>& events);

    float CalculateBallSpeed(float radius) const;
    Vector2 FindSpawnPosition();
    PlayerEntity* FindPlayer(PlayerId player_id);

    RoomId room_id_;
    TickSeq tick_seq_{0};
    Clock::time_point next_tick_time_;
    std::chrono::nanoseconds tick_interval_;
    TickSeq match_duration_ticks_{0};
    TickSeq respawn_delay_ticks_{0};
    bool match_over_{false};
    PlayerId winner_player_id_;
    std::mt19937 rng_;
    std::vector<PlayerEntity> players_;
    std::vector<FoodEntity> foods_;
    FoodSpatialIndex food_spatial_index_;
    PlayerBallSpatialIndex player_ball_spatial_index_;
    RoomVisibility room_visibility_;
    SnapshotDeltaTracker snapshot_delta_tracker_;
    std::vector<Command> pending_commands_;
};

} // namespace rrs
