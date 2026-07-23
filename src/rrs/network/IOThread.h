#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/core/Mailbox.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/observability/MetricsRegistry.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rrs {

class IoThread {
public:
    IoThread(IoThreadId io_thread_id,
             std::vector<MailboxSender<WorkerMessage>> worker_inboxes,
             MetricsRegistry& metrics);
    ~IoThread();

    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;
    IoThread(IoThread&&) = delete;
    IoThread& operator=(IoThread&&) = delete;

    void Start();
    void Stop();
    void EnqueueAcceptedClient(int client_fd);

    IoThreadId id() const { return io_thread_id_; }
    MailboxSender<IoMessage> inbox_sender() { return MailboxSender<IoMessage>{inbox_}; }

private:
    struct PendingWrite {
        std::string frame;
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
        WorkerId worker_id;
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
        std::string frame);
    void QueueErrorFrame(ConnectionId connection_id, ClientConnection& client, const std::string& message);

    IoThreadId io_thread_id_;
    std::vector<MailboxSender<WorkerMessage>> worker_inboxes_;
    Mailbox<IoMessage> inbox_;
    std::vector<ConnectionId> dirty_clients_;
    int epoll_fd_{-1};
    int wake_event_fd_{-1};
    Mailbox<int> accepted_clients_;
    std::unordered_map<ConnectionId, ClientConnection> clients_;
    std::uint64_t next_connection_sequence_{1};
    std::jthread thread_;

private:
    MetricsRegistry& metrics_;
    IoSendMetrics pending_send_metrics_;
};

} // namespace rrs
