#pragma once

#include "rrs/net/BinaryProtocol.h"
#include "rrs/runtime/Session.h"

#include <string>

namespace rrs {

struct WorkerToIoMessage {
    Session session;
    ServerMessageType server_message_type{ServerMessageType::kSnapshot};
    std::string payload;

    [[nodiscard]] static WorkerToIoMessage MakeJoinOk(Session session, std::string snapshot_payload);
    [[nodiscard]] static WorkerToIoMessage MakeReconnectOk(Session session, std::string snapshot_payload);
    [[nodiscard]] static WorkerToIoMessage MakeSnapshot(Session session, std::string payload);
    [[nodiscard]] static WorkerToIoMessage MakeError(Session session, std::string error_message);
};

} // namespace rrs
