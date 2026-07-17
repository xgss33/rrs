#include "rrs/runtime/WorkerThread.h"

#include "rrs/core/Identifiers.h"
#include "rrs/core/Threading.h"
#include "rrs/observability/Logger.h"
#include "rrs/observability/MetricsRegistry.h"
#include "rrs/protocol/BinaryProtocol.h"
#include "rrs/runtime/Session.h"
#include "rrs/runtime/WorkerChannels.h"
#include "rrs/runtime/WorkerRoomRegistry.h"
#include "rrs/simulation/Room.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace rrs {

WorkerThread::WorkerThread(WorkerId worker_id,
                           std::chrono::nanoseconds tick_interval,
                           std::uint32_t max_catch_up_ticks,
                           std::size_t room_capacity,
                           MetricsRegistry& metrics)
    : worker_id_(worker_id)
    , max_catch_up_ticks_(std::max(1U, max_catch_up_ticks))
    , rooms_(worker_id, tick_interval, room_capacity)
    , metrics_(metrics)
{
}

WorkerThread::~WorkerThread()
{
    Stop();
}

void WorkerThread::SetIoInboxes(std::vector<IoInboxSender> io_inboxes)
{
    io_inboxes_ = std::move(io_inboxes);
}

void WorkerThread::Start()
{
    Logger::Info("[Worker] starting id={} dynamic_rooms=true", worker_id_.value());
    thread_ = std::jthread([this](std::stop_token stop_token) {
        Run(stop_token);
    });
}

void WorkerThread::Stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void WorkerThread::Run(std::stop_token stop_token)
{
    SetCurrentThreadName("rrs-w-" + std::to_string(worker_id_.value()));
    Logger::Info("[Worker] id={} loop started", worker_id_.value());

    while (!stop_token.stop_requested()) {
        const auto now = Clock::now();
        DrainInbox(now);
        TickDueRooms(now);

        const auto next_wake_time = rooms_.NextWakeTime(now + std::chrono::milliseconds{1});
        std::this_thread::sleep_until(next_wake_time);
    }

    Logger::Info("[Worker] id={} loop stopped", worker_id_.value());
}

void WorkerThread::DrainInbox(Clock::time_point frame_time)
{
    for (auto& message : inbox_.Drain()) {
        switch (message.type) {
        case IoToWorkerMessageType::kJoin:
            HandleJoin(message, frame_time);
            break;
        case IoToWorkerMessageType::kReconnect:
            HandleReconnect(message, frame_time);
            break;
        case IoToWorkerMessageType::kPlayerInput:
            HandlePlayerInput(message, frame_time);
            break;
        case IoToWorkerMessageType::kLeave:
            HandleLeave(message, frame_time);
            break;
        }
    }
}

void WorkerThread::HandleJoin(const IoToWorkerMessage& message, Clock::time_point entered_at)
{
    const auto& session = message.session;
    auto& room = rooms_.AssignRoomForJoin();
    rooms_.BindPendingSession(session, room.id());
    rooms_.UpdateRoomOpenState(room.id());
    room.EnqueueCommand(Room::Command{
        .type = Room::CommandType::kJoin,
        .session = session,
        .input = {},
        .entered_at = entered_at,
    });

    Logger::Info("[Worker] join player={} session={} room={} generation={}",
                 session.player_id.value(),
                 session.session_id.value(),
                 room.id().value(),
                 session.generation);
}

void WorkerThread::HandleReconnect(const IoToWorkerMessage& message, Clock::time_point entered_at)
{
    const auto& session = message.session;
    auto* binding = rooms_.FindBinding(session.session_id);
    if (binding == nullptr) {
        PushToIo(session, WorkerToIoMessage::MakeError(session, "PLAYER_NOT_FOUND"));
        return;
    }

    auto* room = rooms_.FindRoom(binding->room_id);
    if (room == nullptr) {
        PushToIo(session, WorkerToIoMessage::MakeError(session, "PLAYER_NOT_FOUND"));
        return;
    }

    rooms_.UpdateSession(session);
    room->EnqueueCommand(Room::Command{
        .type = Room::CommandType::kReconnect,
        .session = session,
        .input = {},
        .entered_at = entered_at,
    });

    Logger::Info("[Worker] reconnect player={} session={} room={} generation={}",
                 session.player_id.value(),
                 session.session_id.value(),
                 room->id().value(),
                 session.generation);
}

void WorkerThread::HandlePlayerInput(const IoToWorkerMessage& message, Clock::time_point entered_at)
{
    const auto& session = message.session;
    const auto* binding = rooms_.FindBinding(session.session_id);
    if (binding == nullptr || !binding->active) {
        return;
    }

    auto* room = rooms_.FindRoom(binding->room_id);
    if (room == nullptr) {
        return;
    }

    if (binding->generation != session.generation) {
        Logger::Warn("[Worker] reject stale input session={} player={} message_generation={}",
                     session.session_id.value(),
                     session.player_id.value(),
                     session.generation);
        return;
    }

    room->EnqueueCommand(Room::Command{
        .type = Room::CommandType::kPlayerInput,
        .session = session,
        .input = message.input,
        .entered_at = entered_at,
    });
}

