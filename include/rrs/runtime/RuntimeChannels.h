#pragma once

#include "rrs/runtime/IoToWorkerMessage.h"
#include "rrs/runtime/WorkerToIoMessage.h"
#include "rrs/runtime/Mailbox.h"

namespace rrs {

using WorkerInbox = Mailbox<IoToWorkerMessage>;
using WorkerInboxSender = MailboxSender<IoToWorkerMessage>;

using IoInbox = Mailbox<WorkerToIoMessage>;
using IoInboxSender = MailboxSender<WorkerToIoMessage>;

} // namespace rrs
