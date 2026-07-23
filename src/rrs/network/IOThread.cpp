#include "rrs/network/IOThread.h"

#include "rrs/core/Identifiers.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/core/Threading.h"
#include "rrs/observability/Logger.h"
#include "rrs/observability/MetricsRegistry.h"
#include "rrs/protocol/BinaryProtocol.h"
#include "rrs/runtime/WorkerManager.h"
#include "rrs/simulation/PlayerInput.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace rrs {

namespace {

constexpr std::size_t kOutboundQueueLimit = 16;

bool IsPeerDisconnectError(int error_number)
{
    return error_number == ECONNRESET || error_number == EPIPE;
}

} // namespace

IoThread::IoThread(IoThreadId io_thread_id,
                   std::vector<MailboxSender<WorkerMessage>> worker_inboxes,
                   MetricsRegistry& metrics)
    : io_thread_id_(io_thread_id)
    , worker_inboxes_(std::move(worker_inboxes))
    , inbox_([this] { Wake(); })
    , accepted_clients_([this] { Wake(); })
    , metrics_(metrics)
{
}

IoThread::~IoThread()
{
    Stop();
}

void IoThread::Start()
{
    wake_event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_event_fd_ < 0) {
        throw std::runtime_error("failed to create IO wake eventfd");
    }

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("failed to create IO epoll fd");
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = 0;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_event_fd_, &event) < 0) {
        throw std::runtime_error("failed to register IO wake eventfd");
    }

    Logger::Info("[IO] starting TCP IO thread id={}", io_thread_id_.value());
    thread_ = std::jthread([this](std::stop_token stop_token) { Run(stop_token); });
}

void IoThread::Stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        Wake();
        thread_.join();
    }

    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (wake_event_fd_ >= 0) {
        ::close(wake_event_fd_);
        wake_event_fd_ = -1;
    }
}

