#include "rrs/config/ServerConfig.h"
#include "rrs/core/Identifiers.h"
#include "rrs/core/Mailbox.h"
#include "rrs/core/ThreadMessages.h"
#include "rrs/network/Acceptor.h"
#include "rrs/network/IOThread.h"
#include "rrs/observability/Logger.h"
#include "rrs/observability/MetricsRegistry.h"
#include "rrs/observability/MetricsReporter.h"
#include "rrs/runtime/WorkerThread.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

using WorkerThreads = std::vector<std::unique_ptr<rrs::WorkerThread>>;
using WorkerInboxes = std::vector<rrs::MailboxSender<rrs::WorkerMessage>>;
using IoThreads = std::vector<std::unique_ptr<rrs::IoThread>>;
using IoInboxes = std::vector<rrs::MailboxSender<rrs::IoMessage>>;

void HandleStopSignal(int)
{
    g_stop_requested = 1;
}

} // namespace

int main(int argc, char* argv[]) try
{
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    const auto config = rrs::ParseServerConfig(argc, argv);
    const auto worker_thread_count = config.worker_thread_count;
    const auto io_thread_count = config.io_thread_count;

    rrs::Logger::Initialize(config.app_name, config.log_level, io_thread_count, worker_thread_count);
    rrs::Logger::Info("starting {} on port {}", config.app_name, config.listen_port);
    rrs::Logger::Info("io_threads={} worker_threads={}", io_thread_count, worker_thread_count);

    rrs::MetricsRegistry metrics{worker_thread_count};

    WorkerThreads worker_threads;
    WorkerInboxes worker_inboxes;
    worker_threads.reserve(worker_thread_count);
    worker_inboxes.reserve(worker_thread_count);
    for (std::uint32_t worker_index = 0; worker_index < worker_thread_count; ++worker_index) {
        worker_threads.push_back(std::make_unique<rrs::WorkerThread>(
            rrs::WorkerId{worker_index},
            worker_thread_count,
            metrics));
        worker_inboxes.push_back(worker_threads.back()->inbox_sender());
    }

    IoThreads io_threads;
    IoInboxes io_inboxes;
    io_threads.reserve(io_thread_count);
    io_inboxes.reserve(io_thread_count);
    for (std::uint32_t io_index = 0; io_index < io_thread_count; ++io_index) {
        io_threads.push_back(std::make_unique<rrs::IoThread>(
            rrs::IoThreadId{io_index},
            worker_inboxes,
            metrics));
        io_inboxes.push_back(io_threads.back()->inbox_sender());
    }

    for (auto& worker_thread : worker_threads) {
        worker_thread->SetIoInboxes(io_inboxes);
    }

    rrs::MetricsReporter metrics_reporter{metrics};
    rrs::Acceptor acceptor{config.listen_port, io_threads};

    for (auto& io_thread : io_threads) {
        io_thread->Start();
    }
    for (auto& worker_thread : worker_threads) {
        worker_thread->Start();
    }
    metrics_reporter.Start();
    acceptor.Start();

    
    rrs::Logger::Info("server started");
    while (g_stop_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }
    rrs::Logger::Info("stop requested, shutting down");
    acceptor.Stop();
    metrics_reporter.Stop();
    for (auto& worker_thread : worker_threads) {
        worker_thread->Stop();
    }
    for (auto& io_thread : io_threads) {
        io_thread->Stop();
    }
    rrs::Logger::Info("server stopped");
    return 0;
}
catch (const std::exception& ex)
{
    rrs::Logger::Error("fatal startup error: {}", ex.what());
    return 1;
}
