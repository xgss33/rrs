#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/ConnectionHandle.h"
#include "rrs/runtime/Session.h"

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace rrs {

class SessionManager {
public:
    SessionManager(WorkerId worker_id, std::size_t worker_count);

    [[nodiscard]] SessionId Create(
        PlayerId player_id,
        RoomId room_id,
        ConnectionHandle connection);

    Session* Find(SessionId session_id);
    Session* FindByPlayer(PlayerId player_id);
    [[nodiscard]] std::optional<SessionId> FindIdByConnection(ConnectionHandle connection) const;

    void Activate(SessionId session_id);
    [[nodiscard]] std::optional<ConnectionHandle> Bind(
        SessionId session_id,
        ConnectionHandle connection);
    void Unbind(ConnectionHandle connection);
    [[nodiscard]] Session Remove(SessionId session_id);

private:
    WorkerId worker_id_;
    std::size_t worker_count_;
    SessionId::ValueType next_local_sequence_{0};
    std::unordered_map<SessionId, Session> sessions_;
    std::unordered_map<PlayerId, SessionId> session_by_player_;
    std::unordered_map<ConnectionHandle, SessionId> session_by_connection_;
};

[[nodiscard]] WorkerId GetSessionWorker(SessionId session_id, std::size_t worker_count);

} // namespace rrs
