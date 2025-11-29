#include "core/config.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef _WIN32
#include <unistd.h>
#include <limits.h>
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

    std::string getExecutableDirectory() {
#ifdef _WIN32
        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, MAX_PATH) > 0) {
            std::filesystem::path exePath(path);
            return exePath.parent_path().string();
        }
#else
        char path[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
        if (count != -1) {
            std::filesystem::path exePath(std::string(path, count));
            return exePath.parent_path().string();
        }
#endif
        return ".";
    }

    std::string getAppDataDirectory() {
#ifdef _WIN32
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
            std::filesystem::path appDataPath(path);
            return (appDataPath / "2852Vision").string();
        }
#endif
        // On Linux or if SHGetFolderPathA fails, use ./data
        return "./data";
    }
}

void Config::load() {
    // Environment
    environment = getEnv("FLASK_ENV", getEnv("VISION_ENV", "development"));

    // Data directory - always relative to executable for static assets like field layouts
    std::string exeDir = getExecutableDirectory();
    std::string defaultDataDir = (std::filesystem::path(exeDir) / "data").string();
    data_directory = getEnv("VISION_DATA_DIR", defaultDataDir);

    // AppData directory for database
    std::string appDataDir = getAppDataDirectory();
    std::filesystem::create_directories(appDataDir);

    // Ensure data directory exists
    if (!std::filesystem::exists(data_directory)) {
        spdlog::warn("Data directory not found at: {}", std::filesystem::absolute(data_directory).string());
    }

    // Database path - defaults to AppData/vision.db
    database_path = getEnv("VISION_DATABASE_PATH",
        (std::filesystem::path(appDataDir) / "vision.db").string());

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