#pragma once

#include <string>
#include <nlohmann/json.hpp>

// Forward declaration
namespace SQLite {
    class Statement;
}

namespace vision {

struct Setting {
    std::string key;
    std::string value;

    // JSON serialization
    nlohmann::json toJson() const;
    static Setting fromRow(const class SQLite::Statement& query);

    // Helper to parse JSON value
    nlohmann::json getValueJson() const;
    void setValueJson(const nlohmann::json& j);

    // Type-safe getters
    std::string getStringValue() const { return value; }
    int getIntValue() const;
    double getDoubleValue() const;
    bool getBoolValue() const;
};

// Global settings structure
struct GlobalSettings {
    int team_number = 0;
    std::string ip_mode = "dhcp";  // "dhcp" or "static"
    std::string hostname = "vision";
    std::string static_ip;
    std::string gateway;
    std::string subnet_mask;
    std::string network_interface;

    nlohmann::json toJson() const;
    static GlobalSettings fromJson(const nlohmann::json& j);
};

// Network table settings
struct NetworkTableSettings {
    std::string server_address;
    int port = 5810;
    std::string table_name = "vision";

    nlohmann::json toJson() const;
    static NetworkTableSettings fromJson(const nlohmann::json& j);
};

// AprilTag settings
struct AprilTagSettings {
    std::string selected_field;
    std::vector<std::string> available_fields;

    nlohmann::json toJson() const;
};

} // namespace vision
