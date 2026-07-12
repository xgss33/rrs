#include "rrs/runtime/WorkerToIoMessage.h"
#include "rrs/net/BinaryProtocol.h"

#include <memory>
#include <utility>

namespace rrs {

WorkerToIoMessage WorkerToIoMessage::MakeJoinOk(Session session, const std::string& snapshot_payload)
{
    auto frame = EncodeFrame(
        ServerMessageType::kJoinOk,
        EncodeJoinOkPayload(session.session_id, session.generation, snapshot_payload));

    return WorkerToIoMessage{
        .session = session,
        .encoded_frame = std::make_shared<const std::string>(std::move(frame)),
    };
}

WorkerToIoMessage WorkerToIoMessage::MakeReconnectOk(Session session, const std::string& snapshot_payload)
{
    auto frame = EncodeFrame(
        ServerMessageType::kReconnectOk,
        EncodeReconnectOkPayload(session.session_id, session.generation, snapshot_payload));

    return WorkerToIoMessage{
        .session = session,
        .encoded_frame = std::make_shared<const std::string>(std::move(frame)),
    };
}

WorkerToIoMessage WorkerToIoMessage::MakeError(Session session, const std::string& error_message)
{
    auto frame = EncodeFrame(ServerMessageType::kError, EncodeErrorPayload(error_message));

    return WorkerToIoMessage{
        .session = session,
        .encoded_frame = std::make_shared<const std::string>(std::move(frame)),
    };
}

} // namespace rrs
