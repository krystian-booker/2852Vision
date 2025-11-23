#pragma once

#include "models/setting.hpp"
#include <string>
#include <optional>
#include <vector>

namespace vision {

class SettingsService {
public:
    // Singleton access
    static SettingsService& instance();

    // Get/set individual settings
    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value);
    bool remove(const std::string& key);

    // Type-safe getters with defaults
    std::string getString(const std::string& key, const std::string& defaultValue = "");
    int getInt(const std::string& key, int defaultValue = 0);
    double getDouble(const std::string& key, double defaultValue = 0.0);
    bool getBool(const std::string& key, bool defaultValue = false);

    // Global settings
    GlobalSettings getGlobalSettings();
    void setGlobalSettings(const GlobalSettings& settings);

    // Network table settings
    NetworkTableSettings getNetworkTableSettings();
    void setNetworkTableSettings(const NetworkTableSettings& settings);

    struct FieldLayout {
        std::string name;
        bool is_system;
    };

    // AprilTag field management
    std::string getSelectedField();
    void setSelectedField(const std::string& fieldName);
    std::vector<FieldLayout> getAvailableFields();
    bool addCustomField(const std::string& name, const std::string& jsonContent);
    bool deleteField(const std::string& name);

    // Factory reset
    void factoryReset();

private:
    SettingsService() = default;

    // Field layout storage directory
    std::string getFieldsDirectory();
};

} // namespace vision
