#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/core/Mailbox.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/runtime/WorkerManager.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace rrs {

class MetricsRegistry;

class WorkerThread {
public:
    WorkerThread(WorkerId worker_id,
                 std::size_t worker_count,
                 MetricsRegistry& metrics);
    ~WorkerThread();

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    MailboxSender<WorkerMessage> inbox_sender() { return MailboxSender<WorkerMessage>{inbox_}; }
    void SetIoInboxes(std::vector<MailboxSender<IoMessage>> io_inboxes);

    void Start();
    void Stop();

private:
    using Clock = std::chrono::steady_clock;

    void Run(std::stop_token stop_token);

    Mailbox<WorkerMessage> inbox_;
    std::vector<MailboxSender<IoMessage>> io_inboxes_;
    WorkerId worker_id_;
    WorkerManager manager_;
    std::jthread thread_;

private:
    MetricsRegistry& metrics_;
};

} // namespace rrs
