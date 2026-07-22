#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/RoomManager.h"
#include "rrs/runtime/SessionManager.h"
#include "rrs/runtime/WorkerMessages.h"

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
                 std::chrono::nanoseconds tick_interval,
                 std::uint32_t max_catch_up_ticks,
                 std::size_t room_capacity,
                 MetricsRegistry& metrics);
    ~WorkerThread();

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    WorkerInboxSender inbox_sender() { return WorkerInboxSender{inbox_}; }
    void SetIoInboxes(std::vector<IoInboxSender> io_inboxes);

    void Start();
    void Stop();

private:
    using Clock = std::chrono::steady_clock;

    void Run(std::stop_token stop_token);

    WorkerInbox inbox_;
    std::vector<IoInboxSender> io_inboxes_;
    WorkerId worker_id_;
    std::uint32_t max_catch_up_ticks_;
    SessionManager sessions_;
    RoomManager rooms_;
    std::jthread thread_;

private:
    MetricsRegistry& metrics_;
};

} // namespace rrs
