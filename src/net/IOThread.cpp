#include "rrs/net/IOThread.h"

#include "rrs/metrics/MetricsRegistry.h"
#include "rrs/net/BinaryProtocol.h"
#include "rrs/log/Logger.h"
#include "rrs/runtime/SessionRegistry.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace rrs {

IOThread::IOThread(IoThreadId io_thread_id,
                   std::vector<WorkerInboxSender> worker_inboxes,
                   SessionRegistry& session_registry,
                   MetricsRegistry& metrics,
                   std::size_t outbound_queue_limit)
    : io_thread_id_(io_thread_id)
    , worker_inboxes_(std::move(worker_inboxes))
    , outbound_queue_limit_(outbound_queue_limit)
    , session_registry_(session_registry)
    , metrics_(metrics)
{
}

IOThread::~IOThread()
{
    Stop();
}

void IOThread::Start()
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
    event.data.fd = wake_event_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_event_fd_, &event) < 0) {
        throw std::runtime_error("failed to register IO wake eventfd");
    }

    Logger::Info("[IO] starting TCP IO thread id={}", io_thread_id_.value());
    thread_ = std::jthread([this](std::stop_token stop_token) {
        Run(stop_token);
    });
}

void IOThread::Stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
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

void IOThread::EnqueueAcceptedClient(int client_fd)
{
    if (!accepted_clients_.Push(client_fd)) {
        Logger::Warn("[IO] failed to enqueue accepted fd={}", client_fd);
        ::close(client_fd);
    }
}

void IOThread::Run(std::stop_token stop_token)
{
    while (!stop_token.stop_requested()) {
        DrainAcceptedClients();
        DrainInbox();
        FlushDirtyClients();
        PollSocketEvents(stop_token);
    }

    while (!clients_.empty()) {
        CloseClient(clients_.begin()->first);
    }

    Logger::Info("[IO] TCP IO thread stopped sessions={}", sessions_.size());
}

void IOThread::SetClientWriteInterest(ClientConnection& client, bool enabled)
{
    if (client.wants_write == enabled || epoll_fd_ < 0) {
        return;
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    if (enabled) {
        event.events |= EPOLLOUT;
    }
    event.data.fd = client.fd;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client.fd, &event) < 0) {
        Logger::Warn("[IO] failed to update client epoll interest fd={} error={}", client.fd, std::strerror(errno));
        return;
    }

    client.wants_write = enabled;
}

void IOThread::DrainWakeEvent()
{
    if (wake_event_fd_ < 0) {
        return;
    }

    while (true) {
        std::uint64_t value = 0;
        const auto bytes_read = ::read(wake_event_fd_, &value, sizeof(value));
        if (bytes_read == sizeof(value)) {
            continue;
        }

        if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }

        if (bytes_read < 0 && errno == EINTR) {
            continue;
        }

        if (bytes_read < 0) {
            Logger::Warn("[IO] wake eventfd read failed id={} error={}", io_thread_id_.value(), std::strerror(errno));
        }
        return;
    }
}

void IOThread::PollSocketEvents(std::stop_token stop_token)
{
    constexpr int kMaxEvents = 64;
    constexpr int kEpollTimeoutMs = 1;
    std::array<epoll_event, kMaxEvents> events{};

    const auto event_count = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), kEpollTimeoutMs);
    if (event_count < 0) {
        if (errno != EINTR) {
            Logger::Warn("[IO] epoll_wait failed id={} error={}", io_thread_id_.value(), std::strerror(errno));
        }
        return;
    }

    for (int event_index = 0; event_index < event_count && !stop_token.stop_requested(); ++event_index) {
        const auto fd = events[static_cast<std::size_t>(event_index)].data.fd;
        const auto event_mask = events[static_cast<std::size_t>(event_index)].events;
        if (fd == wake_event_fd_) {
            DrainWakeEvent();
            continue;
        }

        HandleSocketEvent(fd, event_mask);
    }
}

void IOThread::DrainAcceptedClients()
{
    for (auto client_fd : accepted_clients_.Drain()) {
        const auto flags = ::fcntl(client_fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            Logger::Warn("[IO] failed to set nonblocking fd={}", client_fd);
            ::close(client_fd);
            continue;
        }

        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        event.data.fd = client_fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0) {
            Logger::Warn("[IO] failed to register client fd={} error={}", client_fd, std::strerror(errno));
            ::close(client_fd);
            continue;
        }

        clients_.emplace(client_fd, ClientConnection{
                                      .fd = client_fd,
                                      .read_buffer = {},
                                      .session = std::nullopt,
                                      .outbound_queue = {},
                                      .outbound_offset = 0,
                                      .wants_write = false,
                                  });
        metrics_.OnConnectionOpened();
        Logger::Info("[IO] client connected fd={}", client_fd);
    }
}

