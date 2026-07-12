#include "rrs/runtime/WorkerRoomRegistry.h"
#include "rrs/log/Logger.h"

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

    return iterator != rooms_.end() ? &iterator->second : nullptr;
}

Room& WorkerRoomRegistry::AssignRoomForJoin()
{
    for (const auto room_id : open_room_ids_) {
        auto* room = FindRoom(room_id);
        if (room != nullptr && SessionCountForRoom(room_id) < room_capacity_) {
            return *room;
        }
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

Session WorkerRoomRegistry::MakeSession(const WorkerSessionBinding& binding) const
{
    return Session{
        .session_id = binding.session_id,
        .generation = binding.generation,
        .player_id = binding.player_id,
        .io_thread_id = binding.io_thread_id,
        .worker_id = worker_id_,
    };
}

Room::Clock::time_point WorkerRoomRegistry::NextWakeTime(Room::Clock::time_point fallback) const
{
    auto next_wake_time = fallback;
    for (const auto& [_, room] : rooms_) {
        next_wake_time = std::min(next_wake_time, room.next_tick_time());
    }

    return next_wake_time;
}

void WorkerRoomRegistry::BindPendingSession(const Session& session, RoomId room_id)
{
    if (session_bindings_.contains(session.session_id)) {
        return;
    }

    session_bindings_[session.session_id] = WorkerSessionBinding{
        .session_id = session.session_id,
        .generation = session.generation,
        .player_id = session.player_id,
        .io_thread_id = session.io_thread_id,
        .room_id = room_id,
        .active = false,
    };
    ++session_count_by_room_[room_id];
}

void WorkerRoomRegistry::ActivateSession(SessionId session_id)
{
    auto* binding = FindMutableBinding(session_id);
    if (binding == nullptr || binding->active) {
        return;
    }

    binding->active = true;
    active_session_ids_by_room_[binding->room_id].push_back(session_id);
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
    const auto* binding = FindBinding(session_id);
    if (binding == nullptr) {
        return;
    }

    const auto room_id = binding->room_id;
    if (binding->active) {
        RemoveActiveSessionFromRoom(room_id, session_id);
    }
    session_bindings_.erase(session_id);

    auto count_iterator = session_count_by_room_.find(room_id);
    if (count_iterator == session_count_by_room_.end()) {
        return;
    }

    if (count_iterator->second > 1) {
        --count_iterator->second;
    } else {
        session_count_by_room_.erase(count_iterator);
    }
}

void WorkerRoomRegistry::UpdateRoomOpenState(RoomId room_id)
{
    auto* room = FindRoom(room_id);
    if (room == nullptr) {
        return;
    }

    if (SessionCountForRoom(room_id) >= room_capacity_) {
        RemoveOpenRoom(room_id);
        return;
    }

    if (std::find(open_room_ids_.begin(), open_room_ids_.end(), room_id) == open_room_ids_.end()) {
        open_room_ids_.push_back(room_id);
    }
}

void WorkerRoomRegistry::HandleRoomAfterLeave(RoomId room_id)
{
    auto* room = FindRoom(room_id);
    if (room == nullptr) {
        return;
    }

    if (room->entity_count() > 0 || SessionCountForRoom(room_id) > 0) {
        UpdateRoomOpenState(room_id);
        return;
    }

    RemoveOpenRoom(room_id);
    active_session_ids_by_room_.erase(room_id);
    session_count_by_room_.erase(room_id);
    rooms_.erase(room_id);
    Logger::Info("[Worker] delete empty room worker={} room={}", worker_id_.value(), room_id.value());
}

Room& WorkerRoomRegistry::CreateRoom()
{
    const auto room_id = RoomId{worker_id_.value() * kRoomIdWorkerStride + next_local_room_id_++};
    const auto first_tick_time = Clock::now() + tick_interval_;
    const auto iterator = rooms_.try_emplace(room_id, room_id, first_tick_time, tick_interval_).first;
    open_room_ids_.push_back(room_id);
    session_count_by_room_[room_id] = 0;

    Logger::Info("[Worker] create room worker={} room={}", worker_id_.value(), room_id.value());
    return iterator->second;
}

void WorkerRoomRegistry::RemoveOpenRoom(RoomId room_id)
{
    std::erase(open_room_ids_, room_id);
}

void WorkerRoomRegistry::RemoveActiveSessionFromRoom(RoomId room_id, SessionId session_id)
{
    auto iterator = active_session_ids_by_room_.find(room_id);
    if (iterator == active_session_ids_by_room_.end()) {
        return;
    }

    std::erase(iterator->second, session_id);
    if (iterator->second.empty()) {
        active_session_ids_by_room_.erase(iterator);
    }
}

std::size_t WorkerRoomRegistry::SessionCountForRoom(RoomId room_id) const
{
    const auto iterator = session_count_by_room_.find(room_id);
    return iterator != session_count_by_room_.end() ? iterator->second : 0;
}

} // namespace rrs
