#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/ConnectionHandle.h"

#include <optional>

namespace rrs {

enum class SessionState {
    kJoining,
    kActive,
};

struct Session {
    PlayerId player_id;
    RoomId room_id;
    SessionState state{SessionState::kJoining};
    std::optional<ConnectionHandle> connection;
};

} // namespace rrs