void IoThread::Wake()
{
    if (wake_event_fd_ < 0) {
        return;
    }

    const auto wake_value = std::uint64_t{1};
    while (true) {
        const auto bytes_written = ::write(wake_event_fd_, &wake_value, sizeof(wake_value));
        if (bytes_written == sizeof(wake_value)) {
            return;
        }
        if (bytes_written < 0 && errno == EINTR) {
            continue;
        }
        if (bytes_written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        Logger::Warn("[IO] wake eventfd write failed id={} error={}", io_thread_id_.value(), std::strerror(errno));
        return;
    }
}

void IoThread::EnqueueAcceptedClient(int client_fd)
{
    accepted_clients_.Push(client_fd);
}

void IoThread::Run(std::stop_token stop_token)
{
    SetCurrentThreadName("rrs-io-" + std::to_string(io_thread_id_.value()));
    while (!stop_token.stop_requested()) {
        for (const auto client_fd : accepted_clients_.Drain()) {
            const auto flags = ::fcntl(client_fd, F_GETFL, 0);
            if (flags < 0 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                Logger::Warn("[IO] failed to set nonblocking fd={}", client_fd);
                ::close(client_fd);
                continue;
            }

            const auto connection_id = ConnectionId{next_connection_sequence_++};
            epoll_event event{};
            event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
            event.data.u64 = connection_id.value();
            if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                Logger::Warn("[IO] failed to register client fd={} error={}", client_fd, std::strerror(errno));
                ::close(client_fd);
                continue;
            }

            clients_.emplace(connection_id, ClientConnection{
                                                .fd = client_fd,
                                                .state = ClientConnection::State::kAwaitingRequest,
                                                .worker_id = WorkerId{0},
                                                .read_buffer = {},
                                                .outbound_queue = {},
                                                .dirty = false,
                                                .wants_write = false,
                                            });
            metrics_.OnConnectionOpened();
            Logger::Info("[IO] client connected connection={} fd={}", connection_id.value(), client_fd);
        }

        for (auto& message : inbox_.Drain()) {
            auto iterator = clients_.find(message.connection.connection_id);
            if (iterator == clients_.end()) {
                continue;
            }
            auto& client = iterator->second;
            if (message.action == ConnectionAction::kActivate) {
                if (client.state != ClientConnection::State::kPending) {
                    continue;
                }
                client.state = ClientConnection::State::kActive;
            }
            if (message.action == ConnectionAction::kClose) {
                client.state = ClientConnection::State::kClosing;
            }
            const auto has_frame = !message.frame.empty();
            if (has_frame
                && !QueueEncodedFrame(message.connection.connection_id, client, std::move(message.frame))) {
                Logger::Warn("[IO] close slow client connection={} outbound_queue_limit={}",
                             message.connection.connection_id.value(), kOutboundQueueLimit);
                CloseClient(message.connection.connection_id);
                continue;
            }
            if (!has_frame && message.action == ConnectionAction::kClose) {
                CloseClient(message.connection.connection_id);
            }
        }

        auto dirty_clients = std::move(dirty_clients_);
        dirty_clients_.clear();
        for (const auto connection_id : dirty_clients) {
            auto iterator = clients_.find(connection_id);
            if (iterator == clients_.end()) {
                continue;
            }
            auto& client = iterator->second;
            client.dirty = false;
            if (!FlushClientOutbound(connection_id, client)) {
                CloseClient(connection_id);
                continue;
            }
            if (client.state == ClientConnection::State::kClosing && client.outbound_queue.empty()) {
                CloseClient(connection_id);
            }
        }

        if (pending_send_metrics_.send_calls != 0
            || pending_send_metrics_.nonempty_flushes != 0
            || pending_send_metrics_.frames_at_flush != 0) {
            metrics_.MergeIoSendMetrics(pending_send_metrics_);
            pending_send_metrics_ = {};
        }

        constexpr int kMaxEvents = 64;
        std::array<epoll_event, kMaxEvents> events{};
        const auto event_count = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), -1);
        if (event_count < 0) {
            if (errno != EINTR) {
                Logger::Warn("[IO] epoll_wait failed id={} error={}", io_thread_id_.value(), std::strerror(errno));
            }
            continue;
        }

        for (int index = 0; index < event_count && !stop_token.stop_requested(); ++index) {
            const auto& event = events[static_cast<std::size_t>(index)];
            if (event.data.u64 != 0) {
                HandleSocketEvent(ConnectionId{event.data.u64}, event.events);
                continue;
            }

            while (true) {
                std::uint64_t value = 0;
                const auto bytes_read = ::read(wake_event_fd_, &value, sizeof(value));
                if (bytes_read == sizeof(value)) {
                    continue;
                }
                if (bytes_read < 0 && errno == EINTR) {
                    continue;
                }
                if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    Logger::Warn("[IO] wake eventfd read failed id={} error={}",
                                 io_thread_id_.value(), std::strerror(errno));
                }
                break;
            }
        }
    }

    if (pending_send_metrics_.send_calls != 0
        || pending_send_metrics_.nonempty_flushes != 0
        || pending_send_metrics_.frames_at_flush != 0) {
        metrics_.MergeIoSendMetrics(pending_send_metrics_);
        pending_send_metrics_ = {};
    }
    while (!clients_.empty()) {
        CloseClient(clients_.begin()->first);
    }
    Logger::Info("[IO] TCP IO thread stopped id={}", io_thread_id_.value());
}

void IoThread::SetClientWriteInterest(ConnectionId connection_id, ClientConnection& client, bool enabled)
{
    if (client.wants_write == enabled || epoll_fd_ < 0) {
        return;
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    if (enabled) {
        event.events |= EPOLLOUT;
    }
    event.data.u64 = connection_id.value();
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client.fd, &event) < 0) {
        Logger::Warn("[IO] failed to update client epoll interest connection={} fd={} error={}",
                     connection_id.value(), client.fd, std::strerror(errno));
        return;
    }
    client.wants_write = enabled;
}

