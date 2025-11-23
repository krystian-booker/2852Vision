#pragma once

#include <string>
#include <cstdint>

namespace vision {

struct MetricsConfig {
    bool enabled = true;
    int window_seconds = 300;
    int fps_window_seconds = 10;
    int memory_sample_interval_ms = 2000;
};

struct ThresholdsConfig {
    int pipeline_queue_warning = 1;
    int pipeline_queue_critical = 2;
    int latency_warning_ms = 100;
    int latency_critical_ms = 150;
};

struct ServerConfig {
    std::string host = "0.0.0.0";
    uint16_t port = 8080;
    int threads = 4;
};

struct Config {
    std::string environment = "development";
    std::string database_path;
    std::string data_directory;

    ServerConfig server;
    MetricsConfig metrics;
    ThresholdsConfig thresholds;

    // Singleton access
    static Config& instance();

    // Load configuration from environment variables
    void load();

    // Check if running in development mode
    bool isDevelopment() const { return environment == "development"; }
    bool isProduction() const { return environment == "production"; }

private:
    Config() = default;
    static Config instance_;
};

} // namespace vision
