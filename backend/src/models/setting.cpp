#include "models/setting.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

namespace vision {

nlohmann::json Setting::toJson() const {
    return {
        {"key", key},
        {"value", value}
    };
}

Setting Setting::fromRow(const SQLite::Statement& query) {
    Setting s;
    s.key = query.getColumn("key").getString();
    s.value = query.getColumn("value").getString();
    return s;
}

nlohmann::json Setting::getValueJson() const {
    if (value.empty()) {
        return nlohmann::json();
    }
    try {
        return nlohmann::json::parse(value);
    } catch (...) {
        // If not valid JSON, return as string
        return nlohmann::json(value);
    }
}

void Setting::setValueJson(const nlohmann::json& j) {
    value = j.dump();
}

int Setting::getIntValue() const {
    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

double Setting::getDoubleValue() const {
    try {
        return std::stod(value);
    } catch (...) {
        return 0.0;
    }
}

bool Setting::getBoolValue() const {
    return value == "1" || value == "true" || value == "True" || value == "TRUE";
}

// GlobalSettings
nlohmann::json GlobalSettings::toJson() const {
    nlohmann::json j;
    j["team_number"] = team_number;
    j["ip_mode"] = ip_mode;
    j["hostname"] = hostname;
    if (!static_ip.empty()) j["static_ip"] = static_ip;
    if (!gateway.empty()) j["gateway"] = gateway;
    if (!subnet_mask.empty()) j["subnet_mask"] = subnet_mask;
    if (!network_interface.empty()) j["network_interface"] = network_interface;
    return j;
}

GlobalSettings GlobalSettings::fromJson(const nlohmann::json& j) {
    GlobalSettings gs;
    gs.team_number = j.value("team_number", 0);
    gs.ip_mode = j.value("ip_mode", "dhcp");
    gs.hostname = j.value("hostname", "vision");
    gs.static_ip = j.value("static_ip", "");
    gs.gateway = j.value("gateway", "");
    gs.subnet_mask = j.value("subnet_mask", "");
    gs.network_interface = j.value("network_interface", "");
    return gs;
}

// NetworkTableSettings
nlohmann::json NetworkTableSettings::toJson() const {
    return {
        {"server_address", server_address},
        {"port", port},
        {"table_name", table_name}
    };
}

NetworkTableSettings NetworkTableSettings::fromJson(const nlohmann::json& j) {
    NetworkTableSettings nts;
    nts.server_address = j.value("server_address", "");
    nts.port = j.value("port", 5810);
    nts.table_name = j.value("table_name", "vision");
    return nts;
}

// AprilTagSettings
nlohmann::json AprilTagSettings::toJson() const {
    return {
        {"selected_field", selected_field},
        {"available_fields", available_fields}
    };
}

} // namespace vision
