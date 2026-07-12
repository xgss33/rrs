#pragma once

#include "rrs/runtime/Session.h"

#include <memory>
#include <string>

namespace rrs {

struct WorkerToIoMessage {
    Session session;
    std::shared_ptr<const std::string> encoded_frame;

    [[nodiscard]] static WorkerToIoMessage MakeJoinOk(Session session, const std::string& snapshot_payload);
    [[nodiscard]] static WorkerToIoMessage MakeReconnectOk(Session session, const std::string& snapshot_payload);
    [[nodiscard]] static WorkerToIoMessage MakeError(Session session, const std::string& error_message);
};

} // namespace rrs
