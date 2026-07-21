#include "rrs/observability/MetricsRegistry.h"

#include "rrs/core/Identifiers.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>

namespace rrs {

MetricsRegistry::MetricsRegistry(std::size_t worker_count)
    : worker_tick_cost_us_window_max_(worker_count)
    , worker_tick_window_mutexes_(worker_count)
    , worker_tick_cost_us_samples_(worker_count)
    , worker_static_entities_(worker_count)
    , worker_dynamic_entities_(worker_count)
    , worker_visibility_observers_(worker_count)
    , worker_visible_other_player_balls_(worker_count)
{
    for (auto& samples : worker_tick_cost_us_samples_) {
        samples.reserve(256);
    }
    for (auto& value : worker_static_entities_) {
        value.store(0, std::memory_order_relaxed);
    }
    for (auto& value : worker_dynamic_entities_) {
        value.store(0, std::memory_order_relaxed);
    }
    for (auto& value : worker_visibility_observers_) {
        value.store(0, std::memory_order_relaxed);
    }
    for (auto& value : worker_visible_other_player_balls_) {
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

void MetricsRegistry::RecordWorkerTickCostUs(WorkerId worker_id, std::uint64_t cost_us)
{
    const auto worker_index = static_cast<std::size_t>(worker_id.value());
    if (worker_index >= worker_tick_cost_us_window_max_.size()) {
        return;
    }

    auto lock = std::scoped_lock{worker_tick_window_mutexes_[worker_index]};
    worker_tick_cost_us_window_max_[worker_index] =
        std::max(worker_tick_cost_us_window_max_[worker_index], cost_us);
    worker_tick_cost_us_samples_[worker_index].push_back(cost_us);
}

void MetricsRegistry::SetWorkerRoomMetrics(WorkerId worker_id,
                                           std::uint64_t static_entities,
                                           std::uint64_t dynamic_entities,
                                           std::uint64_t visibility_observers,
                                           std::uint64_t visible_other_player_balls) noexcept
{
    const auto worker_index = static_cast<std::size_t>(worker_id.value());
    if (worker_index >= worker_static_entities_.size()) {
        return;
    }

    worker_static_entities_[worker_index].store(static_entities, std::memory_order_relaxed);
    worker_dynamic_entities_[worker_index].store(dynamic_entities, std::memory_order_relaxed);
    worker_visibility_observers_[worker_index].store(visibility_observers, std::memory_order_relaxed);
    worker_visible_other_player_balls_[worker_index].store(visible_other_player_balls, std::memory_order_relaxed);
}

MetricsSnapshot MetricsRegistry::CollectSnapshotAndResetTickWindows()
{
    auto snapshot = MetricsSnapshot{
        .net_connections_current = net_connections_current_.load(std::memory_order_relaxed),
        .net_bytes_in_total = net_bytes_in_total_.load(std::memory_order_relaxed),
        .net_bytes_out_total = net_bytes_out_total_.load(std::memory_order_relaxed),
        .static_entities_current = 0,
        .dynamic_entities_current = 0,
        .visibility_observers_current = 0,
        .visible_other_player_balls_current = 0,
        .io_send_metrics = {
            .send_calls = io_send_calls_.load(std::memory_order_relaxed),
            .nonempty_flushes = io_nonempty_flushes_.load(std::memory_order_relaxed),
            .frames_at_flush = io_frames_at_flush_.load(std::memory_order_relaxed),
        },
        .worker_tick_metrics = {},
    };

    snapshot.worker_tick_metrics.reserve(worker_tick_cost_us_window_max_.size());
    for (std::size_t index = 0; index < worker_tick_cost_us_window_max_.size(); ++index) {
        auto tick_cost_us_max = std::uint64_t{0};
        auto tick_cost_us_samples = std::vector<std::uint64_t>{};
        auto next_tick_cost_us_samples = std::vector<std::uint64_t>{};
        next_tick_cost_us_samples.reserve(256);
        {
            auto lock = std::scoped_lock{worker_tick_window_mutexes_[index]};
            tick_cost_us_max = std::exchange(worker_tick_cost_us_window_max_[index], 0);
            tick_cost_us_samples = std::move(worker_tick_cost_us_samples_[index]);
            worker_tick_cost_us_samples_[index] = std::move(next_tick_cost_us_samples);
        }

        snapshot.static_entities_current += worker_static_entities_[index].load(std::memory_order_relaxed);
        snapshot.dynamic_entities_current += worker_dynamic_entities_[index].load(std::memory_order_relaxed);
        snapshot.visibility_observers_current += worker_visibility_observers_[index].load(std::memory_order_relaxed);
        snapshot.visible_other_player_balls_current +=
            worker_visible_other_player_balls_[index].load(std::memory_order_relaxed);
        snapshot.worker_tick_metrics.push_back(WorkerTickMetrics{
            .worker_id = WorkerId{index},
            .tick_cost_us_max_5s = tick_cost_us_max,
            .tick_cost_us_samples_5s = std::move(tick_cost_us_samples),
        });
    }

    return snapshot;
}

} // namespace rrs
