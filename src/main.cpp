#include "rrs/base/Types.h"
#include "rrs/config/ServerConfig.h"
#include "rrs/net/Acceptor.h"
#include "rrs/net/IOThread.h"
#include "rrs/metrics/MetricsRegistry.h"
#include "rrs/metrics/MetricsReporter.h"
#include "rrs/runtime/WorkerThread.h"
#include "rrs/runtime/SessionRegistry.h"
#include "rrs/log/Logger.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;
constexpr auto kMetricsReportInterval = std::chrono::seconds{5};

using WorkerThreads = std::vector<std::unique_ptr<rrs::WorkerThread>>;
using IOThreads = std::vector<std::unique_ptr<rrs::IOThread>>;

void HandleStopSignal(int)
{
    g_stop_requested = 1;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        std::signal(SIGINT, HandleStopSignal);
        std::signal(SIGTERM, HandleStopSignal);

        const auto config = rrs::ParseServerConfig(argc, argv);
        const auto io_thread_count = config.io_thread_count;
        const auto worker_thread_count = config.worker_thread_count;
        const auto tick_interval = std::chrono::nanoseconds{1'000'000'000LL / config.target_tick_hz};

        rrs::Logger::Initialize(config.app_name, config.log_level);
        rrs::Logger::Info("starting {} on port {}", config.app_name, config.listen_port);
        rrs::Logger::Info("io_threads={} worker_threads={} target_tick_hz={}",
                          io_thread_count,
                          worker_thread_count,
                          config.target_tick_hz);
        rrs::Logger::Info("server starting port={} room_capacity={} max_catch_up_ticks={} outbound_queue_limit={}",
                          config.listen_port,
                          config.room_capacity,
                          config.max_catch_up_ticks,
                          config.outbound_queue_limit);

        rrs::MetricsRegistry metrics{worker_thread_count};
        rrs::MetricsReporter metrics_reporter{metrics, kMetricsReportInterval};
        rrs::SessionRegistry session_registry;

        WorkerThreads workers;
        workers.reserve(worker_thread_count);
        for (std::uint32_t worker_index = 0; worker_index < worker_thread_count; ++worker_index) {
            workers.push_back(std::make_unique<rrs::WorkerThread>(
                std::format("worker-{}", worker_index),
                rrs::WorkerId{worker_index},
                tick_interval,
                config.max_catch_up_ticks,
                config.room_capacity,
                metrics));
        }

        std::vector<rrs::WorkerInboxSender> worker_inboxes;
        worker_inboxes.reserve(workers.size());
        for (const auto& worker : workers) {
            worker_inboxes.push_back(worker->inbox_sender());
        }

        IOThreads io_threads;
        io_threads.reserve(io_thread_count);
        for (std::uint32_t io_index = 0; io_index < io_thread_count; ++io_index) {
            io_threads.push_back(std::make_unique<rrs::IOThread>(
                rrs::IoThreadId{io_index},
                worker_inboxes,
                session_registry,
                metrics,
                config.outbound_queue_limit));
        }

        std::vector<rrs::IoInboxSender> io_inboxes;
        io_inboxes.reserve(io_threads.size());
        for (const auto& io_thread : io_threads) {
            io_inboxes.push_back(io_thread->inbox_sender());
        }

        for (auto& worker : workers) {
            worker->SetIoInboxes(io_inboxes);
            worker->Start();
        }

        for (auto& io_thread : io_threads) {
            io_thread->Start();
        }

        rrs::Acceptor acceptor{config.listen_port, io_threads};
        metrics_reporter.Start();
        acceptor.Start();

        rrs::Logger::Info("server started");
        while (g_stop_requested == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
        }

        rrs::Logger::Info("stop requested, shutting down");
        acceptor.Stop();
        metrics_reporter.Stop();
        for (auto& worker : workers) {
            worker->Stop();
        }
        for (auto& io_thread : io_threads) {
            io_thread->Stop();
        }
        rrs::Logger::Info("server stopped");

        return 0;
    } catch (const std::exception& ex) {
        rrs::Logger::Error("fatal startup error: {}", ex.what());
        return 1;
    }
}
