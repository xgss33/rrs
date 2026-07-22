#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/observability/MetricsRegistry.h"
#include "rrs/runtime/Mailbox.h"
#include "rrs/runtime/WorkerMessages.h"

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

class IOThread {
public:
    IOThread(IoThreadId io_thread_id,
             std::vector<WorkerInboxSender> worker_inboxes,
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

    IoThreadId id() const { return io_thread_id_; }
    IoInboxSender inbox_sender() { return IoInboxSender{inbox_}; }

private:
    struct PendingWrite {
        std::shared_ptr<const std::string> encoded_frame;
        std::size_t offset{0};
    };

    struct ClientConnection {
        int fd{-1};
        enum class State {
            kAwaitingRequest,
            kPending,
            kActive,
            kClosing,
        } state{State::kAwaitingRequest};
        std::optional<WorkerId> worker_id;
        std::string read_buffer;
        std::deque<PendingWrite> outbound_queue;
        bool dirty{false};
        bool wants_write{false};
    };

    void Run(std::stop_token stop_token);
    void Wake();
    void SetClientWriteInterest(ConnectionId connection_id, ClientConnection& client, bool enabled);
    void HandleSocketEvent(ConnectionId connection_id, std::uint32_t events);
    [[nodiscard]] bool FlushClientOutbound(ConnectionId connection_id, ClientConnection& client);
    void CloseClient(ConnectionId connection_id);
    void NotifyDisconnect(ConnectionId connection_id, ClientConnection& client);
    [[nodiscard]] bool QueueEncodedFrame(
        ConnectionId connection_id,
        ClientConnection& client,
        std::shared_ptr<const std::string> encoded_frame);
    void QueueErrorFrame(ConnectionId connection_id, ClientConnection& client, const std::string& message);

    IoThreadId io_thread_id_;
    std::vector<WorkerInboxSender> worker_inboxes_;
    IoInbox inbox_;
    std::size_t outbound_queue_limit_;
    std::vector<ConnectionId> dirty_clients_;
    int epoll_fd_{-1};
    int wake_event_fd_{-1};
    Mailbox<int> accepted_clients_;
    std::unordered_map<ConnectionId, ClientConnection> clients_;
    ConnectionId::ValueType next_connection_id_{1};
    std::jthread thread_;

private:
    MetricsRegistry& metrics_;
    IoSendMetrics pending_send_metrics_;
};

} // namespace rrs