void IoThread::HandleSocketEvent(ConnectionId connection_id, std::uint32_t events)
{
    auto iterator = clients_.find(connection_id);
    if (iterator == clients_.end()) {
        return;
    }
    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U && (events & EPOLLIN) == 0U) {
        CloseClient(connection_id);
        return;
    }

    enum class ReadStatus {
        kOpen,
        kPeerClosed,
        kError,
    };

    auto read_status = ReadStatus::kOpen;
    std::vector<BinaryFrame> ready_frames;
    if ((events & EPOLLIN) != 0U) {
        auto& client = iterator->second;
        char buffer[1024];
        while (true) {
            const auto bytes_read = ::recv(client.fd, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                client.read_buffer.append(buffer, static_cast<std::size_t>(bytes_read));
                metrics_.OnBytesRead(static_cast<std::uint64_t>(bytes_read));
                continue;
            }
            if (bytes_read == 0) {
                read_status = ReadStatus::kPeerClosed;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (!IsPeerDisconnectError(errno)) {
                Logger::Warn("[IO] recv failed fd={} error={}", client.fd, std::strerror(errno));
            }
            read_status = IsPeerDisconnectError(errno) ? ReadStatus::kPeerClosed : ReadStatus::kError;
            break;
        }

        while (true) {
            BinaryFrame frame{};
            const auto decode_status = TryDecodeBinaryFrame(client.read_buffer, frame);
            if (decode_status == BinaryFrameDecodeStatus::kComplete) {
                ready_frames.push_back(std::move(frame));
                continue;
            }
            if (decode_status == BinaryFrameDecodeStatus::kInvalid) {
                Logger::Warn("[IO] invalid frame length fd={} buffered_bytes={}",
                             client.fd, client.read_buffer.size());
                read_status = ReadStatus::kError;
            } else if (read_status != ReadStatus::kOpen && !client.read_buffer.empty()) {
                Logger::Warn("[IO] incomplete terminal frame fd={} buffered_bytes={}",
                             client.fd, client.read_buffer.size());
                read_status = ReadStatus::kError;
            }
            break;
        }
    }

    for (const auto& frame : ready_frames) {
        auto frame_iterator = clients_.find(connection_id);
        if (frame_iterator == clients_.end()) {
            return;
        }
        if (frame_iterator->second.state == ClientConnection::State::kClosing) {
            break;
        }

        auto& client = frame_iterator->second;
        switch (static_cast<ClientMessageType>(frame.message_type)) {
        case ClientMessageType::kJoin: {
            const auto request = DecodeJoinRequest(frame);
            if (!request || request->value() == 0) {
                QueueErrorFrame(connection_id, client, "JOIN_USAGE");
                break;
            }
            if (client.state != ClientConnection::State::kAwaitingRequest) {
                QueueErrorFrame(connection_id, client, "ALREADY_JOINED");
                break;
            }

            const auto worker_id = WorkerId{(request->value() - 1) % worker_inboxes_.size()};
            client.worker_id = worker_id;
            client.state = ClientConnection::State::kPending;
            worker_inboxes_[static_cast<std::size_t>(worker_id.value())].Push(
                WorkerMessage{.connection = ConnectionHandle{io_thread_id_, connection_id}, .payload = *request});
            Logger::Info("[IO] join requested connection={} player={} worker={}",
                         connection_id.value(), request->value(), worker_id.value());
            break;
        }
        case ClientMessageType::kReconnect: {
            const auto request = DecodeReconnectRequest(frame);
            if (!request || request->value() == 0) {
                QueueErrorFrame(connection_id, client, "RECONNECT_USAGE");
                break;
            }
            if (client.state != ClientConnection::State::kAwaitingRequest) {
                QueueErrorFrame(connection_id, client, "ALREADY_JOINED");
                break;
            }

            const auto worker_id = GetSessionWorker(*request, worker_inboxes_.size());
            client.worker_id = worker_id;
            client.state = ClientConnection::State::kPending;
            worker_inboxes_[static_cast<std::size_t>(worker_id.value())].Push(
                WorkerMessage{.connection = ConnectionHandle{io_thread_id_, connection_id}, .payload = *request});
            Logger::Info("[IO] reconnect requested connection={} session={} worker={}",
                         connection_id.value(), request->value(), worker_id.value());
            break;
        }
        case ClientMessageType::kInput: {
            const auto request = DecodeInputRequest(frame);
            if (!request) {
                QueueErrorFrame(connection_id, client, "INPUT_USAGE");
                break;
            }
            if (client.state != ClientConnection::State::kActive) {
                QueueErrorFrame(connection_id, client, "NOT_JOINED");
                break;
            }

            worker_inboxes_[static_cast<std::size_t>(client.worker_id.value())].Push(
                WorkerMessage{.connection = ConnectionHandle{io_thread_id_, connection_id}, .payload = *request});
            break;
        }
        case ClientMessageType::kLeave:
            if (!IsValidLeaveRequest(frame)) {
                QueueErrorFrame(connection_id, client, "LEAVE_USAGE");
                break;
            }
            if (client.state != ClientConnection::State::kActive) {
                QueueErrorFrame(connection_id, client, "NOT_JOINED");
                break;
            }

            worker_inboxes_[static_cast<std::size_t>(client.worker_id.value())].Push(
                WorkerMessage{
                    .connection = ConnectionHandle{io_thread_id_, connection_id},
                    .payload = ConnectionEvent::kLeave,
                });
            client.state = ClientConnection::State::kClosing;
            break;
        default:
            QueueErrorFrame(connection_id, client, "UNKNOWN_COMMAND");
            break;
        }
    }

    iterator = clients_.find(connection_id);
    if (iterator == clients_.end()) {
        return;
    }
    if (read_status != ReadStatus::kOpen || (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
        CloseClient(connection_id);
        return;
    }
    if (iterator->second.state == ClientConnection::State::kClosing && iterator->second.outbound_queue.empty()) {
        CloseClient(connection_id);
        return;
    }
    if ((events & EPOLLOUT) != 0U) {
        if (!FlushClientOutbound(connection_id, iterator->second)) {
            CloseClient(connection_id);
            return;
        }
        if (iterator->second.state == ClientConnection::State::kClosing && iterator->second.outbound_queue.empty()) {
            CloseClient(connection_id);
        }
    }
}

bool IoThread::FlushClientOutbound(ConnectionId connection_id, ClientConnection& client)
{
    const auto queue_depth = client.outbound_queue.size();
    if (queue_depth > 0) {
        ++pending_send_metrics_.nonempty_flushes;
        pending_send_metrics_.frames_at_flush += queue_depth;
    }
    while (!client.outbound_queue.empty()) {
        auto& pending_write = client.outbound_queue.front();
        const auto& frame = pending_write.frame;
        ++pending_send_metrics_.send_calls;
        const auto bytes_sent = ::send(client.fd,
                                       frame.data() + pending_write.offset,
                                       frame.size() - pending_write.offset,
                                       MSG_NOSIGNAL);
        if (bytes_sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                SetClientWriteInterest(connection_id, client, true);
                return true;
            }
            if (!IsPeerDisconnectError(errno)) {
                Logger::Warn("[IO] send outbound failed fd={} error={}", client.fd, std::strerror(errno));
            }
            return false;
        }
        if (bytes_sent == 0) {
            return true;
        }
        pending_write.offset += static_cast<std::size_t>(bytes_sent);
        metrics_.OnBytesWritten(static_cast<std::uint64_t>(bytes_sent));
        if (pending_write.offset >= frame.size()) {
            client.outbound_queue.pop_front();
        }
    }
    SetClientWriteInterest(connection_id, client, false);
    return true;
}

