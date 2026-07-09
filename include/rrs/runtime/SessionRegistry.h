#pragma once

#include "rrs/base/Types.h"
#include "rrs/runtime/Session.h"

#include <map>
#include <mutex>
#include <optional>

namespace rrs {

class SessionRegistry {
public:
    [[nodiscard]] Session Create(PlayerId player_id, IoThreadId io_thread_id, WorkerId worker_id);
    [[nodiscard]] std::optional<Session> Reconnect(SessionId session_id, IoThreadId io_thread_id);
    void Remove(SessionId session_id);

private:
    std::mutex mutex_;
    std::map<SessionId, Session> sessions_by_id_;
    SessionId::ValueType next_session_id_{1};
};

} // namespace rrs
