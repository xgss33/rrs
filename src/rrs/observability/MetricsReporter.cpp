#include "rrs/observability/MetricsReporter.h"

#include "rrs/core/Threading.h"
#include "rrs/observability/Logger.h"
#include "rrs/observability/MetricsRegistry.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace rrs {

namespace {

struct ProcessCpuSample {
    std::chrono::steady_clock::time_point sampled_at;
    std::uint64_t cpu_time_ticks{0};
};

std::optional<ProcessCpuSample> ReadProcessCpuSample()
{
    auto stat_file = std::ifstream{"/proc/self/stat"};
    auto line = std::string{};
    if (!std::getline(stat_file, line)) {
        return std::nullopt;
    }

    const auto command_end = line.rfind(')');
    if (command_end == std::string::npos || command_end + 2 >= line.size()) {
        return std::nullopt;
    }

    auto fields = std::vector<std::string>{};
    auto stream = std::istringstream{line.substr(command_end + 2)};
    auto field = std::string{};
    while (stream >> field) {
        fields.push_back(field);
    }

    constexpr std::size_t kUtimeIndex = 11;
    constexpr std::size_t kStimeIndex = 12;
    if (fields.size() <= kStimeIndex) {
        return std::nullopt;
    }

    try {
        const auto user_ticks = std::stoull(fields[kUtimeIndex]);
        const auto system_ticks = std::stoull(fields[kStimeIndex]);
        return ProcessCpuSample{
            .sampled_at = std::chrono::steady_clock::now(),
            .cpu_time_ticks = user_ticks + system_ticks,
        };
    } catch (...) {
        return std::nullopt;
    }
}

std::uint64_t ReadProcessRssBytes()
{
    auto statm_file = std::ifstream{"/proc/self/statm"};
    std::uint64_t ignored_total_pages = 0;
    std::uint64_t resident_pages = 0;
    statm_file >> ignored_total_pages >> resident_pages;

    const auto page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }

    return resident_pages * static_cast<std::uint64_t>(page_size);
}

double CalculateCpuPercent(const ProcessCpuSample& previous, const ProcessCpuSample& current)
{
    const auto ticks_per_second = ::sysconf(_SC_CLK_TCK);
    if (ticks_per_second <= 0 || current.cpu_time_ticks < previous.cpu_time_ticks) {
        return 0.0;
    }

    const auto elapsed_seconds = std::chrono::duration<double>(current.sampled_at - previous.sampled_at).count();
    if (elapsed_seconds <= 0.0) {
        return 0.0;
    }

    const auto process_seconds = static_cast<double>(current.cpu_time_ticks - previous.cpu_time_ticks)
        / static_cast<double>(ticks_per_second);
    return process_seconds / elapsed_seconds * 100.0;
}

std::string FormatWorkerTickCosts(const std::vector<WorkerTickMetrics>& metrics)
{
    auto output = std::string{};
    for (const auto& metric : metrics) {
        if (!output.empty()) {
            output.push_back(',');
        }
        output += std::to_string(metric.worker_id.value());
        output.push_back(':');
        output += std::to_string(metric.tick_cost_us_max_5s);
    }
    return output;
}

} // namespace

MetricsReporter::MetricsReporter(MetricsRegistry& metrics, std::chrono::seconds report_interval)
    : metrics_(metrics)
    , report_interval_(report_interval)
{
}

MetricsReporter::~MetricsReporter()
{
    Stop();
}

void MetricsReporter::Start()
{
    thread_ = std::jthread([this](std::stop_token stop_token) {
        Run(stop_token);
    });
}

void MetricsReporter::Stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        wake_condition_.notify_all();
        thread_.join();
    }
}

void MetricsReporter::Run(std::stop_token stop_token)
{
    SetCurrentThreadName("rrs-metrics");

    auto previous_snapshot = metrics_.CollectSnapshotAndResetTickMaxima();
    auto previous_cpu_sample = ReadProcessCpuSample();
    auto previous_sample_time = std::chrono::steady_clock::now();

    while (!stop_token.stop_requested()) {
        auto lock = std::unique_lock<std::mutex>{mutex_};
        wake_condition_.wait_for(lock, stop_token, report_interval_, [] {
            return false;
        });
        lock.unlock();

        if (stop_token.stop_requested()) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto snapshot = metrics_.CollectSnapshotAndResetTickMaxima();
        const auto elapsed = std::chrono::duration<double>(now - previous_sample_time).count();
        const auto bytes_in_per_sec = elapsed > 0.0
            ? static_cast<std::uint64_t>((snapshot.net_bytes_in_total - previous_snapshot.net_bytes_in_total) / elapsed)
            : 0;
        const auto bytes_out_per_sec = elapsed > 0.0
            ? static_cast<std::uint64_t>((snapshot.net_bytes_out_total - previous_snapshot.net_bytes_out_total) / elapsed)
            : 0;
        const auto send_calls = snapshot.io_send_metrics.send_calls - previous_snapshot.io_send_metrics.send_calls;
        const auto nonempty_flushes =
            snapshot.io_send_metrics.nonempty_flushes - previous_snapshot.io_send_metrics.nonempty_flushes;
        const auto frames_at_flush =
            snapshot.io_send_metrics.frames_at_flush - previous_snapshot.io_send_metrics.frames_at_flush;
        const auto send_calls_per_sec =
            elapsed > 0.0 ? static_cast<std::uint64_t>(send_calls / elapsed) : 0;
        const auto avg_frames_per_flush = nonempty_flushes > 0
            ? static_cast<double>(frames_at_flush) / static_cast<double>(nonempty_flushes)
            : 0.0;

        const auto current_cpu_sample = ReadProcessCpuSample();
        const auto cpu_percent = previous_cpu_sample && current_cpu_sample
            ? CalculateCpuPercent(*previous_cpu_sample, *current_cpu_sample)
            : 0.0;
        const auto rss_bytes = ReadProcessRssBytes();

        Logger::Metrics(
            "[Metrics] rrs_net_connections_current={} rrs_net_bytes_in_per_sec={} rrs_net_bytes_out_per_sec={} "
            "rrs_net_send_calls_per_sec={} rrs_net_avg_frames_per_flush={:.2f} "
            "rrs_process_cpu_percent={:.2f} rrs_process_memory_rss_bytes={} "
            "rrs_static_entities_current={} rrs_dynamic_entities_current={} "
            "rrs_worker_tick_cost_us_max_5s={}",
            snapshot.net_connections_current,
            bytes_in_per_sec,
            bytes_out_per_sec,
            send_calls_per_sec,
            avg_frames_per_flush,
            cpu_percent,
            rss_bytes,
            snapshot.static_entities_current,
            snapshot.dynamic_entities_current,
            FormatWorkerTickCosts(snapshot.worker_tick_metrics));

        previous_snapshot = snapshot;
        previous_cpu_sample = current_cpu_sample;
        previous_sample_time = now;
    }
}

} // namespace rrs
