#include "rrs/runtime/SessionManager.h"

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/ConnectionHandle.h"
#include "rrs/runtime/Session.h"

namespace rrs {

SessionManager::SessionManager(WorkerId worker_id, std::size_t worker_count)
    : worker_id_(worker_id)
    , worker_count_(worker_count)
{
}

SessionId SessionManager::Create(
    PlayerId player_id,
    RoomId room_id,
    ConnectionHandle connection)
{
    const auto session_id = SessionId{
        next_local_sequence_++ * worker_count_ + worker_id_.value() + 1,
    };
    sessions_.emplace(session_id, Session{
                                      .player_id = player_id,
                                      .room_id = room_id,
                                      .state = SessionState::kJoining,
                                      .connection = connection,
                                  });
    session_by_player_.emplace(player_id, session_id);
    session_by_connection_.emplace(connection, session_id);
    return session_id;
}

Session* SessionManager::Find(SessionId session_id)
{
    const auto iterator = sessions_.find(session_id);
    return iterator != sessions_.end() ? &iterator->second : nullptr;
}

Session* SessionManager::FindByPlayer(PlayerId player_id)
{
    const auto iterator = session_by_player_.find(player_id);
    return iterator != session_by_player_.end() ? Find(iterator->second) : nullptr;
}

std::optional<SessionId> SessionManager::FindIdByConnection(ConnectionHandle connection) const
{
    const auto iterator = session_by_connection_.find(connection);
    if (iterator == session_by_connection_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

void SessionManager::Activate(SessionId session_id)
{
    sessions_.at(session_id).state = SessionState::kActive;
}

std::optional<ConnectionHandle> SessionManager::Bind(
    SessionId session_id,
    ConnectionHandle connection)
{
    auto& session = sessions_.at(session_id);
    auto replaced_connection = session.connection;
    if (replaced_connection) {
        session_by_connection_.erase(*replaced_connection);
    }
    session.connection = connection;
    session_by_connection_.emplace(connection, session_id);
    return replaced_connection;
}

void SessionManager::Unbind(ConnectionHandle connection)
{
    const auto session_id = session_by_connection_.at(connection);
    session_by_connection_.erase(connection);
    sessions_.at(session_id).connection.reset();
}

Session SessionManager::Remove(SessionId session_id)
{
    auto session = sessions_.at(session_id);
    session_by_player_.erase(session.player_id);
    if (session.connection) {
        session_by_connection_.erase(*session.connection);
    }
    sessions_.erase(session_id);
    return session;
}

WorkerId GetSessionWorker(SessionId session_id, std::size_t worker_count)
{
    return WorkerId{(session_id.value() - 1) % worker_count};
}

} // namespace rrs
