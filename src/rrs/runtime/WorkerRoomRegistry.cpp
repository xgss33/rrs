#include "rrs/runtime/WorkerRoomRegistry.h"

#include "rrs/core/Identifiers.h"
#include "rrs/observability/Logger.h"
#include "rrs/runtime/Session.h"
#include "rrs/simulation/Room.h"

#include <algorithm>

namespace rrs {

namespace {

constexpr RoomId::ValueType kRoomIdWorkerStride = 1'000'000;

} // namespace

WorkerRoomRegistry::WorkerRoomRegistry(WorkerId worker_id,
                                       std::chrono::nanoseconds tick_interval,
                                       std::size_t room_capacity)
    : worker_id_(worker_id)
    , tick_interval_(tick_interval)
    , room_capacity_(room_capacity)
{
}

Room* WorkerRoomRegistry::FindRoom(RoomId room_id)
{
    const auto iterator = rooms_.find(room_id);

    return iterator != rooms_.end() ? &iterator->second.room : nullptr;
}

Room& WorkerRoomRegistry::AssignRoomForJoin()
{
    if (!open_room_ids_.empty()) {
        return rooms_.at(open_room_ids_.front()).room;
    }

    return CreateRoom();
}

WorkerSessionBinding* WorkerRoomRegistry::FindMutableBinding(SessionId session_id)
{
    const auto iterator = session_bindings_.find(session_id);
    if (iterator == session_bindings_.end()) {
        return nullptr;
    }

    return &iterator->second;
}

const WorkerSessionBinding* WorkerRoomRegistry::FindBinding(SessionId session_id) const
{
    const auto iterator = session_bindings_.find(session_id);
    if (iterator == session_bindings_.end()) {
        return nullptr;
    }

    return &iterator->second;
}

Session WorkerRoomRegistry::MakeSession(SessionId session_id, const WorkerSessionBinding& binding) const
{
    return Session{
        .session_id = session_id,
        .generation = binding.generation,
        .player_id = binding.player_id,
        .io_thread_id = binding.io_thread_id,
        .worker_id = worker_id_,
    };
}

Room::Clock::time_point WorkerRoomRegistry::NextWakeTime(Room::Clock::time_point fallback) const
{
    auto next_wake_time = fallback;
    for (const auto& [_, room_state] : rooms_) {
        next_wake_time = std::min(next_wake_time, room_state.room.next_tick_time());
    }

    return next_wake_time;
}

void WorkerRoomRegistry::BindPendingSession(const Session& session, RoomId room_id)
{
    const auto [_, inserted] = session_bindings_.try_emplace(session.session_id, WorkerSessionBinding{
        .generation = session.generation,
        .player_id = session.player_id,
        .io_thread_id = session.io_thread_id,
        .room_id = room_id,
        .active = false,
    });
    if (!inserted) {
        return;
    }

    ++rooms_.at(room_id).session_count;
}

void WorkerRoomRegistry::ActivateSession(SessionId session_id)
{
    auto* binding = FindMutableBinding(session_id);
    if (binding == nullptr || binding->active) {
        return;
    }

    binding->active = true;
    rooms_.at(binding->room_id).active_session_ids.push_back(session_id);
}

void WorkerRoomRegistry::UpdateSession(const Session& session)
{
    auto* binding = FindMutableBinding(session.session_id);
    if (binding != nullptr) {
        binding->generation = session.generation;
        binding->io_thread_id = session.io_thread_id;
    }
}

void WorkerRoomRegistry::RemoveSession(SessionId session_id)
{
    const auto binding_iterator = session_bindings_.find(session_id);
    if (binding_iterator == session_bindings_.end()) {
        return;
    }

    const auto room_id = binding_iterator->second.room_id;
    auto& room_state = rooms_.at(room_id);
    if (binding_iterator->second.active) {
        std::erase(room_state.active_session_ids, session_id);
    }
    --room_state.session_count;
    session_bindings_.erase(binding_iterator);
}

void WorkerRoomRegistry::UpdateRoomOpenState(RoomId room_id)
{
    const auto room_iterator = rooms_.find(room_id);
    if (room_iterator == rooms_.end()) {
        return;
    }

    if (room_iterator->second.session_count >= room_capacity_) {
        std::erase(open_room_ids_, room_id);
        return;
    }

    if (std::find(open_room_ids_.begin(), open_room_ids_.end(), room_id) == open_room_ids_.end()) {
        open_room_ids_.push_back(room_id);
    }
}

void WorkerRoomRegistry::HandleRoomAfterLeave(RoomId room_id)
{
    const auto room_iterator = rooms_.find(room_id);
    if (room_iterator == rooms_.end()) {
        return;
    }

    if (room_iterator->second.room.player_count() > 0 || room_iterator->second.session_count > 0) {
        UpdateRoomOpenState(room_id);
        return;
    }

    std::erase(open_room_ids_, room_id);
    rooms_.erase(room_iterator);
    Logger::Info("[Worker] delete empty room worker={} room={}", worker_id_.value(), room_id.value());
}

Room& WorkerRoomRegistry::CreateRoom()
{
    const auto room_id = RoomId{worker_id_.value() * kRoomIdWorkerStride + next_local_room_id_++};
    const auto first_tick_time = Clock::now() + tick_interval_;
    const auto iterator = rooms_.try_emplace(
        room_id,
        RoomState{
            .room = Room{room_id, first_tick_time, tick_interval_},
            .active_session_ids = {},
            .session_count = 0,
        }).first;
    open_room_ids_.push_back(room_id);

    Logger::Info("[Worker] create room worker={} room={}", worker_id_.value(), room_id.value());
    return iterator->second.room;
}

WorkerRoomMetrics WorkerRoomRegistry::CollectMetrics() const
{
    auto metrics = WorkerRoomMetrics{};
    for (const auto& [_, room_state] : rooms_) {
        metrics.static_entities += room_state.room.static_entity_count();
        metrics.dynamic_entities += room_state.room.dynamic_entity_count();
        metrics.visibility_observers += room_state.room.visibility_observer_count();
        metrics.visible_other_player_balls += room_state.room.visible_other_player_ball_count();
    }
    return metrics;
}

} // namespace rrs
