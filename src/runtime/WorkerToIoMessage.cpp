#include "rrs/runtime/WorkerToIoMessage.h"
#include "rrs/net/BinaryProtocol.h"

#include <utility>

namespace rrs {

WorkerToIoMessage WorkerToIoMessage::MakeJoinOk(Session session, std::string snapshot_payload)
{
    return WorkerToIoMessage{
        .session = session,
        .server_message_type = ServerMessageType::kJoinOk,
        .payload = EncodeJoinOkPayload(session.session_id, session.generation, snapshot_payload),
    };
}

WorkerToIoMessage WorkerToIoMessage::MakeReconnectOk(Session session, std::string snapshot_payload)
{
    return WorkerToIoMessage{
        .session = session,
        .server_message_type = ServerMessageType::kReconnectOk,
        .payload = EncodeReconnectOkPayload(session.session_id, session.generation, snapshot_payload),
    };
}

WorkerToIoMessage WorkerToIoMessage::MakeSnapshot(Session session, std::string payload)
{
    return WorkerToIoMessage{
        .session = session,
        .server_message_type = ServerMessageType::kSnapshot,
        .payload = std::move(payload),
    };
}

WorkerToIoMessage WorkerToIoMessage::MakeError(Session session, std::string error_message)
{
    return WorkerToIoMessage{
        .session = session,
        .server_message_type = ServerMessageType::kError,
        .payload = EncodeErrorPayload(error_message),
    };
}

} // namespace rrs
