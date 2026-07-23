#include "rrs/runtime/WorkerThread.h"

#include "rrs/core/Identifiers.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/core/Threading.h"
#include "rrs/observability/Logger.h"
#include "rrs/observability/MetricsRegistry.h"
#include "rrs/protocol/BinaryProtocol.h"
#include "rrs/runtime/WorkerManager.h"
#include "rrs/simulation/Room.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace rrs {

WorkerThread::WorkerThread(WorkerId worker_id,
                           std::size_t worker_count,
                           MetricsRegistry& metrics)
    : worker_id_(worker_id)
    , manager_(worker_id, worker_count)
    , metrics_(metrics)
{
}

WorkerThread::~WorkerThread()
{
    Stop();
}

void WorkerThread::SetIoInboxes(std::vector<MailboxSender<IoMessage>> io_inboxes)
{
    io_inboxes_ = std::move(io_inboxes);
}

void WorkerThread::Start()
{
    Logger::Info("[Worker] starting id={} dynamic_rooms=true", worker_id_.value());
    thread_ = std::jthread([this](std::stop_token stop_token) { Run(stop_token); });
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

        for (auto& message : inbox_.Drain()) {
            if (const auto* player_id = std::get_if<PlayerId>(&message.payload)) {
                if (manager_.FindSessionByPlayer(*player_id) != nullptr) {
                    const auto io_index = static_cast<std::size_t>(message.connection.io_thread_id.value());
                    io_inboxes_[io_index].Push(IoMessage{
                        .connection = message.connection,
                        .frame = EncodeFrame(ServerMessageType::kError, "PLAYER_ALREADY_JOINED"),
                        .action = ConnectionAction::kClose,
                    });
                    continue;
                }

                const auto session_id = manager_.Join(*player_id, message.connection, now);
                Logger::Info("[Worker] join player={} session={} room={}",
                             player_id->value(),
                             session_id.value(),
                             manager_.RoomFor(session_id).id().value());
                continue;
            }

            if (const auto* session_id = std::get_if<SessionId>(&message.payload)) {
                auto* session = manager_.FindSession(*session_id);
                if (session == nullptr || session->state != SessionState::kActive) {
                    const auto io_index = static_cast<std::size_t>(message.connection.io_thread_id.value());
                    io_inboxes_[io_index].Push(IoMessage{
                        .connection = message.connection,
                        .frame = EncodeFrame(ServerMessageType::kError, "SESSION_NOT_FOUND"),
                        .action = ConnectionAction::kClose,
                    });
                    continue;
                }

                auto& room = manager_.RoomFor(*session_id);
                const auto update = room.BuildSnapshot(session->player_id, true, false);
                const auto food_baseline = room.BuildFoodSnapshotBaseline();
                const auto old_connection = manager_.Bind(*session, message.connection);

                if (old_connection) {
                    const auto io_index = static_cast<std::size_t>(old_connection->io_thread_id.value());
                    io_inboxes_[io_index].Push(IoMessage{
                        .connection = *old_connection,
                        .frame = {},
                        .action = ConnectionAction::kClose,
                    });
                }

                const auto io_index = static_cast<std::size_t>(message.connection.io_thread_id.value());
                io_inboxes_[io_index].Push(IoMessage{
                    .connection = message.connection,
                    .frame = EncodeFrame(
                        ServerMessageType::kFullSnapshot,
                        EncodeFullSnapshotPayload(
                            *session_id,
                            EncodeSnapshotPayload(*update, food_baseline))),
                    .action = ConnectionAction::kActivate,
                });
                Logger::Info("[Worker] reconnect player={} session={} room={}",
                             session->player_id.value(),
                             session_id->value(),
                             room.id().value());
                continue;
            }

            if (const auto* input = std::get_if<PlayerInput>(&message.payload)) {
                manager_.EnqueueInput(message.connection, *input, now);
                continue;
            }

            const auto event = std::get<ConnectionEvent>(message.payload);
            auto* session = manager_.FindSessionByConnection(message.connection);
            if (session == nullptr) {
                continue;
            }
            const auto session_id = session->id;
            const auto player_id = session->player_id;
            if (event == ConnectionEvent::kLeave) {
                manager_.Leave(session_id, now);
                Logger::Info("[Worker] leave player={} session={}", player_id.value(), session_id.value());
            } else {
                manager_.Disconnect(session_id, now);
                Logger::Info("[Worker] disconnected session={} player={}", session_id.value(), player_id.value());
            }
        }

        const auto room_ticked = manager_.TickDueRooms(
            now,
            [this](Room& room,
                   std::span<const Session> sessions,
                   const Room::TickResult& result,
                   Clock::time_point scheduled_tick_time) {
                auto messages_by_io = std::vector<std::vector<IoMessage>>(io_inboxes_.size());
                auto food_baseline = std::optional<std::vector<FoodSnapshotUpdate>>{};

                for (const auto& session : sessions) {
                    if (!session.connection || session.state != SessionState::kActive) {
                        continue;
                    }

                    const auto joined = std::find(
                        result.joined_players.begin(),
                        result.joined_players.end(),
                        session.player_id) != result.joined_players.end();
                    const auto update = room.BuildSnapshot(
                        session.player_id,
                        joined,
                        !result.food_updates.empty());
                    if (!update) {
                        continue;
                    }

                    if (joined && !food_baseline) {
                        food_baseline = room.BuildFoodSnapshotBaseline();
                    }
                    const auto& food_updates = joined ? *food_baseline : result.food_updates;
                    auto payload = EncodeSnapshotPayload(*update, food_updates);
                    if (joined) {
                        payload = EncodeFullSnapshotPayload(session.id, payload);
                    }

                    auto outgoing = IoMessage{
                        .connection = *session.connection,
                        .frame = EncodeFrame(
                            joined ? ServerMessageType::kFullSnapshot : ServerMessageType::kDeltaSnapshot,
                            payload),
                        .action = result.match_ended
                            ? ConnectionAction::kClose
                            : (joined ? ConnectionAction::kActivate : ConnectionAction::kNone),
                    };
                    const auto io_index = static_cast<std::size_t>(outgoing.connection.io_thread_id.value());
                    messages_by_io[io_index].push_back(std::move(outgoing));
                }

                for (std::size_t io_index = 0; io_index < messages_by_io.size(); ++io_index) {
                    if (!messages_by_io[io_index].empty()) {
                        io_inboxes_[io_index].PushBatch(std::move(messages_by_io[io_index]));
                    }
                }

                if (result.match_ended) {
                    Logger::Info("[Worker] match ended worker={} room={} sessions={}",
                                 worker_id_.value(), room.id().value(), sessions.size());
                }

                const auto response_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - scheduled_tick_time);
                metrics_.RecordRoomTickResponseTimeUs(
                    worker_id_,
                    static_cast<std::uint64_t>(response_time.count()));
            });

        if (room_ticked) {
            const auto room_metrics = manager_.CollectMetrics();
            metrics_.SetWorkerRoomMetrics(
                worker_id_,
                room_metrics.static_entities,
                room_metrics.dynamic_entities,
                room_metrics.visibility_observers,
                room_metrics.visible_other_player_balls);
        }

        const auto next_wake_time = manager_.NextWakeTime(now + std::chrono::milliseconds{1});
        std::this_thread::sleep_until(next_wake_time);
    }

    Logger::Info("[Worker] id={} loop stopped", worker_id_.value());
}

} // namespace rrs
