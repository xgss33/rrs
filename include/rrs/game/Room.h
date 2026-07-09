#pragma once

#include "rrs/base/Types.h"
#include "rrs/runtime/IoToWorkerMessage.h"
#include "rrs/game/FoodEntity.h"
#include "rrs/game/MathTypes.h"
#include "rrs/game/PlayerEntity.h"
#include "rrs/game/RoomSnapshot.h"
#include "rrs/runtime/Session.h"

#include <chrono>
#include <cstddef>
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
        RoomSnapshot snapshot;
        std::vector<Event> events;
    };

    void EnqueueCommand(Command command);
    [[nodiscard]] TickResult Tick();

private:
    void InitializeFood();
    [[nodiscard]] std::vector<Command> TakeCommandsForTick(Clock::time_point tick_start);
    void ProcessCommands(const std::vector<Command>& commands, TickResult& result);
    void JoinPlayer(const Session& session, TickResult& result);
    void ReconnectPlayer(const Session& session, TickResult& result);
    void ApplyPlayerInput(const Session& session, const PlayerInput& input);
    void LeavePlayer(const Session& session, TickResult& result);

    void RespawnDuePlayers();
    void MovePlayers();
    void ResolveFoodCollisions();
    void ResolvePlayerCollisions();
    void TryEatPlayer(PlayerEntity& attacker, PlayerEntity& victim);
    void UpdateMatchState();

    [[nodiscard]] float CalcSpeed(float radius) const;
    [[nodiscard]] Vector2 RandomPosition(float radius);
    [[nodiscard]] Vector2 FindRespawnPosition();
    [[nodiscard]] RoomSnapshot BuildSnapshot() const;
    [[nodiscard]] PlayerEntity* FindPlayer(PlayerId player_id);
    [[nodiscard]] const PlayerEntity* FindPlayer(PlayerId player_id) const;

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
    std::vector<Command> pending_commands_;
};

} // namespace rrs
