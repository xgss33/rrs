#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/ConnectionHandle.h"
#include "rrs/runtime/Mailbox.h"
#include "rrs/simulation/PlayerInput.h"

#include <memory>
#include <string>
#include <variant>

namespace rrs {

struct JoinMessage {
    ConnectionHandle connection;
    PlayerId player_id;
};

struct ReconnectMessage {
    ConnectionHandle connection;
    SessionId session_id;
};

struct InputMessage {
    ConnectionHandle connection;
    PlayerInput input;
};

enum class ConnectionAction {
    kLeave,
    kDisconnected,
};

struct ConnectionMessage {
    ConnectionHandle connection;
    ConnectionAction action{ConnectionAction::kDisconnected};
};

using IoToWorkerMessage = std::variant<JoinMessage, ReconnectMessage, InputMessage, ConnectionMessage>;

struct WorkerToIoMessage {
    ConnectionHandle connection;
    std::shared_ptr<const std::string> encoded_frame;
    bool activate_connection{false};
    bool close_after_send{false};
};

using WorkerInbox = Mailbox<IoToWorkerMessage>;
using WorkerInboxSender = MailboxSender<IoToWorkerMessage>;

using IoInbox = Mailbox<WorkerToIoMessage>;
using IoInboxSender = MailboxSender<WorkerToIoMessage>;

} // namespace rrs
