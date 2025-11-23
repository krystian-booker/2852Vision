#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <memory>
#include <string>
#include <mutex>

namespace vision {

class Database {
public:
    // Singleton access
    static Database& instance();

    // Initialize the database with the given path
    void initialize(const std::string& dbPath);

    // Get a reference to the database (throws if not initialized)
    SQLite::Database& get();

    // Execute a query with automatic mutex locking
    void execute(const std::string& sql);

    // Check if database is initialized (thread-safe)
    bool isInitialized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return db_ != nullptr;
    }

    // Execute with lock held - use for transactions requiring multiple operations
    template<typename Func>
    auto withLock(Func&& func) -> decltype(func(std::declval<SQLite::Database&>())) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) {
            throw std::runtime_error("Database not initialized");
        }
        return func(*db_);
    }

private:
    Database() = default;

    void createSchema();

    std::unique_ptr<SQLite::Database> db_;
    mutable std::mutex mutex_;
};

} // namespace vision
