#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace rrs {

class MetricsRegistry;
struct WorkerTickMetrics;

class MetricsReporter {
public:
    MetricsReporter(MetricsRegistry& metrics, std::chrono::seconds report_interval);
    ~MetricsReporter();

    MetricsReporter(const MetricsReporter&) = delete;
    MetricsReporter& operator=(const MetricsReporter&) = delete;
    MetricsReporter(MetricsReporter&&) = delete;
    MetricsReporter& operator=(MetricsReporter&&) = delete;

    void Start();
    void Stop();

private:
    struct ProcessCpuSample {
        std::chrono::steady_clock::time_point sampled_at;
        std::uint64_t cpu_time_ticks{0};
    };

    void Run(std::stop_token stop_token);
    [[nodiscard]] static std::optional<ProcessCpuSample> ReadProcessCpuSample();
    [[nodiscard]] static std::uint64_t ReadProcessRssBytes();
    [[nodiscard]] static double CalculateCpuPercent(const ProcessCpuSample& previous, const ProcessCpuSample& current);
    [[nodiscard]] static std::string FormatWorkerValues(
        const std::vector<WorkerTickMetrics>& metrics,
        bool format_window_max);

    MetricsRegistry& metrics_;
    std::chrono::seconds report_interval_;
    std::mutex mutex_;
    std::condition_variable_any wake_condition_;
    std::jthread thread_;
};

} // namespace rrs
