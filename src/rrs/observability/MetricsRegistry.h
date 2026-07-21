#pragma once

#include "rrs/core/Identifiers.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rrs {

struct RoomTickResponseTimeSamples {
    WorkerId worker_id;
    std::vector<std::uint64_t> samples_us;
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
    std::uint64_t static_entities_current{0};
    std::uint64_t dynamic_entities_current{0};
    std::uint64_t visibility_observers_current{0};
    std::uint64_t visible_other_player_balls_current{0};
    IoSendMetrics io_send_metrics;
    std::vector<RoomTickResponseTimeSamples> room_tick_response_times;
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
    void RecordRoomTickResponseTimeUs(WorkerId worker_id, std::uint64_t response_time_us);
    void SetWorkerRoomMetrics(WorkerId worker_id,
                              std::uint64_t static_entities,
                              std::uint64_t dynamic_entities,
                              std::uint64_t visibility_observers,
                              std::uint64_t visible_other_player_balls) noexcept;

    [[nodiscard]] MetricsSnapshot CollectSnapshotAndResetRoomTickResponseTimes();

private:
    std::atomic<std::uint64_t> net_connections_current_{0};
    std::atomic<std::uint64_t> net_bytes_in_total_{0};
    std::atomic<std::uint64_t> net_bytes_out_total_{0};
    std::atomic<std::uint64_t> io_send_calls_{0};
    std::atomic<std::uint64_t> io_nonempty_flushes_{0};
    std::atomic<std::uint64_t> io_frames_at_flush_{0};
    std::vector<std::mutex> room_tick_response_time_mutexes_;
    std::vector<std::vector<std::uint64_t>> room_tick_response_time_samples_;
    std::vector<std::atomic<std::uint64_t>> worker_static_entities_;
    std::vector<std::atomic<std::uint64_t>> worker_dynamic_entities_;
    std::vector<std::atomic<std::uint64_t>> worker_visibility_observers_;
    std::vector<std::atomic<std::uint64_t>> worker_visible_other_player_balls_;
};

} // namespace rrs
