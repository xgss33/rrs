#include "rrs/runtime/WorkerManager.h"

#include "rrs/core/Identifiers.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/simulation/Room.h"
#include "rrs/simulation/RoomRules.h"

#include <algorithm>
#include <utility>

namespace rrs {

WorkerManager::WorkerManager(WorkerId worker_id, std::size_t worker_count)
    : worker_id_(worker_id)
    , worker_count_(worker_count)
{
}

Session* WorkerManager::FindSession(SessionId session_id)
{
    const auto room_id = room_by_session_.find(session_id);
    if (room_id == room_by_session_.end()) {
        return nullptr;
    }
    return FindSessionInRoom(rooms_.at(room_id->second), session_id);
}

Session* WorkerManager::FindSessionByPlayer(PlayerId player_id)
{
    const auto session_id = session_by_player_.find(player_id);
    return session_id == session_by_player_.end() ? nullptr : FindSession(session_id->second);
}

Session* WorkerManager::FindSessionByConnection(ConnectionHandle connection)
{
    const auto session_id = session_by_connection_.find(connection);
    return session_id == session_by_connection_.end() ? nullptr : FindSession(session_id->second);
}

Room& WorkerManager::RoomFor(SessionId session_id)
{
    return rooms_.at(room_by_session_.at(session_id)).room;
}

SessionId WorkerManager::Join(
    PlayerId player_id,
    ConnectionHandle connection,
    Room::Clock::time_point entered_at)
{
    auto& room = AssignRoomForJoin();
    const auto session_id = SessionId{
        next_local_session_sequence_++ * worker_count_ + worker_id_.value() + 1,
    };
    room.sessions.push_back(Session{
        .id = session_id,
        .player_id = player_id,
        .state = SessionState::kJoining,
        .connection = connection,
    });
    room_by_session_.emplace(session_id, room.room.id());
    session_by_player_.emplace(player_id, session_id);
    session_by_connection_.emplace(connection, session_id);
    room.room.EnqueueCommand(Room::Command{
        .type = Room::CommandType::kJoin,
        .player_id = player_id,
        .input = {},
        .entered_at = entered_at,
    });
    UpdateRoomOpenState(room.room.id());
    return session_id;
}

std::optional<ConnectionHandle> WorkerManager::Bind(
    Session& session,
    ConnectionHandle connection)
{
    const auto replaced_connection = session.connection;
    if (replaced_connection) {
        session_by_connection_.erase(*replaced_connection);
    }
    session.connection = connection;
    session_by_connection_.emplace(connection, session.id);
    return replaced_connection;
}

void WorkerManager::EnqueueInput(
    ConnectionHandle connection,
    PlayerInput input,
    Room::Clock::time_point entered_at)
{
    auto* session = FindSessionByConnection(connection);
    if (session == nullptr || session->state != SessionState::kActive) {
        return;
    }
    RoomFor(session->id).EnqueueCommand(Room::Command{
        .type = Room::CommandType::kPlayerInput,
        .player_id = session->player_id,
        .input = input,
        .entered_at = entered_at,
    });
}

void WorkerManager::Leave(SessionId session_id, Room::Clock::time_point entered_at)
{
    RemoveSession(session_id, entered_at);
}

void WorkerManager::Disconnect(SessionId session_id, Room::Clock::time_point entered_at)
{
    auto* session = FindSession(session_id);
    if (session->state == SessionState::kJoining) {
        RemoveSession(session_id, entered_at);
        return;
    }

    RoomFor(session_id).RemoveSnapshotObserver(session->player_id);
    session_by_connection_.erase(*session->connection);
    session->connection.reset();
}

Room::Clock::time_point WorkerManager::NextWakeTime(Room::Clock::time_point fallback) const
{
    auto next_wake_time = fallback;
    for (const auto& [_, room] : rooms_) {
        next_wake_time = std::min(next_wake_time, room.room.next_tick_time());
    }
    return next_wake_time;
}

RoomMetrics WorkerManager::CollectMetrics() const
{
    auto metrics = RoomMetrics{};
    for (const auto& [_, room] : rooms_) {
        metrics.static_entities += room.room.static_entity_count();
        metrics.dynamic_entities += room.room.dynamic_entity_count();
        metrics.visibility_observers += room.room.visibility_observer_count();
        metrics.visible_other_player_balls += room.room.visible_other_player_ball_count();
    }
    return metrics;
}

WorkerManager::RoomState& WorkerManager::AssignRoomForJoin()
{
    return open_room_ids_.empty() ? CreateRoom() : rooms_.at(open_room_ids_.front());
}

WorkerManager::RoomState& WorkerManager::CreateRoom()
{
    const auto room_id = RoomId{
        next_local_room_sequence_++ * worker_count_ + worker_id_.value() + 1};
    const auto first_tick_time = Room::Clock::now() + room_rules::kTickInterval;
    auto [room, _] = rooms_.try_emplace(
        room_id,
        RoomState{
            .room = Room{room_id, first_tick_time},
            .sessions = {},
        });
    open_room_ids_.push_back(room_id);
    return room->second;
}

Session* WorkerManager::FindSessionInRoom(RoomState& room, SessionId session_id)
{
    const auto session = std::find_if(room.sessions.begin(), room.sessions.end(), [session_id](const Session& value) {
        return value.id == session_id;
    });
    return session == room.sessions.end() ? nullptr : &*session;
}

void WorkerManager::RemoveSession(SessionId session_id, Room::Clock::time_point entered_at)
{
    const auto room_id = room_by_session_.at(session_id);
    auto& room = rooms_.at(room_id);
    const auto session = *FindSessionInRoom(room, session_id);
    room.room.EnqueueCommand(Room::Command{
        .type = Room::CommandType::kLeave,
        .player_id = session.player_id,
        .input = {},
        .entered_at = entered_at,
    });
    if (session.connection) {
        session_by_connection_.erase(*session.connection);
    }
    session_by_player_.erase(session.player_id);
    room_by_session_.erase(session_id);
    std::erase_if(room.sessions, [session_id](const Session& value) {
        return value.id == session_id;
    });
    UpdateRoomOpenState(room_id);
}

void WorkerManager::RemoveRoom(RoomId room_id)
{
    auto& room = rooms_.at(room_id);
    for (const auto& session : room.sessions) {
        room_by_session_.erase(session.id);
        session_by_player_.erase(session.player_id);
        if (session.connection) {
            session_by_connection_.erase(*session.connection);
        }
    }
    std::erase(open_room_ids_, room_id);
    rooms_.erase(room_id);
}

void WorkerManager::UpdateRoomOpenState(RoomId room_id)
{
    const auto& room = rooms_.at(room_id);
    if (!room.room.accepts_joins() || room.sessions.size() >= room_rules::kRoomCapacity) {
        std::erase(open_room_ids_, room_id);
        return;
    }
    if (std::find(open_room_ids_.begin(), open_room_ids_.end(), room_id) == open_room_ids_.end()) {
        open_room_ids_.push_back(room_id);
    }
}

WorkerId GetSessionWorker(SessionId session_id, std::size_t worker_count)
{
    return WorkerId{(session_id.value() - 1) % worker_count};
}

} // namespace rrs
