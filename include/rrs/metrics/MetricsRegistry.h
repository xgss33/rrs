#pragma once

#include "rrs/base/Types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rrs {

struct WorkerTickMetrics {
    WorkerId worker_id;
    std::uint64_t tick_cost_us_last{0};
    std::uint64_t tick_cost_us_max_5s{0};
};

struct IoSendMetrics {
    std::uint64_t send_calls{0};
    std::uint64_t nonempty_flushes{0};
    std::uint64_t frames_at_flush{0};
};

struct MetricsSnapshot {
    std::uint64_t net_connections_current{0};
    std::uint64_t net_bytes_in_total{0};
    std::uint64_t net_bytes_out_total{0};
    IoSendMetrics io_send_metrics;
    std::vector<WorkerTickMetrics> worker_tick_metrics;
};

class MetricsRegistry {
public:
    explicit MetricsRegistry(std::size_t worker_count);

    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;
    MetricsRegistry(MetricsRegistry&&) = delete;
    MetricsRegistry& operator=(MetricsRegistry&&) = delete;

    void OnConnectionOpened() noexcept;
    void OnConnectionClosed() noexcept;
    void OnBytesRead(std::uint64_t byte_count) noexcept;
    void OnBytesWritten(std::uint64_t byte_count) noexcept;
    void MergeIoSendMetrics(const IoSendMetrics& metrics) noexcept;
    void SetWorkerTickCostUs(WorkerId worker_id, std::uint64_t cost_us) noexcept;

    [[nodiscard]] MetricsSnapshot CollectSnapshotAndResetTickMaxima();

private:
    std::atomic<std::uint64_t> net_connections_current_{0};
    std::atomic<std::uint64_t> net_bytes_in_total_{0};
    std::atomic<std::uint64_t> net_bytes_out_total_{0};
    std::atomic<std::uint64_t> io_send_calls_{0};
    std::atomic<std::uint64_t> io_nonempty_flushes_{0};
    std::atomic<std::uint64_t> io_frames_at_flush_{0};
    std::vector<std::atomic<std::uint64_t>> worker_tick_cost_us_last_;
    std::vector<std::atomic<std::uint64_t>> worker_tick_cost_us_window_max_;
};

} // namespace rrs
