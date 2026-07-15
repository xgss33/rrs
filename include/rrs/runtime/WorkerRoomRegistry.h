#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/Room.h"
#include "rrs/runtime/Session.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace rrs {

struct WorkerSessionBinding {
    SessionId session_id;
    Generation generation{1};
    PlayerId player_id;
    IoThreadId io_thread_id;
    RoomId room_id;
    bool active{false};
};

class WorkerRoomRegistry {
public:
    WorkerRoomRegistry(WorkerId worker_id,
                       std::chrono::nanoseconds tick_interval,
                       std::size_t room_capacity);

    [[nodiscard]] Room* FindRoom(RoomId room_id);
    [[nodiscard]] Room& AssignRoomForJoin();
    [[nodiscard]] const WorkerSessionBinding* FindBinding(SessionId session_id) const;
    [[nodiscard]] Session MakeSession(const WorkerSessionBinding& binding) const;
    [[nodiscard]] Room::Clock::time_point NextWakeTime(Room::Clock::time_point fallback) const;

    template <typename HandleSession>
    void ForEachActiveSessionInRoom(RoomId room_id, HandleSession handle_session) const
    {
        const auto session_ids_iterator = active_session_ids_by_room_.find(room_id);
        if (session_ids_iterator == active_session_ids_by_room_.end()) {
            return;
        }

        for (const auto session_id : session_ids_iterator->second) {
            const auto* binding = FindBinding(session_id);
            if (binding != nullptr && binding->active && binding->room_id == room_id) {
                handle_session(MakeSession(*binding));
            }
        }
    }

    template <typename HandleTickResult>
    void TickDueRooms(Room::Clock::time_point now, std::uint32_t max_catch_up_ticks, HandleTickResult handle_tick_result)
    {
        auto room_iterator = rooms_.begin();
        while (room_iterator != rooms_.end()) {
            auto& room = room_iterator->second;
            const auto room_id = room_iterator->first;
            std::uint32_t catch_up_count = 0;
            bool room_erased = false;

            while (room.next_tick_time() <= now && catch_up_count < max_catch_up_ticks) {
                auto result = room.Tick();
                handle_tick_result(room, result);

                if (rooms_.find(room_id) == rooms_.end()) {
                    room_erased = true;
                    break;
                }

                ++catch_up_count;
                now = Room::Clock::now();
            }

            if (!room_erased) {
                ++room_iterator;
            } else {
                room_iterator = rooms_.upper_bound(room_id);
            }
        }
    }

    void BindPendingSession(const Session& session, RoomId room_id);
    void ActivateSession(SessionId session_id);
    void UpdateSession(const Session& session);
    void RemoveSession(SessionId session_id);
    void UpdateRoomOpenState(RoomId room_id);
    void HandleRoomAfterLeave(RoomId room_id);

private:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] Room& CreateRoom();
    [[nodiscard]] WorkerSessionBinding* FindMutableBinding(SessionId session_id);
    [[nodiscard]] std::size_t SessionCountForRoom(RoomId room_id) const;
    void RemoveActiveSessionFromRoom(RoomId room_id, SessionId session_id);
    void RemoveOpenRoom(RoomId room_id);

    WorkerId worker_id_;
    std::chrono::nanoseconds tick_interval_;
    std::size_t room_capacity_;
    std::map<RoomId, Room> rooms_;
    std::vector<RoomId> open_room_ids_;
    std::unordered_map<SessionId, WorkerSessionBinding> session_bindings_;
    std::unordered_map<RoomId, std::vector<SessionId>> active_session_ids_by_room_;
    std::unordered_map<RoomId, std::size_t> session_count_by_room_;
    RoomId::ValueType next_local_room_id_{1};
};

} // namespace rrs
