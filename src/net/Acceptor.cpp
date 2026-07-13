#include "rrs/net/Acceptor.h"

#include "rrs/base/Threading.h"
#include "rrs/net/IOThread.h"
#include "rrs/log/Logger.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace rrs {

Acceptor::Acceptor(std::uint16_t port, const std::vector<std::unique_ptr<IOThread>>& io_threads)
    : port_(port)
    , io_threads_(io_threads)
{
    if (io_threads_.empty()) {
        throw std::invalid_argument("Acceptor requires at least one IOThread");
    }
    for (const auto& io_thread : io_threads_) {
        if (io_thread == nullptr) {
            throw std::invalid_argument("Acceptor requires non-null IOThread targets");
        }
    }
}

Acceptor::~Acceptor()
{
    Stop();
}

void Acceptor::Start()
{
    listen_fd_ = CreateListenSocket();
    Logger::Info("[Acceptor] listening on 0.0.0.0:{}", port_);
    thread_ = std::jthread([this](std::stop_token stop_token) {
        Run(stop_token);
    });
}

void Acceptor::Stop()
{
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void Acceptor::Run(std::stop_token stop_token)
{
    SetCurrentThreadName("rrs-accept");

    while (!stop_token.stop_requested()) {
        sockaddr_in client_address{};
        socklen_t address_length = sizeof(client_address);
        const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_address), &address_length);
        if (client_fd < 0) {
            if (errno == EBADF || errno == EINVAL) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            Logger::Warn("[Acceptor] accept failed error={}", std::strerror(errno));
            continue;
        }

        auto& io_thread = SelectIoThread();
        io_thread.EnqueueAcceptedClient(client_fd);
        WakeIoThread(io_thread);
        Logger::Info("[Acceptor] accepted fd={} assigned_io={}", client_fd, io_thread.id().value());
    }

    Logger::Info("[Acceptor] stopped");
}

IOThread& Acceptor::SelectIoThread()
{
    auto& io_thread = *io_threads_[next_io_thread_index_];
    next_io_thread_index_ = (next_io_thread_index_ + 1) % io_threads_.size();
    return io_thread;
}

void Acceptor::WakeIoThread(const IOThread& io_thread) const
{
    const auto event_fd = io_thread.wake_event_fd();
    if (event_fd < 0) {
        Logger::Warn("[Acceptor] cannot wake IOThread id={} because eventfd is invalid", io_thread.id().value());
        return;
    }

    const std::uint64_t value = 1;
    const auto bytes_written = ::write(event_fd, &value, sizeof(value));
    if (bytes_written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        Logger::Warn("[Acceptor] eventfd write failed io={} error={}", io_thread.id().value(), std::strerror(errno));
    }
}

int Acceptor::CreateListenSocket() const
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("failed to create listen socket");
    }

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ::close(fd);
        throw std::runtime_error("failed to set SO_REUSEADDR");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(fd);
        throw std::runtime_error("failed to bind listen socket");
    }

    if (::listen(fd, SOMAXCONN) < 0) {
        ::close(fd);
        throw std::runtime_error("failed to listen");
    }

    return fd;
}

} // namespace rrs
