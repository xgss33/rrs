#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/Session.h"
#include "rrs/simulation/Room.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace rrs {

struct WorkerSessionBinding {
    Generation generation{1};
    PlayerId player_id;
    IoThreadId io_thread_id;
    RoomId room_id;
    bool active{false};
};

struct WorkerRoomMetrics {
    std::size_t static_entities{0};
    std::size_t dynamic_entities{0};
    std::size_t visibility_observers{0};
    std::size_t visible_other_player_balls{0};
};

class WorkerRoomRegistry {
public:
    WorkerRoomRegistry(WorkerId worker_id,
                       std::chrono::nanoseconds tick_interval,
                       std::size_t room_capacity);

    Room* FindRoom(RoomId room_id);
    [[nodiscard]] Room& AssignRoomForJoin();
    const WorkerSessionBinding* FindBinding(SessionId session_id) const;
    Session MakeSession(SessionId session_id, const WorkerSessionBinding& binding) const;
    Room::Clock::time_point NextWakeTime(Room::Clock::time_point fallback) const;

    template <typename HandleSession>
    void ForEachActiveSessionInRoom(RoomId room_id, HandleSession handle_session) const
    {
        const auto room_iterator = rooms_.find(room_id);
        if (room_iterator == rooms_.end()) {
            return;
        }

        for (const auto session_id : room_iterator->second.active_session_ids) {
            handle_session(MakeSession(session_id, session_bindings_.at(session_id)));
        }
    }

    template <typename HandleTickResult>
    void TickDueRooms(Room::Clock::time_point now, std::uint32_t max_catch_up_ticks, HandleTickResult handle_tick_result)
    {
        auto room_iterator = rooms_.begin();
        while (room_iterator != rooms_.end()) {
            auto& room = room_iterator->second.room;
            const auto room_id = room_iterator->first;
            std::uint32_t catch_up_count = 0;
            bool room_erased = false;

            while (room.next_tick_time() <= now && catch_up_count < max_catch_up_ticks) {
                const auto scheduled_tick_time = room.next_tick_time();
                auto result = room.Tick();
                UpdateRoomOpenState(room_id);
                handle_tick_result(room, result, scheduled_tick_time);

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

    struct RoomState {
        Room room;
        std::vector<SessionId> active_session_ids;
        std::size_t session_count{0};
    };

    Room& CreateRoom();
    WorkerSessionBinding* FindMutableBinding(SessionId session_id);

    WorkerId worker_id_;
    std::chrono::nanoseconds tick_interval_;
    std::size_t room_capacity_;
    std::map<RoomId, RoomState> rooms_;
    std::vector<RoomId> open_room_ids_;
    std::unordered_map<SessionId, WorkerSessionBinding> session_bindings_;
    RoomId::ValueType next_local_room_id_{1};

public:
    WorkerRoomMetrics CollectMetrics() const;
};

} // namespace rrs
