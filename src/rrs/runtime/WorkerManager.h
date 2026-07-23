#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/simulation/Room.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace rrs {

enum class SessionState {
    kJoining,
    kActive,
};

struct Session {
    SessionId id;
    PlayerId player_id;
    SessionState state{SessionState::kJoining};
    std::optional<ConnectionHandle> connection;
};

struct RoomMetrics {
    std::size_t static_entities{0};
    std::size_t dynamic_entities{0};
    std::size_t visibility_observers{0};
    std::size_t visible_other_player_balls{0};
};

class WorkerManager {
public:
    WorkerManager(WorkerId worker_id, std::size_t worker_count);

    Session* FindSession(SessionId session_id);
    Session* FindSessionByPlayer(PlayerId player_id);
    Session* FindSessionByConnection(ConnectionHandle connection);
    Room& RoomFor(SessionId session_id);

    SessionId Join(PlayerId player_id, ConnectionHandle connection, Room::Clock::time_point entered_at);
    [[nodiscard]] std::optional<ConnectionHandle> Bind(Session& session, ConnectionHandle connection);
    void EnqueueInput(ConnectionHandle connection, PlayerInput input, Room::Clock::time_point entered_at);
    void Leave(SessionId session_id, Room::Clock::time_point entered_at);
    void Disconnect(SessionId session_id, Room::Clock::time_point entered_at);

    template <typename HandleTick>
    bool TickDueRooms(Room::Clock::time_point now, HandleTick handle_tick)
    {
        constexpr std::uint32_t kMaxCatchUpTicks = 2;
        auto room_ticked = false;
        auto room_iterator = rooms_.begin();
        while (room_iterator != rooms_.end()) {
            const auto room_id = room_iterator->first;
            auto room_erased = false;

            std::uint32_t catch_up_count = 0;
            while (room_iterator->second.room.next_tick_time() <= now
                   && catch_up_count < kMaxCatchUpTicks) {
                room_ticked = true;
                const auto scheduled_tick_time = room_iterator->second.room.next_tick_time();
                auto result = room_iterator->second.room.Tick();

                for (const auto player_id : result.joined_players) {
                    const auto session_id = session_by_player_.find(player_id);
                    if (session_id == session_by_player_.end()) {
                        continue;
                    }
                    if (room_by_session_.at(session_id->second) == room_id) {
                        FindSessionInRoom(room_iterator->second, session_id->second)->state = SessionState::kActive;
                    }
                }

                UpdateRoomOpenState(room_id);
                handle_tick(
                    room_iterator->second.room,
                    std::span<const Session>{room_iterator->second.sessions},
                    result,
                    scheduled_tick_time);

                if (result.match_ended
                    || (room_iterator->second.room.player_count() == 0
                        && room_iterator->second.sessions.empty())) {
                    RemoveRoom(room_id);
                    room_erased = true;
                    break;
                }

                ++catch_up_count;
                now = Room::Clock::now();
            }

            if (room_erased) {
                room_iterator = rooms_.upper_bound(room_id);
            } else {
                ++room_iterator;
            }
        }
        return room_ticked;
    }

    Room::Clock::time_point NextWakeTime(Room::Clock::time_point fallback) const;
    [[nodiscard]] RoomMetrics CollectMetrics() const;

private:
    struct RoomState {
        Room room;
        std::vector<Session> sessions;
    };

    RoomState& AssignRoomForJoin();
    RoomState& CreateRoom();
    static Session* FindSessionInRoom(RoomState& room, SessionId session_id);
    void RemoveSession(SessionId session_id, Room::Clock::time_point entered_at);
    void RemoveRoom(RoomId room_id);
    void UpdateRoomOpenState(RoomId room_id);

    WorkerId worker_id_;
    std::size_t worker_count_;
    std::uint64_t next_local_session_sequence_{0};
    std::uint64_t next_local_room_sequence_{0};
    std::map<RoomId, RoomState> rooms_;
    std::vector<RoomId> open_room_ids_;
    std::unordered_map<SessionId, RoomId> room_by_session_;
    std::unordered_map<PlayerId, SessionId> session_by_player_;
    std::unordered_map<ConnectionHandle, SessionId> session_by_connection_;
};

[[nodiscard]] WorkerId GetSessionWorker(SessionId session_id, std::size_t worker_count);

} // namespace rrs
