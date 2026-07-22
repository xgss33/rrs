#include "rrs/runtime/RoomManager.h"

#include "rrs/core/Identifiers.h"
#include "rrs/observability/Logger.h"
#include "rrs/simulation/Room.h"

#include <algorithm>

namespace rrs {

namespace {

constexpr RoomId::ValueType kRoomIdWorkerStride = 1'000'000;

} // namespace

RoomManager::RoomManager(WorkerId worker_id,
                         std::chrono::nanoseconds tick_interval,
                         std::size_t room_capacity)
    : worker_id_(worker_id)
    , tick_interval_(tick_interval)
    , room_capacity_(room_capacity)
{
}

Room* RoomManager::FindRoom(RoomId room_id)
{
    const auto iterator = rooms_.find(room_id);
    return iterator != rooms_.end() ? &iterator->second.room : nullptr;
}

Room& RoomManager::AssignRoomForJoin()
{
    if (!open_room_ids_.empty()) {
        return rooms_.at(open_room_ids_.front()).room;
    }
    return CreateRoom();
}

Room::Clock::time_point RoomManager::NextWakeTime(Room::Clock::time_point fallback) const
{
    auto next_wake_time = fallback;
    for (const auto& [_, room_state] : rooms_) {
        next_wake_time = std::min(next_wake_time, room_state.room.next_tick_time());
    }
    return next_wake_time;
}

void RoomManager::AddSession(RoomId room_id, SessionId session_id)
{
    auto& room_state = rooms_.at(room_id);
    room_state.session_ids.push_back(session_id);
    UpdateRoomOpenState(room_id);
}

void RoomManager::RemoveSession(RoomId room_id, SessionId session_id)
{
    std::erase(rooms_.at(room_id).session_ids, session_id);
    UpdateRoomOpenState(room_id);
}

const std::vector<SessionId>& RoomManager::Sessions(RoomId room_id) const
{
    return rooms_.at(room_id).session_ids;
}

std::vector<SessionId> RoomManager::RemoveRoom(RoomId room_id)
{
    const auto iterator = rooms_.find(room_id);
    auto session_ids = std::move(iterator->second.session_ids);
    std::erase(open_room_ids_, room_id);
    rooms_.erase(iterator);
    Logger::Info("[Worker] delete room worker={} room={}", worker_id_.value(), room_id.value());
    return session_ids;
}

void RoomManager::UpdateRoomOpenState(RoomId room_id)
{
    const auto iterator = rooms_.find(room_id);
    if (!iterator->second.room.accepts_joins()
        || iterator->second.session_ids.size() >= room_capacity_) {
        std::erase(open_room_ids_, room_id);
        return;
    }

    if (std::find(open_room_ids_.begin(), open_room_ids_.end(), room_id) == open_room_ids_.end()) {
        open_room_ids_.push_back(room_id);
    }
}

void RoomManager::HandleRoomAfterLeave(RoomId room_id)
{
    const auto iterator = rooms_.find(room_id);
    if (iterator->second.room.player_count() > 0 || !iterator->second.session_ids.empty()) {
        UpdateRoomOpenState(room_id);
        return;
    }
    (void)RemoveRoom(room_id);
}

Room& RoomManager::CreateRoom()
{
    const auto room_id = RoomId{worker_id_.value() * kRoomIdWorkerStride + next_local_room_id_++};
    const auto first_tick_time = Clock::now() + tick_interval_;
    const auto iterator = rooms_.try_emplace(
        room_id,
        RoomState{
            .room = Room{room_id, first_tick_time, tick_interval_},
            .session_ids = {},
        }).first;
    open_room_ids_.push_back(room_id);

    Logger::Info("[Worker] create room worker={} room={}", worker_id_.value(), room_id.value());
    return iterator->second.room;
}

RoomMetrics RoomManager::CollectMetrics() const
{
    auto metrics = RoomMetrics{};
    for (const auto& [_, room_state] : rooms_) {
        metrics.static_entities += room_state.room.static_entity_count();
        metrics.dynamic_entities += room_state.room.dynamic_entity_count();
        metrics.visibility_observers += room_state.room.visibility_observer_count();
        metrics.visible_other_player_balls += room_state.room.visible_other_player_ball_count();
    }
    return metrics;
}

} // namespace rrs