void IOThread::DrainInbox()
{
    for (auto& message : inbox_.Drain()) {
        const auto client_fd = sessions_.FindClientFd(message.session.session_id);
        if (!client_fd) {
            continue;
        }

        auto client_iterator = clients_.find(*client_fd);
        if (client_iterator == clients_.end()) {
            continue;
        }

        if (!IsCurrentClientSession(client_iterator->second, message.session)) {
            continue;
        }

        SendBinaryFrame(client_iterator->second, message.server_message_type, message.payload);
    }
}

void IOThread::FlushDirtyClients()
{
    while (!dirty_clients_.empty()) {
        const auto client_fd = *dirty_clients_.begin();
        dirty_clients_.erase(dirty_clients_.begin());

        auto iterator = clients_.find(client_fd);
        if (iterator == clients_.end()) {
            continue;
        }

        if (!FlushClientOutbound(iterator->second)) {
            CloseClient(client_fd);
        }
    }
}

void IOThread::HandleSocketEvent(int client_fd, std::uint32_t events)
{
    auto iterator = clients_.find(client_fd);
    if (iterator == clients_.end()) {
        return;
    }

    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U && (events & EPOLLIN) == 0U) {
        CloseClient(client_fd);
        return;
    }

    std::vector<BinaryFrame> ready_frames;
    if ((events & EPOLLIN) != 0U && !ReadClientFrames(iterator->second, ready_frames)) {
        CloseClient(client_fd);
        return;
    }

    for (const auto& frame : ready_frames) {
        auto frame_iterator = clients_.find(client_fd);
        if (frame_iterator == clients_.end()) {
            return;
        }
        HandleBinaryFrame(frame_iterator->second, frame);
    }

    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
        CloseClient(client_fd);
        return;
    }

    auto flush_iterator = clients_.find(client_fd);
    if (flush_iterator != clients_.end() && (events & EPOLLOUT) != 0U && !FlushClientOutbound(flush_iterator->second)) {
        CloseClient(client_fd);
    }
}

bool IOThread::ReadClientFrames(ClientConnection& client, std::vector<BinaryFrame>& ready_frames)
{
    char buffer[1024];

    while (true) {
        const auto bytes_read = ::recv(client.fd, buffer, sizeof(buffer), 0);
        if (bytes_read > 0) {
            client.read_buffer.append(buffer, static_cast<std::size_t>(bytes_read));
            metrics_.OnBytesRead(static_cast<std::uint64_t>(bytes_read));
            continue;
        }

        if (bytes_read == 0) {
            return false;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        Logger::Warn("[IO] recv failed fd={} error={}", client.fd, std::strerror(errno));
        return false;
    }

    while (auto frame = TryDecodeBinaryFrame(client.read_buffer)) {
        ready_frames.push_back(std::move(*frame));
    }

    return true;
}

bool IOThread::FlushClientOutbound(ClientConnection& client)
{
    while (!client.outbound_queue.empty()) {
        auto& frame = client.outbound_queue.front();
        const auto bytes_sent = ::send(
            client.fd,
            frame.data() + client.outbound_offset,
            frame.size() - client.outbound_offset,
            MSG_NOSIGNAL);
        if (bytes_sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                SetClientWriteInterest(client, true);
                return true;
            }

            Logger::Warn("[IO] send outbound failed fd={} error={}", client.fd, std::strerror(errno));
            return false;
        }

        if (bytes_sent == 0) {
            return true;
        }

        client.outbound_offset += static_cast<std::size_t>(bytes_sent);
        metrics_.OnBytesWritten(static_cast<std::uint64_t>(bytes_sent));
        if (client.outbound_offset >= frame.size()) {
            client.outbound_queue.pop_front();
            client.outbound_offset = 0;
        }
    }

    SetClientWriteInterest(client, false);
    return true;
}

void IOThread::CloseClient(int client_fd)
{
    auto iterator = clients_.find(client_fd);
    if (iterator == clients_.end()) {
        return;
    }

    Logger::Info("[IO] client closed fd={}", client_fd);
    if (iterator->second.session) {
        sessions_.Unbind(client_fd, iterator->second.session->session_id);
    }

    dirty_clients_.erase(client_fd);
    if (epoll_fd_ >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    }
    ::close(client_fd);
    clients_.erase(iterator);
    metrics_.OnConnectionClosed();
}

void IOThread::HandleBinaryFrame(ClientConnection& client, const BinaryFrame& frame)
{
    switch (static_cast<ClientMessageType>(frame.message_type)) {
    case ClientMessageType::kJoin: {
        const auto request = DecodeJoinRequest(frame);
        if (!request || !request->player_id.is_valid()) {
            SendError(client, "JOIN_USAGE");
            return;
        }
        HandleJoin(client, request->player_id);
        return;
    }
    case ClientMessageType::kReconnect: {
        const auto request = DecodeReconnectRequest(frame);
        if (!request || !request->session_id.is_valid()) {
            SendError(client, "RECONNECT_USAGE");
            return;
        }
        HandleReconnect(client, request->session_id);
        return;
    }
    case ClientMessageType::kInput: {
        const auto request = DecodeInputRequest(frame);
        if (!request) {
            SendError(client, "INPUT_USAGE");
            return;
        }
        HandleInput(client, PlayerInput{
                                .move_x = request->move_x,
                                .move_y = request->move_y,
                            });
        return;
    }
    case ClientMessageType::kLeave: {
        const auto request = DecodeLeaveRequest(frame);
        if (!request) {
            SendError(client, "LEAVE_USAGE");
            return;
        }
        HandleLeave(client);
        return;
    }
    }

    SendError(client, "UNKNOWN_COMMAND");
}

