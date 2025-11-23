#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

// Forward declaration
namespace SQLite {
    class Statement;
}

namespace vision {

enum class CameraType {
    USB,
    Spinnaker,
    RealSense
};

NLOHMANN_JSON_SERIALIZE_ENUM(CameraType, {
    {CameraType::USB, "USB"},
    {CameraType::Spinnaker, "Spinnaker"},
    {CameraType::RealSense, "RealSense"}
})

enum class ExposureMode {
    Auto,
    Manual
};

NLOHMANN_JSON_SERIALIZE_ENUM(ExposureMode, {
    {ExposureMode::Auto, "auto"},
    {ExposureMode::Manual, "manual"}
})

enum class GainMode {
    Auto,
    Manual
};

NLOHMANN_JSON_SERIALIZE_ENUM(GainMode, {
    {GainMode::Auto, "auto"},
    {GainMode::Manual, "manual"}
})

struct Resolution {
    int width;
    int height;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Resolution, width, height)
};

struct Camera {
    int id = 0;
    std::string name;
    CameraType camera_type = CameraType::USB;
    std::string identifier;
    int orientation = 0;  // 0, 90, 180, 270
    int exposure_value = 500;
    int gain_value = 50;
    ExposureMode exposure_mode = ExposureMode::Auto;
    GainMode gain_mode = GainMode::Auto;
    std::optional<std::string> camera_matrix_json;
    std::optional<std::string> dist_coeffs_json;
    std::optional<double> reprojection_error;
    std::optional<std::string> device_info_json;
    std::optional<std::string> resolution_json;
    std::optional<int> framerate;
    bool depth_enabled = false;

    // JSON serialization
    nlohmann::json toJson() const;
    static Camera fromJson(const nlohmann::json& j);

    // Database operations
    static Camera fromRow(const class SQLite::Statement& query);
    void bindToStatement(class SQLite::Statement& stmt) const;
};

// Device info returned by discovery
struct DeviceInfo {
    std::string identifier;
    std::string name;
    CameraType camera_type;
    std::optional<std::string> serial_number;
    std::optional<std::string> manufacturer;
    std::optional<std::string> product;

    nlohmann::json toJson() const;
};

// Camera profile (resolution + framerate)
struct CameraProfile {
    int width;
    int height;
    int fps;

    nlohmann::json toJson() const;
};

} // namespace vision
