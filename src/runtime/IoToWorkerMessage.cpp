#include "rrs/runtime/IoToWorkerMessage.h"

namespace rrs {

IoToWorkerMessage IoToWorkerMessage::MakeJoin(Session session)
{
    return IoToWorkerMessage{
        .type = IoToWorkerMessageType::kJoin,
        .session = session,
        .input = {},
    };
}

IoToWorkerMessage IoToWorkerMessage::MakeReconnect(Session session)
{
    return IoToWorkerMessage{
        .type = IoToWorkerMessageType::kReconnect,
        .session = session,
        .input = {},
    };
}

IoToWorkerMessage IoToWorkerMessage::MakePlayerInput(Session session, PlayerInput input)
{
    return IoToWorkerMessage{
        .type = IoToWorkerMessageType::kPlayerInput,
        .session = session,
        .input = input,
    };
}

IoToWorkerMessage IoToWorkerMessage::MakeLeave(Session session)
{
    return IoToWorkerMessage{
        .type = IoToWorkerMessageType::kLeave,
        .session = session,
        .input = {},
    };
}

} // namespace rrs
