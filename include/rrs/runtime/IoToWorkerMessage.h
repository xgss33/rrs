#pragma once

#include "rrs/runtime/Session.h"

#include <cstdint>

namespace rrs {

struct PlayerInput {
    std::int32_t move_x{0};
    std::int32_t move_y{0};
};

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

    [[nodiscard]] static IoToWorkerMessage MakeJoin(Session session);
    [[nodiscard]] static IoToWorkerMessage MakeReconnect(Session session);
    [[nodiscard]] static IoToWorkerMessage MakePlayerInput(Session session, PlayerInput input);
    [[nodiscard]] static IoToWorkerMessage MakeLeave(Session session);
};

} // namespace rrs
