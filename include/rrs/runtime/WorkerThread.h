#pragma once

#include "rrs/base/Types.h"
#include "rrs/runtime/IoToWorkerMessage.h"
#include "rrs/game/Room.h"
#include "rrs/runtime/RuntimeChannels.h"
#include "rrs/runtime/WorkerRoomRegistry.h"
#include "rrs/runtime/WorkerToIoMessage.h"
#include "rrs/runtime/Session.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace rrs {

class MetricsRegistry;

class WorkerThread {
public:
    WorkerThread(std::string name,
                 WorkerId worker_id,
                 std::chrono::nanoseconds tick_interval,
                 std::uint32_t max_catch_up_ticks,
                 std::size_t room_capacity,
                 MetricsRegistry& metrics);
    ~WorkerThread();

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    [[nodiscard]] WorkerInboxSender inbox_sender() noexcept { return WorkerInboxSender{inbox_}; }
    void SetIoInboxes(std::vector<IoInboxSender> io_inboxes);

    void Start();
    void Stop();

private:
    using Clock = std::chrono::steady_clock;

    void Run(std::stop_token stop_token);
    void DrainInbox(Clock::time_point frame_time);
    void HandleJoin(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void HandleReconnect(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void HandlePlayerInput(IoToWorkerMessage message, Clock::time_point entered_at);
    void HandleLeave(const IoToWorkerMessage& message, Clock::time_point entered_at);
    void TickDueRooms(Clock::time_point now);
    void HandleRoomTickResult(const Room& room, const Room::TickResult& result);
    void HandleRoomEvent(RoomId room_id,
                         const std::string& full_snapshot_payload,
                         const Room::Event& event,
                         std::vector<SessionId>& excluded_sessions);
    void PublishSnapshot(RoomId room_id,
                         std::shared_ptr<const std::string> encoded_snapshot_frame,
                         const std::vector<SessionId>& excluded_sessions);
    void PushToIo(const Session& session, WorkerToIoMessage message);

    std::string name_;
    WorkerInbox inbox_;  // 所有的io都向这里输入
    std::vector<IoInboxSender> io_inboxes_;  // 向各个io输出
    WorkerId worker_id_;
    std::uint32_t max_catch_up_ticks_;
    MetricsRegistry& metrics_;
    WorkerRoomRegistry rooms_;  // 对拥有的房间以及操作房间的函数的封装, 为了减少worker文件大小, 提高可读
    std::jthread thread_;
};

} // namespace rrs
