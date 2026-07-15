#pragma once

#include "rrs/game/PlayerInput.h"
#include "rrs/runtime/Session.h"

namespace rrs {

enum class IoToWorkerMessageType {
    kJoin,
    kReconnect,
    kPlayerInput,
    kLeave,
};

struct IoToWorkerMessage {
    IoToWorkerMessageType type{IoToWorkerMessageType::kPlayerInput};
    Session session;
    PlayerInput input;
};

} // namespace rrs