void IOThread::HandleJoin(ClientConnection& client, PlayerId player_id)
{
    if (client.session.has_value()) {
        SendError(client, "ALREADY_JOINED");
        return;
    }

    const auto worker_id = SelectWorkerForJoin(player_id);
    const auto session = session_registry_.Create(player_id, io_thread_id_, worker_id);
    BindClientSession(client, session);

    if (!PushToWorker(worker_id, IoToWorkerMessage::MakeJoin(session))) {
        session_registry_.Remove(session.session_id);
        UnbindClientSession(client);
        SendError(client, "WORKER_QUEUE_UNAVAILABLE");
        return;
    }

    Logger::Info("[IO] join requested fd={} session={} player={} worker={} generation={}",
                 client.fd,
                 session.session_id.value(),
                 session.player_id.value(),
                 session.worker_id.value(),
                 session.generation);
}

void IOThread::HandleReconnect(ClientConnection& client, SessionId session_id)
{
    if (client.session.has_value()) {
        SendError(client, "ALREADY_JOINED");
        return;
    }

    const auto session = session_registry_.Reconnect(session_id, io_thread_id_);
    if (!session) {
        SendError(client, "SESSION_NOT_FOUND");
        return;
    }

    BindClientSession(client, *session);
    if (!PushToWorker(session->worker_id, IoToWorkerMessage::MakeReconnect(*session))) {
        UnbindClientSession(client);
        SendError(client, "WORKER_QUEUE_UNAVAILABLE");
        return;
    }

    Logger::Info("[IO] reconnect requested fd={} session={} player={} worker={} generation={}",
                 client.fd,
                 session->session_id.value(),
                 session->player_id.value(),
                 session->worker_id.value(),
                 session->generation);
}

void IOThread::HandleInput(ClientConnection& client, PlayerInput input)
{
    if (!client.session) {
        SendError(client, "NOT_JOINED");
        return;
    }

    if (!PushToWorker(client.session->worker_id, IoToWorkerMessage::MakePlayerInput(*client.session, input))) {
        SendError(client, "WORKER_QUEUE_UNAVAILABLE");
    }
}

void IOThread::HandleLeave(ClientConnection& client)
{
    if (!client.session) {
        return;
    }

    const auto session = *client.session;
    session_registry_.Remove(session.session_id);
    UnbindClientSession(client);
    if (!PushToWorker(session.worker_id, IoToWorkerMessage::MakeLeave(session))) {
        Logger::Warn("[IO] failed to push leave session={} worker={}", session.session_id.value(), session.worker_id.value());
    }
    Logger::Info("[IO] leave requested fd={} session={} player={}",
                 client.fd,
                 session.session_id.value(),
                 session.player_id.value());
}

void IOThread::BindClientSession(ClientConnection& client, const Session& session)
{
    client.session = session;
    sessions_.Bind(client.fd, session.session_id);
}

void IOThread::UnbindClientSession(ClientConnection& client)
{
    if (!client.session) {
        return;
    }

    sessions_.Unbind(client.fd, client.session->session_id);
    client.session = std::nullopt;
}

void IOThread::SendBinaryFrame(ClientConnection& client, ServerMessageType message_type, const std::string& payload)
{
    if (client.outbound_queue.size() >= outbound_queue_limit_) {
        client.outbound_queue.pop_front();
        client.outbound_offset = 0;
    }

    client.outbound_queue.push_back(EncodeFrame(message_type, payload));
    dirty_clients_.insert(client.fd);
}

void IOThread::SendError(ClientConnection& client, const std::string& message)
{
    SendBinaryFrame(client, ServerMessageType::kError, EncodeErrorPayload(message));
}

bool IOThread::IsCurrentClientSession(const ClientConnection& client, const Session& session) const
{
    return client.session
        && client.session->session_id == session.session_id
        && client.session->generation == session.generation
        && client.session->io_thread_id == io_thread_id_;
}

WorkerId IOThread::SelectWorkerForJoin(PlayerId player_id) const
{
    const auto worker_count = worker_inboxes_.size();
    return WorkerId{(player_id.value() - 1) % worker_count};
}

bool IOThread::PushToWorker(WorkerId worker_id, IoToWorkerMessage message)
{
    const auto worker_index = static_cast<std::size_t>(worker_id.value());
    if (worker_index >= worker_inboxes_.size() || !worker_inboxes_[worker_index].IsValid()) {
        Logger::Warn("[IO] worker inbox unavailable worker={}", worker_id.value());
        return false;
    }

    return worker_inboxes_[worker_index].Push(std::move(message));
}

} // namespace rrs
