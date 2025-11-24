#include "core/config.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace vision {

Config Config::instance_;

Config& Config::instance() {
    return instance_;
}

namespace {
    std::string getEnv(const char* name, const std::string& defaultValue = "") {
        const char* value = std::getenv(name);
        return value ? std::string(value) : defaultValue;
    }

    int getEnvInt(const char* name, int defaultValue) {
        const char* value = std::getenv(name);
        if (value) {
            try {
                return std::stoi(value);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    bool getEnvBool(const char* name, bool defaultValue) {
        const char* value = std::getenv(name);
        if (value) {
            std::string str(value);
            return str == "1" || str == "true" || str == "True" || str == "TRUE";
        }
        return defaultValue;
    }

    std::string getDefaultDataDirectory() {
#ifdef _WIN32
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
            std::filesystem::path appDataPath(path);
            return (appDataPath / "2852Vision").string();
        }
#endif
        // Always use local data directory relative to executable
        return "./data";
    }
}

void Config::load() {
    // Environment
    environment = getEnv("FLASK_ENV", getEnv("VISION_ENV", "development"));

    // Data directory
    data_directory = getEnv("VISION_DATA_DIR", getDefaultDataDirectory());

    // Ensure data directory exists
    std::filesystem::create_directories(data_directory);

    // Database path
    database_path = getEnv("VISION_DATABASE_PATH",
        (std::filesystem::path(data_directory) / "vision.db").string());

    // Server configuration
    server.host = getEnv("VISION_HOST", isDevelopment() ? "0.0.0.0" : "0.0.0.0");
    server.port = static_cast<uint16_t>(getEnvInt("VISION_PORT", isDevelopment() ? 5001 : 8080));
    server.threads = getEnvInt("VISION_THREADS", 4);  // Multiple threads to prevent video stream blocking other endpoints

    // Metrics configuration
    metrics.enabled = getEnvBool("VISION_METRICS_ENABLED", true);
    metrics.window_seconds = getEnvInt("VISION_METRICS_WINDOW", 300);
    metrics.fps_window_seconds = getEnvInt("VISION_FPS_WINDOW", 10);
    metrics.memory_sample_interval_ms = getEnvInt("VISION_MEMORY_INTERVAL", 2000);

    // Thresholds
    thresholds.pipeline_queue_warning = getEnvInt("VISION_QUEUE_WARNING", 1);
    thresholds.pipeline_queue_critical = getEnvInt("VISION_QUEUE_CRITICAL", 2);
    thresholds.latency_warning_ms = getEnvInt("VISION_LATENCY_WARNING", 100);
    thresholds.latency_critical_ms = getEnvInt("VISION_LATENCY_CRITICAL", 150);

    spdlog::info("Configuration loaded:");
    spdlog::info("  Environment: {}", environment);
    spdlog::info("  Data directory: {}", data_directory);
    spdlog::info("  Database: {}", database_path);
    spdlog::info("  Server: {}:{}", server.host, server.port);
}

} // namespace vision