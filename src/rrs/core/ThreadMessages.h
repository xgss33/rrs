#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerInput.h"

#include <string>
#include <variant>

namespace rrs {

enum class ConnectionEvent {
    kLeave,
    kDisconnected,
};

struct WorkerMessage {
    ConnectionHandle connection;
    std::variant<PlayerId, SessionId, PlayerInput, ConnectionEvent> payload;
};

enum class ConnectionAction {
    kNone,
    kActivate,
    kClose,
};

struct IoMessage {
    ConnectionHandle connection;
    std::string frame;
    ConnectionAction action{ConnectionAction::kNone};
};

} // namespace rrs
