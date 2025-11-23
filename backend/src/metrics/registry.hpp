#pragma once

#include <nlohmann/json.hpp>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <atomic>

namespace vision {

// Pipeline performance metrics
struct PipelineMetrics {
    int pipeline_id = 0;
    std::string pipeline_name;

    // FPS calculation
    double fps = 0.0;
    int frames_processed = 0;

    // Latency (milliseconds)
    double latency_avg_ms = 0.0;
    double latency_p95_ms = 0.0;
    double latency_max_ms = 0.0;

    // Queue stats
    int queue_depth = 0;
    int queue_max_size = 2;
    double queue_utilization = 0.0;

    // Drops
    int dropped_frames_total = 0;
    int dropped_frames_window = 0;

    nlohmann::json toJson() const;
};

// System resource metrics
struct SystemMetrics {
    double cpu_usage_percent = 0.0;
    double ram_usage_percent = 0.0;
    int64_t ram_used_mb = 0;
    int64_t ram_total_mb = 0;
    double cpu_temperature = 0.0;
    int active_pipelines = 0;

    nlohmann::json toJson() const;
};

// Metrics thresholds
struct MetricsThresholds {
    int queue_warning = 1;
    int queue_critical = 2;
    int latency_warning_ms = 100;
    int latency_critical_ms = 150;

    nlohmann::json toJson() const;
};

// Metrics summary
struct MetricsSummary {
    std::vector<PipelineMetrics> pipelines;
    SystemMetrics system;
    MetricsThresholds thresholds;

    nlohmann::json toJson() const;
};

// Metrics registry singleton
class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    // Record a processed frame for a pipeline
    void recordFrame(int pipelineId, double processingTimeMs, double queueWaitMs);

    // Record a dropped frame
    void recordDrop(int pipelineId);

    // Get metrics for a specific pipeline
    PipelineMetrics getPipelineMetrics(int pipelineId);

    // Get all pipeline metrics
    std::vector<PipelineMetrics> getAllPipelineMetrics();

    // Get system metrics
    SystemMetrics getSystemMetrics();

    // Get metrics summary
    MetricsSummary getSummary();

    // Set pipeline info
    void setPipelineInfo(int pipelineId, const std::string& name);

    // Remove pipeline metrics
    void removePipeline(int pipelineId);

    // Update system metrics
    void updateSystemMetrics();

private:
    MetricsRegistry() = default;

    // Internal helper that assumes mutex is already held
    PipelineMetrics getPipelineMetricsLocked(int pipelineId);

    struct PipelineData {
        std::string name;
        std::deque<double> latencies;
        std::deque<std::chrono::steady_clock::time_point> frameTimes;
        int totalFrames = 0;
        int droppedFrames = 0;
        int droppedFramesWindow = 0;
        double maxLatency = 0.0;
    };

    std::unordered_map<int, PipelineData> pipelineData_;
    std::mutex mutex_;

    SystemMetrics systemMetrics_;
    std::chrono::steady_clock::time_point lastSystemUpdate_;

    // Configuration
    static constexpr int WINDOW_SIZE = 100;  // Number of samples to keep
    static constexpr int FPS_WINDOW_SECONDS = 10;
};

} // namespace vision
