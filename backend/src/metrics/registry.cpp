#include "metrics/registry.hpp"
#include "core/config.hpp"
#include <algorithm>
#include <numeric>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace vision {

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry instance;
    return instance;
}

void MetricsRegistry::recordFrame(int pipelineId, double processingTimeMs, double queueWaitMs) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& data = pipelineData_[pipelineId];
    auto now = std::chrono::steady_clock::now();

    // Record latency
    double totalLatency = processingTimeMs + queueWaitMs;
    data.latencies.push_back(totalLatency);
    if (data.latencies.size() > WINDOW_SIZE) {
        data.latencies.pop_front();
    }

    // Record frame time for FPS
    data.frameTimes.push_back(now);

    // Remove old frame times
    auto cutoff = now - std::chrono::seconds(FPS_WINDOW_SECONDS);
    while (!data.frameTimes.empty() && data.frameTimes.front() < cutoff) {
        data.frameTimes.pop_front();
    }

    // Update max latency
    if (totalLatency > data.maxLatency) {
        data.maxLatency = totalLatency;
    }

    data.totalFrames++;
}

void MetricsRegistry::recordDrop(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& data = pipelineData_[pipelineId];
    data.droppedFrames++;
    data.droppedFramesWindow++;
}

PipelineMetrics MetricsRegistry::getPipelineMetricsLocked(int pipelineId) {
    // NOTE: Caller must hold mutex_

    PipelineMetrics metrics;
    metrics.pipeline_id = pipelineId;

    auto it = pipelineData_.find(pipelineId);
    if (it == pipelineData_.end()) {
        return metrics;
    }

    auto& data = it->second;
    metrics.pipeline_name = data.name;
    metrics.frames_processed = data.totalFrames;
    metrics.dropped_frames_total = data.droppedFrames;
    metrics.dropped_frames_window = data.droppedFramesWindow;

    // Calculate FPS
    if (!data.frameTimes.empty()) {
        metrics.fps = static_cast<double>(data.frameTimes.size()) / FPS_WINDOW_SECONDS;
    }

    // Calculate latency stats
    if (!data.latencies.empty()) {
        std::vector<double> sorted(data.latencies.begin(), data.latencies.end());
        std::sort(sorted.begin(), sorted.end());

        // Average
        metrics.latency_avg_ms = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();

        // P95
        size_t p95Index = static_cast<size_t>(sorted.size() * 0.95);
        metrics.latency_p95_ms = sorted[(std::min)(p95Index, sorted.size() - 1)];

        // Max
        metrics.latency_max_ms = data.maxLatency;
    }

    // Reset window counters
    data.droppedFramesWindow = 0;
    data.maxLatency = 0.0;

    return metrics;
}

PipelineMetrics MetricsRegistry::getPipelineMetrics(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return getPipelineMetricsLocked(pipelineId);
}

std::vector<PipelineMetrics> MetricsRegistry::getAllPipelineMetrics() {
    std::vector<PipelineMetrics> result;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, _] : pipelineData_) {
        result.push_back(getPipelineMetricsLocked(id));
    }

    return result;
}

void MetricsRegistry::setPipelineInfo(int pipelineId, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipelineData_[pipelineId].name = name;
}

void MetricsRegistry::removePipeline(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipelineData_.erase(pipelineId);
}

SystemMetrics MetricsRegistry::getSystemMetrics() {
    updateSystemMetrics();
    return systemMetrics_;
}

void MetricsRegistry::updateSystemMetrics() {
    auto now = std::chrono::steady_clock::now();

    // Only update every 2 seconds
    if (now - lastSystemUpdate_ < std::chrono::seconds(2)) {
        return;
    }
    lastSystemUpdate_ = now;

#ifdef _WIN32
    // Get RAM usage
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        systemMetrics_.ram_total_mb = memStatus.ullTotalPhys / (1024 * 1024);
        systemMetrics_.ram_used_mb = (memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024 * 1024);
        systemMetrics_.ram_usage_percent = memStatus.dwMemoryLoad;
    }

    // Get CPU usage (simplified)
    static ULARGE_INTEGER lastIdleTime = {0}, lastKernelTime = {0}, lastUserTime = {0};
    FILETIME idleTime, kernelTime, userTime;

    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idleTime.dwLowDateTime;
        idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime;
        kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime;
        user.HighPart = userTime.dwHighDateTime;

        if (lastIdleTime.QuadPart != 0) {
            ULONGLONG idleDiff = idle.QuadPart - lastIdleTime.QuadPart;
            ULONGLONG kernelDiff = kernel.QuadPart - lastKernelTime.QuadPart;
            ULONGLONG userDiff = user.QuadPart - lastUserTime.QuadPart;
            ULONGLONG total = kernelDiff + userDiff;

            if (total > 0) {
                systemMetrics_.cpu_usage_percent = 100.0 * (1.0 - (double)idleDiff / total);
            }
        }

        lastIdleTime = idle;
        lastKernelTime = kernel;
        lastUserTime = user;
    }
#else
    // Linux implementation would go here
    systemMetrics_.cpu_usage_percent = 0.0;
    systemMetrics_.ram_usage_percent = 0.0;
#endif

    // Count active pipelines
    std::lock_guard<std::mutex> lock(mutex_);
    systemMetrics_.active_pipelines = static_cast<int>(pipelineData_.size());
}

MetricsSummary MetricsRegistry::getSummary() {
    MetricsSummary summary;
    summary.pipelines = getAllPipelineMetrics();
    summary.system = getSystemMetrics();

    auto& config = Config::instance();
    summary.thresholds.queue_warning = config.thresholds.pipeline_queue_warning;
    summary.thresholds.queue_critical = config.thresholds.pipeline_queue_critical;
    summary.thresholds.latency_warning_ms = config.thresholds.latency_warning_ms;
    summary.thresholds.latency_critical_ms = config.thresholds.latency_critical_ms;

    return summary;
}

// JSON serialization

nlohmann::json PipelineMetrics::toJson() const {
    return {
        {"pipeline_id", pipeline_id},
        {"pipeline_name", pipeline_name},
        {"fps", fps},
        {"frames_processed", frames_processed},
        {"latency_avg_ms", latency_avg_ms},
        {"latency_p95_ms", latency_p95_ms},
        {"latency_max_ms", latency_max_ms},
        {"queue_depth", queue_depth},
        {"queue_max_size", queue_max_size},
        {"queue_utilization", queue_utilization},
        {"dropped_frames_total", dropped_frames_total},
        {"dropped_frames_window", dropped_frames_window}
    };
}

nlohmann::json SystemMetrics::toJson() const {
    return {
        {"cpu_usage_percent", cpu_usage_percent},
        {"ram_usage_percent", ram_usage_percent},
        {"ram_used_mb", ram_used_mb},
        {"ram_total_mb", ram_total_mb},
        {"cpu_temperature", cpu_temperature},
        {"active_pipelines", active_pipelines}
    };
}

nlohmann::json MetricsThresholds::toJson() const {
    return {
        {"queue_warning", queue_warning},
        {"queue_critical", queue_critical},
        {"latency_warning_ms", latency_warning_ms},
        {"latency_critical_ms", latency_critical_ms}
    };
}

nlohmann::json MetricsSummary::toJson() const {
    nlohmann::json pipelinesJson = nlohmann::json::array();
    for (const auto& p : pipelines) {
        pipelinesJson.push_back(p.toJson());
    }

    return {
        {"pipelines", pipelinesJson},
        {"system", system.toJson()},
        {"thresholds", thresholds.toJson()}
    };
}

} // namespace vision
