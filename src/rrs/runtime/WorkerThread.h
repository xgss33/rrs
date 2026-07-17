#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/Session.h"
#include "rrs/runtime/WorkerChannels.h"
#include "rrs/runtime/WorkerRoomRegistry.h"
#include "rrs/simulation/Room.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace rrs {

class MetricsRegistry;

class WorkerThread {
public:
    WorkerThread(WorkerId worker_id,
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
    void DrainInbox(Clock::time_point frame_time);
    void HandleJoin(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void HandleReconnect(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void HandlePlayerInput(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void HandleLeave(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void TickDueRooms(Clock::time_point now);
    void HandleRoomTickResult(const Room& room, const Room::TickResult& result);
    void HandleRoomEvent(RoomId room_id,
                         const Room::TickResult& result,
                         const Room::Event& event,
                         std::vector<SessionId>& excluded_sessions);
    void PublishSnapshotUpdates(RoomId room_id,
                                const Room::TickResult& result,
                                const std::vector<SessionId>& excluded_sessions);
    void PushToIo(const Session& session, WorkerToIoMessage message);

    WorkerInbox inbox_;
    std::vector<IoInboxSender> io_inboxes_;
    WorkerId worker_id_;
    std::uint32_t max_catch_up_ticks_;
    WorkerRoomRegistry rooms_;
    std::jthread thread_;

private:
    MetricsRegistry& metrics_;
};

} // namespace rrs
