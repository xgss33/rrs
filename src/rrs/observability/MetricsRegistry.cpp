#include "rrs/observability/MetricsRegistry.h"

#include "rrs/core/Identifiers.h"

#include <atomic>

namespace rrs {

MetricsRegistry::MetricsRegistry(std::size_t worker_count)
    : worker_tick_cost_us_window_max_(worker_count)
    , worker_static_entities_(worker_count)
    , worker_dynamic_entities_(worker_count)
{
    for (auto& value : worker_tick_cost_us_window_max_) {
        value.store(0, std::memory_order_relaxed);
    }
    for (auto& value : worker_static_entities_) {
        value.store(0, std::memory_order_relaxed);
    }
    for (auto& value : worker_dynamic_entities_) {
        value.store(0, std::memory_order_relaxed);
    }
}

void MetricsRegistry::OnConnectionOpened() noexcept
{
    net_connections_current_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::OnConnectionClosed() noexcept
{
    auto current = net_connections_current_.load(std::memory_order_relaxed);
    while (current > 0
           && !net_connections_current_.compare_exchange_weak(
               current,
               current - 1,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void MetricsRegistry::OnBytesRead(std::uint64_t byte_count) noexcept
{
    net_bytes_in_total_.fetch_add(byte_count, std::memory_order_relaxed);
}

void MetricsRegistry::OnBytesWritten(std::uint64_t byte_count) noexcept
{
    net_bytes_out_total_.fetch_add(byte_count, std::memory_order_relaxed);
}

void MetricsRegistry::MergeIoSendMetrics(const IoSendMetrics& metrics) noexcept
{
    io_send_calls_.fetch_add(metrics.send_calls, std::memory_order_relaxed);
    io_nonempty_flushes_.fetch_add(metrics.nonempty_flushes, std::memory_order_relaxed);
    io_frames_at_flush_.fetch_add(metrics.frames_at_flush, std::memory_order_relaxed);
}

void MetricsRegistry::RecordWorkerTickCostUs(WorkerId worker_id, std::uint64_t cost_us) noexcept
{
    const auto worker_index = static_cast<std::size_t>(worker_id.value());
    if (worker_index >= worker_tick_cost_us_window_max_.size()) {
        return;
    }

    auto max_cost = worker_tick_cost_us_window_max_[worker_index].load(std::memory_order_relaxed);
    while (cost_us > max_cost
           && !worker_tick_cost_us_window_max_[worker_index].compare_exchange_weak(
               max_cost,
               cost_us,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void MetricsRegistry::SetWorkerEntityCounts(WorkerId worker_id,
                                            std::uint64_t static_entities,
                                            std::uint64_t dynamic_entities) noexcept
{
    const auto worker_index = static_cast<std::size_t>(worker_id.value());
    if (worker_index >= worker_static_entities_.size()) {
        return;
    }

    worker_static_entities_[worker_index].store(static_entities, std::memory_order_relaxed);
    worker_dynamic_entities_[worker_index].store(dynamic_entities, std::memory_order_relaxed);
}

MetricsSnapshot MetricsRegistry::CollectSnapshotAndResetTickMaxima()
{
    auto snapshot = MetricsSnapshot{
        .net_connections_current = net_connections_current_.load(std::memory_order_relaxed),
        .net_bytes_in_total = net_bytes_in_total_.load(std::memory_order_relaxed),
        .net_bytes_out_total = net_bytes_out_total_.load(std::memory_order_relaxed),
        .static_entities_current = 0,
        .dynamic_entities_current = 0,
        .io_send_metrics = {
            .send_calls = io_send_calls_.load(std::memory_order_relaxed),
            .nonempty_flushes = io_nonempty_flushes_.load(std::memory_order_relaxed),
            .frames_at_flush = io_frames_at_flush_.load(std::memory_order_relaxed),
        },
        .worker_tick_metrics = {},
    };

    snapshot.worker_tick_metrics.reserve(worker_tick_cost_us_window_max_.size());
    for (std::size_t index = 0; index < worker_tick_cost_us_window_max_.size(); ++index) {
        snapshot.static_entities_current += worker_static_entities_[index].load(std::memory_order_relaxed);
        snapshot.dynamic_entities_current += worker_dynamic_entities_[index].load(std::memory_order_relaxed);
        snapshot.worker_tick_metrics.push_back(WorkerTickMetrics{
            .worker_id = WorkerId{index},
            .tick_cost_us_max_5s = worker_tick_cost_us_window_max_[index].exchange(0, std::memory_order_relaxed),
        });
    }

    return snapshot;
}

} // namespace rrs
