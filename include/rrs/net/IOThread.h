#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/PlayerInput.h"
#include "rrs/metrics/MetricsRegistry.h"
#include "rrs/runtime/IoToWorkerMessage.h"
#include "rrs/runtime/Mailbox.h"
#include "rrs/runtime/RuntimeChannels.h"
#include "rrs/runtime/Session.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rrs {

struct BinaryFrame;

class SessionRegistry;

class IOThread {
public:
    IOThread(IoThreadId io_thread_id,
             std::vector<WorkerInboxSender> worker_inboxes,
             SessionRegistry& session_registry,
             MetricsRegistry& metrics,
             std::size_t outbound_queue_limit);
    ~IOThread();

    IOThread(const IOThread&) = delete;
    IOThread& operator=(const IOThread&) = delete;
    IOThread(IOThread&&) = delete;
    IOThread& operator=(IOThread&&) = delete;

    void Start();
    void Stop();
    void EnqueueAcceptedClient(int client_fd);

    [[nodiscard]] IoThreadId id() const noexcept { return io_thread_id_; }
    [[nodiscard]] IoInboxSender inbox_sender() noexcept { return IoInboxSender{inbox_}; }

private:
    struct PendingWrite {
        std::shared_ptr<const std::string> encoded_frame;
        std::size_t offset{0};
    };

    struct ClientConnection {
        int fd{-1};
        std::string read_buffer;
        std::optional<Session> session;
        std::deque<PendingWrite> outbound_queue;
        bool dirty{false};
        bool wants_write{false};
    };

    void Run(std::stop_token stop_token);
    void Wake();
    void SetClientWriteInterest(ClientConnection& client, bool enabled);
    void DrainWakeEvent();
    void DrainAcceptedClients();
    void DrainInbox();
    void FlushDirtyClients();
    void PollSocketEvents(std::stop_token stop_token);
    void HandleSocketEvent(int client_fd, std::uint32_t events);
    [[nodiscard]] bool ReadClientFrames(ClientConnection& client, std::vector<BinaryFrame>& ready_frames);
    [[nodiscard]] bool FlushClientOutbound(ClientConnection& client);
    void CloseClient(int client_fd);
    void HandleBinaryFrame(ClientConnection& client, const BinaryFrame& frame);
    void HandleJoin(ClientConnection& client, PlayerId player_id);
    void HandleReconnect(ClientConnection& client, SessionId session_id);
    void HandleInput(ClientConnection& client, PlayerInput input);
    void HandleLeave(ClientConnection& client);
    void BindClientSession(ClientConnection& client, const Session& session);
    void UnbindClientSession(ClientConnection& client);
    void QueueEncodedFrame(ClientConnection& client, std::shared_ptr<const std::string> encoded_frame);
    void QueueErrorFrame(ClientConnection& client, const std::string& message);
    [[nodiscard]] bool IsCurrentClientSession(const ClientConnection& client, const Session& session) const;
    [[nodiscard]] WorkerId SelectWorkerForJoin(PlayerId player_id) const;
    [[nodiscard]] bool PushToWorker(WorkerId worker_id, IoToWorkerMessage message);
    void PublishSendMetrics();

    IoThreadId io_thread_id_;
    std::vector<WorkerInboxSender> worker_inboxes_;
    IoInbox inbox_;
    std::size_t outbound_queue_limit_;
    SessionRegistry& session_registry_;
    std::unordered_map<SessionId, int> client_fd_by_session_;
    std::vector<int> dirty_clients_;
    int epoll_fd_{-1};
    int wake_event_fd_{-1};
    Mailbox<int> accepted_clients_;
    std::unordered_map<int, ClientConnection> clients_;
    std::jthread thread_;
    MetricsRegistry& metrics_;
    IoSendMetrics pending_send_metrics_;
};

} // namespace rrs