void WorkerThread::HandleLeave(const IoToWorkerMessage& message, Clock::time_point entered_at)
{
    const auto& session = message.session;
    const auto* binding = rooms_.FindBinding(session.session_id);
    if (binding == nullptr) {
        return;
    }

    auto* room = rooms_.FindRoom(binding->room_id);
    if (room == nullptr) {
        return;
    }

    if (binding->generation != session.generation) {
        return;
    }

    room->EnqueueCommand(Room::Command{
        .type = Room::CommandType::kLeave,
        .session = session,
        .input = {},
        .entered_at = entered_at,
    });

    Logger::Info("[Worker] leave player={} session={} room={}",
                 session.player_id.value(),
                 session.session_id.value(),
                 room->id().value());
}

void WorkerThread::TickDueRooms(Clock::time_point now)
{
    const auto tick_start = Clock::now();
    rooms_.TickDueRooms(now, max_catch_up_ticks_, [this](const Room& room, const Room::TickResult& result) {
        HandleRoomTickResult(room, result);
    });
    const auto tick_cost = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - tick_start).count();
    metrics_.SetWorkerTickCostUs(worker_id_, static_cast<std::uint64_t>(std::max<std::int64_t>(0, tick_cost)));
}

void WorkerThread::HandleRoomTickResult(const Room& room, const Room::TickResult& result)
{
    const auto room_id = room.id();
    const auto snapshot_payload = EncodeSnapshotPayload(result.broadcast_snapshot);
    auto snapshot_frame_bytes = EncodeFrame(ServerMessageType::kSnapshot, snapshot_payload);
    const auto encoded_snapshot_frame = std::make_shared<const std::string>(std::move(snapshot_frame_bytes));
    const auto full_snapshot_payload = result.full_snapshot.has_value() ? EncodeSnapshotPayload(*result.full_snapshot) : std::string{};
    auto excluded_sessions = std::vector<SessionId>{};
    excluded_sessions.reserve(result.events.size());

    for (const auto& event : result.events) {
        HandleRoomEvent(room_id, full_snapshot_payload, event, excluded_sessions);
    }

    PublishSnapshot(room_id, encoded_snapshot_frame, excluded_sessions);
}

void WorkerThread::HandleRoomEvent(RoomId room_id,
                                   const std::string& full_snapshot_payload,
                                   const Room::Event& event,
                                   std::vector<SessionId>& excluded_sessions)
{
    switch (event.type) {
    case Room::EventType::kJoinAccepted:
        rooms_.ActivateSession(event.session.session_id);
        rooms_.UpdateRoomOpenState(room_id);
        PushToIo(event.session, WorkerToIoMessage::MakeJoinOk(event.session, full_snapshot_payload));
        excluded_sessions.push_back(event.session.session_id);
        return;
    case Room::EventType::kReconnectAccepted:
        PushToIo(event.session, WorkerToIoMessage::MakeReconnectOk(event.session, full_snapshot_payload));
        excluded_sessions.push_back(event.session.session_id);
        return;
    case Room::EventType::kReconnectRejected:
        PushToIo(event.session, WorkerToIoMessage::MakeError(event.session, "PLAYER_NOT_FOUND"));
        excluded_sessions.push_back(event.session.session_id);
        return;
    case Room::EventType::kPlayerLeft:
        rooms_.RemoveSession(event.session.session_id);
        rooms_.HandleRoomAfterLeave(room_id);
        excluded_sessions.push_back(event.session.session_id);
        return;
    }
}

void WorkerThread::PublishSnapshot(RoomId room_id,
                                   std::shared_ptr<const std::string> encoded_snapshot_frame,
                                   const std::vector<SessionId>& excluded_sessions)
{
    rooms_.ForEachActiveSessionInRoom(room_id, [this, &encoded_snapshot_frame, &excluded_sessions](const Session& session) {
        if (std::find(excluded_sessions.begin(), excluded_sessions.end(), session.session_id) != excluded_sessions.end()) {
            return;
        }

        PushToIo(session, WorkerToIoMessage{
                              .session = session,
                              .encoded_frame = encoded_snapshot_frame,
                          });
    });
}

void WorkerThread::PushToIo(const Session& session, WorkerToIoMessage message)
{
    const auto io_index = static_cast<std::size_t>(session.io_thread_id.value());
    if (io_index >= io_inboxes_.size()) {
        Logger::Warn("[Worker] drop message session={} reason=io_inbox_not_found", session.session_id.value());
        return;
    }

    if (!io_inboxes_[io_index].Push(std::move(message))) {
        Logger::Warn("[Worker] drop message session={} reason=io_inbox_push_failed", session.session_id.value());
    }
}

} // namespace rrs
