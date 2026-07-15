#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/FoodSpatialIndex.h"
#include "rrs/game/MathTypes.h"
#include "rrs/game/PlayerInput.h"
#include "rrs/game/PlayerEntity.h"
#include "rrs/game/RoomSnapshot.h"
#include "rrs/runtime/Session.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <random>
#include <vector>

namespace rrs {

class Room {
public:
    using Clock = std::chrono::steady_clock;

    Room(RoomId room_id, Clock::time_point first_tick_time, std::chrono::nanoseconds tick_interval);

    [[nodiscard]] RoomId id() const noexcept { return room_id_; }
    [[nodiscard]] Clock::time_point next_tick_time() const noexcept { return next_tick_time_; }
    [[nodiscard]] std::size_t entity_count() const noexcept { return players_.size(); }

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

    struct TickResult {
        RoomSnapshot broadcast_snapshot;
        std::optional<RoomSnapshot> full_snapshot;
        std::vector<Event> events;
    };

    void EnqueueCommand(Command command);
    [[nodiscard]] TickResult Tick();

private:
    struct AggregatedPlayerInput {
        PlayerId player_id;
        PlayerInput input;
    };

    void InitializeFood();

    [[nodiscard]] std::vector<Command> TakeCommandsForTick(Clock::time_point tick_start);
    [[nodiscard]] std::vector<AggregatedPlayerInput> ProcessCommands(const std::vector<Command>& commands, TickResult& result);
    void JoinPlayer(const Session& session, TickResult& result);
    void ReconnectPlayer(const Session& session, TickResult& result);
    void ApplyPlayerInputs(const std::vector<AggregatedPlayerInput>& inputs);
    void LeavePlayer(const Session& session, TickResult& result);

    void SplitPlayers(const std::vector<AggregatedPlayerInput>& inputs);
    void SplitPlayer(PlayerEntity& player);
    void MovePlayers();
    void ResolveFoodCollisions();
    void ResolvePlayerBallEating();
    void TryEatBall(PlayerEntity& attacker,
                    std::size_t attacker_ball_index,
                    PlayerEntity& victim,
                    std::size_t victim_ball_index);
    void RespawnDuePlayers();
    void UpdateMatchState();

    [[nodiscard]] RoomSnapshot BuildBroadcastSnapshot();
    [[nodiscard]] RoomSnapshot BuildFullSnapshot() const;
    [[nodiscard]] RoomSnapshot BuildSnapshotWithPlayers() const;

    [[nodiscard]] float CalculateBallSpeed(float radius) const;
    [[nodiscard]] Vector2 FindSpawnPosition();
    [[nodiscard]] PlayerEntity* FindPlayer(PlayerId player_id);

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
    FoodSpatialIndex foods_;
    std::vector<Command> pending_commands_;
};

} // namespace rrs
