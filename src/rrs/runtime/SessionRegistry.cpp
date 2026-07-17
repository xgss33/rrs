#include "rrs/runtime/SessionRegistry.h"

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/Session.h"

#include <mutex>

namespace rrs {

Session SessionRegistry::Create(PlayerId player_id, IoThreadId io_thread_id, WorkerId worker_id)
{
    std::unique_lock lock(mutex_);

    auto session = Session{
        .session_id = SessionId{next_session_id_++},
        .generation = 1,
        .player_id = player_id,
        .io_thread_id = io_thread_id,
        .worker_id = worker_id,
    };

    sessions_by_id_[session.session_id] = session;
    return session;
}

std::optional<Session> SessionRegistry::Reconnect(SessionId session_id, IoThreadId io_thread_id)
{
    std::unique_lock lock(mutex_);

    auto iterator = sessions_by_id_.find(session_id);
    if (iterator == sessions_by_id_.end()) {
        return std::nullopt;
    }

    ++iterator->second.generation;
    iterator->second.io_thread_id = io_thread_id;
    return iterator->second;
}

void SessionRegistry::Remove(SessionId session_id)
{
    std::unique_lock lock(mutex_);

    const auto iterator = sessions_by_id_.find(session_id);
    if (iterator == sessions_by_id_.end()) {
        return;
    }

    sessions_by_id_.erase(iterator);
}

} // namespace rrs
