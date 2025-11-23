#include "core/database.hpp"
#include <spdlog/spdlog.h>

namespace vision {

Database& Database::instance() {
    static Database instance;
    return instance;
}

void Database::initialize(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (db_) {
        spdlog::warn("Database already initialized, reinitializing...");
    }

    spdlog::info("Initializing database at: {}", dbPath);

    db_ = std::make_unique<SQLite::Database>(
        dbPath,
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE
    );

    // Enable foreign keys
    db_->exec("PRAGMA foreign_keys = ON;");

    createSchema();

    spdlog::info("Database initialized successfully");
}

SQLite::Database& Database::get() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        throw std::runtime_error("Database not initialized");
    }
    return *db_;
}

void Database::execute(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        throw std::runtime_error("Database not initialized");
    }
    db_->exec(sql);
}

void Database::createSchema() {
    spdlog::debug("Creating database schema...");

    // Settings table
    db_->exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");

    // Cameras table
    db_->exec(R"(
        CREATE TABLE IF NOT EXISTS cameras (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            camera_type TEXT NOT NULL,
            identifier TEXT UNIQUE NOT NULL,
            orientation INTEGER DEFAULT 0,
            exposure_value INTEGER DEFAULT 500,
            gain_value INTEGER DEFAULT 50,
            exposure_mode TEXT DEFAULT 'auto',
            gain_mode TEXT DEFAULT 'auto',
            camera_matrix_json TEXT,
            dist_coeffs_json TEXT,
            reprojection_error REAL,
            device_info_json TEXT,
            resolution_json TEXT,
            framerate INTEGER,
            depth_enabled INTEGER DEFAULT 0
        );
    )");

    // Pipelines table
    db_->exec(R"(
        CREATE TABLE IF NOT EXISTS pipelines (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            pipeline_type TEXT NOT NULL DEFAULT 'AprilTag',
            config TEXT,
            camera_id INTEGER NOT NULL,
            FOREIGN KEY (camera_id) REFERENCES cameras(id) ON DELETE CASCADE
        );
    )");

    spdlog::debug("Database schema created");
}

} // namespace vision
