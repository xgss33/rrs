#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace rrs {

class IOThread;

class Acceptor {
public:
    Acceptor(std::uint16_t port, const std::vector<std::unique_ptr<IOThread>>& io_threads);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;
    Acceptor(Acceptor&&) = delete;
    Acceptor& operator=(Acceptor&&) = delete;

    void Start();
    void Stop();

private:
    void Run(std::stop_token stop_token);
    [[nodiscard]] IOThread& SelectIoThread();
    [[nodiscard]] int CreateListenSocket() const;

    std::uint16_t port_;
    const std::vector<std::unique_ptr<IOThread>>& io_threads_;
    std::size_t next_io_thread_index_{0};
    int listen_fd_{-1};
    std::jthread thread_;
};

} // namespace rrs
