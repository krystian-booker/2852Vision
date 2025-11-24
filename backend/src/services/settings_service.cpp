#include "services/settings_service.hpp"
#include "core/database.hpp"
#include "core/config.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <filesystem>
#include <fstream>
#include <set>

namespace vision {

namespace {
    const std::set<std::string> SYSTEM_FIELDS = {
        "2022-rapidreact.json",
        "2023-chargedup.json",
        "2024-crescendo.json",
        "2025-reefscape-andymark.json",
        "2025-reefscape-welded.json"
    };
}

SettingsService& SettingsService::instance() {
    static SettingsService instance;
    return instance;
}

std::optional<std::string> SettingsService::get(const std::string& key) {
    auto& db = Database::instance();
    return db.withLock([&key](SQLite::Database& sqlDb) -> std::optional<std::string> {
        SQLite::Statement query(sqlDb, "SELECT value FROM settings WHERE key = ?");
        query.bind(1, key);

        if (query.executeStep()) {
            return query.getColumn(0).getString();
        }

        return std::nullopt;
    });
}

void SettingsService::set(const std::string& key, const std::string& value) {
    auto& db = Database::instance();
    db.withLock([&key, &value](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb,
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
        stmt.bind(1, key);
        stmt.bind(2, value);
        stmt.exec();
    });
}

bool SettingsService::remove(const std::string& key) {
    auto& db = Database::instance();
    return db.withLock([&key](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, "DELETE FROM settings WHERE key = ?");
        stmt.bind(1, key);
        return stmt.exec() > 0;
    });
}

std::string SettingsService::getString(const std::string& key, const std::string& defaultValue) {
    auto value = get(key);
    return value.value_or(defaultValue);
}

int SettingsService::getInt(const std::string& key, int defaultValue) {
    auto value = get(key);
    if (value) {
        try {
            return std::stoi(*value);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

double SettingsService::getDouble(const std::string& key, double defaultValue) {
    auto value = get(key);
    if (value) {
        try {
            return std::stod(*value);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool SettingsService::getBool(const std::string& key, bool defaultValue) {
    auto value = get(key);
    if (value) {
        return *value == "1" || *value == "true" || *value == "True";
    }
    return defaultValue;
}

GlobalSettings SettingsService::getGlobalSettings() {
    GlobalSettings gs;
    gs.team_number = getInt("team_number", 0);
    gs.ip_mode = getString("ip_mode", "dhcp");
    gs.hostname = getString("hostname", "vision");
    gs.static_ip = getString("static_ip", "");
    gs.gateway = getString("gateway", "");
    gs.subnet_mask = getString("subnet_mask", "");
    gs.network_interface = getString("network_interface", "");
    return gs;
}

void SettingsService::setGlobalSettings(const GlobalSettings& settings) {
    set("team_number", std::to_string(settings.team_number));
    set("ip_mode", settings.ip_mode);
    set("hostname", settings.hostname);
    set("static_ip", settings.static_ip);
    set("gateway", settings.gateway);
    set("subnet_mask", settings.subnet_mask);
    set("network_interface", settings.network_interface);
    spdlog::info("Updated global settings");
}

NetworkTableSettings SettingsService::getNetworkTableSettings() {
    NetworkTableSettings nts;
    nts.server_address = getString("nt_server_address", "");
    nts.port = getInt("nt_port", 5810);
    nts.table_name = getString("nt_table_name", "vision");
    return nts;
}

void SettingsService::setNetworkTableSettings(const NetworkTableSettings& settings) {
    set("nt_server_address", settings.server_address);
    set("nt_port", std::to_string(settings.port));
    set("nt_table_name", settings.table_name);
}

std::string SettingsService::getSelectedField() {
    return getString("selected_field", "");
}

void SettingsService::setSelectedField(const std::string& fieldName) {
    set("selected_field", fieldName);
    spdlog::info("Selected field layout: {}", fieldName);
}

std::string SettingsService::getFieldsDirectory() {
    auto& config = Config::instance();
    // Fields are stored directly in the data directory
    auto fieldsDir = std::filesystem::path(config.data_directory);
    std::filesystem::create_directories(fieldsDir);
    return fieldsDir.string();
}

std::vector<SettingsService::FieldLayout> SettingsService::getAvailableFields() {
    std::vector<FieldLayout> fields;
    std::string fieldsDir = getFieldsDirectory();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(fieldsDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().filename().string();
                // Check if it's a valid field layout (simple check: ends in .json)
                // In a real app, we might want to validate the JSON content too
                
                bool isSystem = SYSTEM_FIELDS.contains(filename);
                fields.push_back({filename, isSystem});
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Error reading fields directory: {}", e.what());
    }

    std::sort(fields.begin(), fields.end(), [](const FieldLayout& a, const FieldLayout& b) {
        return a.name < b.name;
    });
    return fields;
}

bool SettingsService::addCustomField(const std::string& name, const std::string& jsonContent) {
    try {
        // Validate JSON (returns false instead of throwing on invalid input)
        if (!nlohmann::json::accept(jsonContent)) {
            spdlog::error("Invalid JSON for field layout: {}", name);
            return false;
        }

        std::string fieldsDir = getFieldsDirectory();
        // Ensure filename ends with .json
        std::string filename = name;
        if (!filename.ends_with(".json")) {
            filename += ".json";
        }
        
        // Prevent overwriting system fields
        if (SYSTEM_FIELDS.contains(filename)) {
            spdlog::error("Cannot overwrite system field: {}", filename);
            return false;
        }

        std::filesystem::path filePath = std::filesystem::path(fieldsDir) / filename;

        std::ofstream file(filePath);
        if (!file) {
            spdlog::error("Failed to create field file: {}", filePath.string());
            return false;
        }

        file << jsonContent;
        spdlog::info("Added custom field layout: {}", name);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to add field layout: {}", e.what());
        return false;
    }
}

bool SettingsService::deleteField(const std::string& name) {
    // Prevent deleting system fields
    if (SYSTEM_FIELDS.contains(name)) {
        spdlog::error("Cannot delete system field: {}", name);
        return false;
    }

    std::string fieldsDir = getFieldsDirectory();
    std::filesystem::path filePath = std::filesystem::path(fieldsDir) / name;

    try {
        if (std::filesystem::remove(filePath)) {
            spdlog::info("Deleted field layout: {}", name);

            // Clear selected field if it was deleted
            if (getSelectedField() == name) {
                remove("selected_field");
            }
            return true;
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete field: {}", e.what());
    }

    return false;
}

void SettingsService::factoryReset() {
    auto& db = Database::instance();
    db.withLock([](SQLite::Database& sqlDb) {
        // Clear all tables
        sqlDb.exec("DELETE FROM settings");
        sqlDb.exec("DELETE FROM pipelines");
        sqlDb.exec("DELETE FROM cameras");
    });

    spdlog::info("Factory reset completed");
}

} // namespace vision