void IoThread::CloseClient(ConnectionId connection_id)
{
    auto iterator = clients_.find(connection_id);
    if (iterator == clients_.end()) {
        return;
    }
    auto& client = iterator->second;
    NotifyDisconnect(connection_id, client);
    Logger::Info("[IO] client closed connection={} fd={}", connection_id.value(), client.fd);
    if (epoll_fd_ >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client.fd, nullptr);
    }
    ::close(client.fd);
    clients_.erase(iterator);
    metrics_.OnConnectionClosed();
}

void IoThread::NotifyDisconnect(ConnectionId connection_id, ClientConnection& client)
{
    if ((client.state != ClientConnection::State::kPending
         && client.state != ClientConnection::State::kActive)) {
        return;
    }
    worker_inboxes_[static_cast<std::size_t>(client.worker_id.value())].Push(
        WorkerMessage{
            .connection = ConnectionHandle{io_thread_id_, connection_id},
            .payload = ConnectionEvent::kDisconnected,
        });
}

bool IoThread::QueueEncodedFrame(ConnectionId connection_id,
                                 ClientConnection& client,
                                 std::string frame)
{
    if (client.outbound_queue.size() >= kOutboundQueueLimit) {
        return false;
    }
    client.outbound_queue.push_back(PendingWrite{.frame = std::move(frame), .offset = 0});
    if (!client.dirty) {
        client.dirty = true;
        dirty_clients_.push_back(connection_id);
    }
    return true;
}

void IoThread::QueueErrorFrame(ConnectionId connection_id, ClientConnection& client, const std::string& message)
{
    NotifyDisconnect(connection_id, client);
    client.state = ClientConnection::State::kClosing;
    if (!QueueEncodedFrame(connection_id,
                           client,
                           EncodeFrame(ServerMessageType::kError, message))) {
        CloseClient(connection_id);
    }
}

} // namespace rrs
