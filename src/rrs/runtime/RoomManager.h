#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/Room.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace rrs {

struct RoomMetrics {
    std::size_t static_entities{0};
    std::size_t dynamic_entities{0};
    std::size_t visibility_observers{0};
    std::size_t visible_other_player_balls{0};
};

class RoomManager {
public:
    RoomManager(WorkerId worker_id,
                std::chrono::nanoseconds tick_interval,
                std::size_t room_capacity);

    Room* FindRoom(RoomId room_id);
    [[nodiscard]] Room& AssignRoomForJoin();
    Room::Clock::time_point NextWakeTime(Room::Clock::time_point fallback) const;

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

                if (!rooms_.contains(room_id)) {
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

    void AddSession(RoomId room_id, SessionId session_id);
    void RemoveSession(RoomId room_id, SessionId session_id);
    const std::vector<SessionId>& Sessions(RoomId room_id) const;
    [[nodiscard]] std::vector<SessionId> RemoveRoom(RoomId room_id);
    void UpdateRoomOpenState(RoomId room_id);
    void HandleRoomAfterLeave(RoomId room_id);
    [[nodiscard]] RoomMetrics CollectMetrics() const;

private:
    using Clock = std::chrono::steady_clock;

    struct RoomState {
        Room room;
        std::vector<SessionId> session_ids;
    };

    Room& CreateRoom();

    WorkerId worker_id_;
    std::chrono::nanoseconds tick_interval_;
    std::size_t room_capacity_;
    std::map<RoomId, RoomState> rooms_;
    std::vector<RoomId> open_room_ids_;
    RoomId::ValueType next_local_room_id_{1};
};

} // namespace rrs
