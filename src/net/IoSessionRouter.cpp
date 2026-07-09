#include "rrs/net/IoSessionRouter.h"

namespace rrs {

void IoSessionRouter::Bind(int client_fd, SessionId session_id)
{
    fd_by_session_[session_id] = client_fd;
}

bool IoSessionRouter::Unbind(int client_fd, SessionId session_id)
{
    const auto fd_iterator = fd_by_session_.find(session_id);
    if (fd_iterator == fd_by_session_.end() || fd_iterator->second != client_fd) {
        return false;
    }

    fd_by_session_.erase(fd_iterator);
    return true;
}

std::optional<int> IoSessionRouter::FindClientFd(SessionId session_id) const
{
    const auto iterator = fd_by_session_.find(session_id);
    if (iterator == fd_by_session_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

} // namespace rrs
