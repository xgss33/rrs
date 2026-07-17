#pragma once

#include "rrs/runtime/Mailbox.h"
#include "rrs/runtime/Session.h"
#include "rrs/simulation/PlayerInput.h"

#include <memory>
#include <string>

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

struct WorkerToIoMessage {
    Session session;
    std::shared_ptr<const std::string> encoded_frame;

    [[nodiscard]] static WorkerToIoMessage MakeJoinOk(Session session, const std::string& snapshot_payload);
    [[nodiscard]] static WorkerToIoMessage MakeReconnectOk(Session session, const std::string& snapshot_payload);
    [[nodiscard]] static WorkerToIoMessage MakeError(Session session, const std::string& error_message);
};

using WorkerInbox = Mailbox<IoToWorkerMessage>;
using WorkerInboxSender = MailboxSender<IoToWorkerMessage>;

using IoInbox = Mailbox<WorkerToIoMessage>;
using IoInboxSender = MailboxSender<WorkerToIoMessage>;

} // namespace rrs
