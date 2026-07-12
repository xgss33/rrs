#pragma once

#include "rrs/base/Types.h"

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace rrs {

class IoSessionRouter {
public:
    void Bind(int client_fd, SessionId session_id);
    bool Unbind(int client_fd, SessionId session_id);

    [[nodiscard]] std::optional<int> FindClientFd(SessionId session_id) const;
    [[nodiscard]] std::size_t size() const noexcept { return fd_by_session_.size(); }

private:
    std::unordered_map<SessionId, int> fd_by_session_;
};

} // namespace rrs
