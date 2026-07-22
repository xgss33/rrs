#include "rrs/runtime/WorkerThread.h"

#include "rrs/core/Identifiers.h"
#include "rrs/core/Threading.h"
#include "rrs/observability/Logger.h"
#include "rrs/observability/MetricsRegistry.h"
#include "rrs/protocol/BinaryProtocol.h"
#include "rrs/runtime/ConnectionHandle.h"
#include "rrs/runtime/RoomManager.h"
#include "rrs/runtime/Session.h"
#include "rrs/runtime/SessionManager.h"
#include "rrs/runtime/WorkerMessages.h"
#include "rrs/simulation/Room.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace rrs {

WorkerThread::WorkerThread(WorkerId worker_id,
                           std::size_t worker_count,
                           std::chrono::nanoseconds tick_interval,
                           std::uint32_t max_catch_up_ticks,
                           std::size_t room_capacity,
                           MetricsRegistry& metrics)
    : worker_id_(worker_id)
    , max_catch_up_ticks_(max_catch_up_ticks)
    , sessions_(worker_id, worker_count)
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
            if (const auto* join = std::get_if<JoinMessage>(&message)) {
                if (sessions_.FindByPlayer(join->player_id) != nullptr) {
                    const auto io_index = static_cast<std::size_t>(join->connection.io_thread_id.value());
                    (void)io_inboxes_[io_index].Push(WorkerToIoMessage{
                        .connection = join->connection,
                        .encoded_frame = std::make_shared<const std::string>(
                            EncodeFrame(ServerMessageType::kError, "PLAYER_ALREADY_JOINED")),
                        .activate_connection = false,
                        .close_after_send = true,
                    });
                    continue;
                }

                auto& room = rooms_.AssignRoomForJoin();
                const auto session_id = sessions_.Create(join->player_id, room.id(), join->connection);
                rooms_.AddSession(room.id(), session_id);
                room.EnqueueCommand(Room::Command{
                    .type = Room::CommandType::kJoin,
                    .session_id = session_id,
                    .player_id = join->player_id,
                    .input = {},
                    .entered_at = now,
                });
                Logger::Info("[Worker] join player={} session={} room={}",
                             join->player_id.value(), session_id.value(), room.id().value());
                continue;
            }

            if (const auto* reconnect = std::get_if<ReconnectMessage>(&message)) {
                auto* session = sessions_.Find(reconnect->session_id);
                if (session == nullptr || session->state != SessionState::kActive) {
                    const auto io_index = static_cast<std::size_t>(reconnect->connection.io_thread_id.value());
                    (void)io_inboxes_[io_index].Push(WorkerToIoMessage{
                        .connection = reconnect->connection,
                        .encoded_frame = std::make_shared<const std::string>(
                            EncodeFrame(ServerMessageType::kError, "SESSION_NOT_FOUND")),
                        .activate_connection = false,
                        .close_after_send = true,
                    });
                    continue;
                }

                auto& room = *rooms_.FindRoom(session->room_id);
                const auto update = room.BuildSnapshot(session->player_id, true, false);
                const auto old_connection = session->connection;
                (void)sessions_.Bind(reconnect->session_id, reconnect->connection);

                if (old_connection) {
                    const auto io_index = static_cast<std::size_t>(old_connection->io_thread_id.value());
                    (void)io_inboxes_[io_index].Push(WorkerToIoMessage{
                        .connection = *old_connection,
                        .encoded_frame = nullptr,
                        .activate_connection = false,
                        .close_after_send = true,
                    });
                }

                const auto food_baseline = room.BuildFoodSnapshotBaseline();
                const auto io_index = static_cast<std::size_t>(reconnect->connection.io_thread_id.value());
                (void)io_inboxes_[io_index].Push(WorkerToIoMessage{
                    .connection = reconnect->connection,
                    .encoded_frame = std::make_shared<const std::string>(EncodeFrame(
                        ServerMessageType::kReconnectOk,
                        EncodeSessionPayload(
                            reconnect->session_id,
                            EncodeSnapshotPayload(*update, food_baseline)))),
                    .activate_connection = true,
                    .close_after_send = false,
                });
                Logger::Info("[Worker] reconnect player={} session={} room={}",
                             session->player_id.value(), reconnect->session_id.value(), session->room_id.value());
                continue;
            }

            if (const auto* input = std::get_if<InputMessage>(&message)) {
                const auto session_id = sessions_.FindIdByConnection(input->connection);
                if (!session_id) {
                    continue;
                }

                const auto& session = *sessions_.Find(*session_id);
                rooms_.FindRoom(session.room_id)->EnqueueCommand(Room::Command{
                    .type = Room::CommandType::kPlayerInput,
                    .session_id = *session_id,
                    .player_id = session.player_id,
                    .input = input->input,
                    .entered_at = now,
                });
                continue;
            }

            const auto& connection = std::get<ConnectionMessage>(message);
            const auto session_id = sessions_.FindIdByConnection(connection.connection);
            if (!session_id) {
                continue;
            }

            auto& session = *sessions_.Find(*session_id);
            if (connection.action == ConnectionAction::kLeave || session.state == SessionState::kJoining) {
                const auto removed = sessions_.Remove(*session_id);
                rooms_.RemoveSession(removed.room_id, *session_id);
                rooms_.FindRoom(removed.room_id)->EnqueueCommand(Room::Command{
                    .type = Room::CommandType::kLeave,
                    .session_id = *session_id,
                    .player_id = removed.player_id,
                    .input = {},
                    .entered_at = now,
                });
                Logger::Info("[Worker] leave player={} session={} room={}",
                             removed.player_id.value(), session_id->value(), removed.room_id.value());
                continue;
            }

            rooms_.FindRoom(session.room_id)->RemoveSnapshotObserver(session.player_id);
            sessions_.Unbind(connection.connection);
            Logger::Info("[Worker] disconnected session={} player={}",
                         session_id->value(), session.player_id.value());
        }

        auto room_ticked = false;
        rooms_.TickDueRooms(
            now,
            max_catch_up_ticks_,
            [this, &room_ticked](Room& room,
                                 const Room::TickResult& result,
                                 Clock::time_point scheduled_tick_time) {
                room_ticked = true;
                const auto room_id = room.id();
                auto joined_sessions = std::vector<SessionId>{};
                joined_sessions.reserve(result.events.size());

                for (const auto& event : result.events) {
                    if (event.type == Room::EventType::kJoinAccepted) {
                        if (sessions_.Find(event.session_id) != nullptr) {
                            sessions_.Activate(event.session_id);
                            joined_sessions.push_back(event.session_id);
                        }
                    } else {
                        rooms_.HandleRoomAfterLeave(room_id);
                    }
                }

                if (auto* live_room = rooms_.FindRoom(room_id)) {
                    auto messages_by_io = std::vector<std::vector<WorkerToIoMessage>>(io_inboxes_.size());
                    auto food_baseline = std::optional<std::vector<FoodSnapshotUpdate>>{};

                    for (const auto session_id : rooms_.Sessions(room_id)) {
                        const auto& session = *sessions_.Find(session_id);
                        if (!session.connection || session.state != SessionState::kActive) {
                            continue;
                        }

                        const auto joined = std::find(joined_sessions.begin(), joined_sessions.end(), session_id)
                            != joined_sessions.end();
                        const auto update = live_room->BuildSnapshot(
                            session.player_id,
                            joined,
                            !result.food_updates.empty());
                        if (!update) {
                            continue;
                        }

                        if (joined && !food_baseline) {
                            food_baseline = live_room->BuildFoodSnapshotBaseline();
                        }
                        const auto& food_updates = joined ? *food_baseline : result.food_updates;
                        const auto payload = EncodeSnapshotPayload(*update, food_updates);
                        const auto frame_payload = joined
                            ? EncodeSessionPayload(session_id, payload)
                            : payload;
                        auto outgoing = WorkerToIoMessage{
                            .connection = *session.connection,
                            .encoded_frame = std::make_shared<const std::string>(EncodeFrame(
                                joined ? ServerMessageType::kJoinOk : ServerMessageType::kSnapshot,
                                frame_payload)),
                            .activate_connection = joined,
                            .close_after_send = result.match_ended,
                        };
                        const auto io_index = static_cast<std::size_t>(outgoing.connection.io_thread_id.value());
                        messages_by_io[io_index].push_back(std::move(outgoing));
                    }

                    for (std::size_t io_index = 0; io_index < messages_by_io.size(); ++io_index) {
                        if (!messages_by_io[io_index].empty()) {
                            (void)io_inboxes_[io_index].PushBatch(std::move(messages_by_io[io_index]));
                        }
                    }

                    if (result.match_ended) {
                        auto session_ids = rooms_.RemoveRoom(room_id);
                        for (const auto session_id : session_ids) {
                            (void)sessions_.Remove(session_id);
                        }
                        Logger::Info("[Worker] match ended worker={} room={} sessions={}",
                                     worker_id_.value(), room_id.value(), session_ids.size());
                    }
                }

                const auto response_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - scheduled_tick_time);
                metrics_.RecordRoomTickResponseTimeUs(
                    worker_id_,
                    static_cast<std::uint64_t>(response_time.count()));
            });

        if (room_ticked) {
            const auto room_metrics = rooms_.CollectMetrics();
            metrics_.SetWorkerRoomMetrics(
                worker_id_,
                room_metrics.static_entities,
                room_metrics.dynamic_entities,
                room_metrics.visibility_observers,
                room_metrics.visible_other_player_balls);
        }

        const auto next_wake_time = rooms_.NextWakeTime(now + std::chrono::milliseconds{1});
        std::this_thread::sleep_until(next_wake_time);
    }

    Logger::Info("[Worker] id={} loop stopped", worker_id_.value());
}

} // namespace rrs
