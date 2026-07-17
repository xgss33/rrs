#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace rrs {

class MetricsRegistry;

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
    void Run(std::stop_token stop_token);

    MetricsRegistry& metrics_;
    std::chrono::seconds report_interval_;
    std::mutex mutex_;
    std::condition_variable_any wake_condition_;
    std::jthread thread_;
};

} // namespace rrs
