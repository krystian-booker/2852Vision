#include "models/camera.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

namespace vision {

nlohmann::json Camera::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["camera_type"] = camera_type;
    j["identifier"] = identifier;
    j["orientation"] = orientation;
    j["exposure_value"] = exposure_value;
    j["gain_value"] = gain_value;
    j["exposure_mode"] = exposure_mode;
    j["gain_mode"] = gain_mode;
    j["camera_matrix_json"] = camera_matrix_json.value_or("");
    j["dist_coeffs_json"] = dist_coeffs_json.value_or("");
    j["reprojection_error"] = reprojection_error.has_value() ? nlohmann::json(reprojection_error.value()) : nlohmann::json(nullptr);
    j["device_info_json"] = device_info_json.value_or("");
    j["resolution_json"] = resolution_json.value_or("");
    j["framerate"] = framerate.has_value() ? nlohmann::json(framerate.value()) : nlohmann::json(nullptr);
    j["depth_enabled"] = depth_enabled;
    return j;
}

Camera Camera::fromJson(const nlohmann::json& j) {
    Camera cam;
    cam.id = j.value("id", 0);
    cam.name = j.at("name").get<std::string>();
    cam.camera_type = j.at("camera_type").get<CameraType>();
    cam.identifier = j.at("identifier").get<std::string>();
    cam.orientation = j.value("orientation", 0);
    cam.exposure_value = j.value("exposure_value", 500);
    cam.gain_value = j.value("gain_value", 50);
    cam.exposure_mode = j.value("exposure_mode", ExposureMode::Auto);
    cam.gain_mode = j.value("gain_mode", GainMode::Auto);

    if (j.contains("camera_matrix_json") && !j["camera_matrix_json"].is_null() && !j["camera_matrix_json"].get<std::string>().empty()) {
        cam.camera_matrix_json = j["camera_matrix_json"].get<std::string>();
    }
    if (j.contains("dist_coeffs_json") && !j["dist_coeffs_json"].is_null() && !j["dist_coeffs_json"].get<std::string>().empty()) {
        cam.dist_coeffs_json = j["dist_coeffs_json"].get<std::string>();
    }
    if (j.contains("reprojection_error") && !j["reprojection_error"].is_null()) {
        cam.reprojection_error = j["reprojection_error"].get<double>();
    }
    if (j.contains("device_info_json") && !j["device_info_json"].is_null() && !j["device_info_json"].get<std::string>().empty()) {
        cam.device_info_json = j["device_info_json"].get<std::string>();
    }
    if (j.contains("resolution_json") && !j["resolution_json"].is_null() && !j["resolution_json"].get<std::string>().empty()) {
        cam.resolution_json = j["resolution_json"].get<std::string>();
    }
    if (j.contains("framerate") && !j["framerate"].is_null()) {
        cam.framerate = j["framerate"].get<int>();
    }
    cam.depth_enabled = j.value("depth_enabled", false);

    return cam;
}

Camera Camera::fromRow(const SQLite::Statement& query) {
    Camera cam;
    cam.id = query.getColumn("id").getInt();
    cam.name = query.getColumn("name").getString();

    std::string typeStr = query.getColumn("camera_type").getString();
    if (typeStr == "USB") cam.camera_type = CameraType::USB;
    else if (typeStr == "Spinnaker") cam.camera_type = CameraType::Spinnaker;
    else if (typeStr == "RealSense") cam.camera_type = CameraType::RealSense;

    cam.identifier = query.getColumn("identifier").getString();
    cam.orientation = query.getColumn("orientation").getInt();
    cam.exposure_value = query.getColumn("exposure_value").getInt();
    cam.gain_value = query.getColumn("gain_value").getInt();
    cam.exposure_mode = query.getColumn("exposure_mode").getString() == "auto" ? ExposureMode::Auto : ExposureMode::Manual;
    cam.gain_mode = query.getColumn("gain_mode").getString() == "auto" ? GainMode::Auto : GainMode::Manual;

    if (!query.getColumn("camera_matrix_json").isNull()) {
        cam.camera_matrix_json = query.getColumn("camera_matrix_json").getString();
    }
    if (!query.getColumn("dist_coeffs_json").isNull()) {
        cam.dist_coeffs_json = query.getColumn("dist_coeffs_json").getString();
    }
    if (!query.getColumn("reprojection_error").isNull()) {
        cam.reprojection_error = query.getColumn("reprojection_error").getDouble();
    }
    if (!query.getColumn("device_info_json").isNull()) {
        cam.device_info_json = query.getColumn("device_info_json").getString();
    }
    if (!query.getColumn("resolution_json").isNull()) {
        cam.resolution_json = query.getColumn("resolution_json").getString();
    }
    if (!query.getColumn("framerate").isNull()) {
        cam.framerate = query.getColumn("framerate").getInt();
    }
    cam.depth_enabled = query.getColumn("depth_enabled").getInt() != 0;

    return cam;
}

void Camera::bindToStatement(SQLite::Statement& stmt) const {
    stmt.bind(":name", name);

    std::string typeStr;
    switch (camera_type) {
        case CameraType::USB: typeStr = "USB"; break;
        case CameraType::Spinnaker: typeStr = "Spinnaker"; break;
        case CameraType::RealSense: typeStr = "RealSense"; break;
    }
    stmt.bind(":camera_type", typeStr);
    stmt.bind(":identifier", identifier);
    stmt.bind(":orientation", orientation);
    stmt.bind(":exposure_value", exposure_value);
    stmt.bind(":gain_value", gain_value);
    stmt.bind(":exposure_mode", exposure_mode == ExposureMode::Auto ? "auto" : "manual");
    stmt.bind(":gain_mode", gain_mode == GainMode::Auto ? "auto" : "manual");

    if (camera_matrix_json) stmt.bind(":camera_matrix_json", *camera_matrix_json);
    else stmt.bind(":camera_matrix_json");

    if (dist_coeffs_json) stmt.bind(":dist_coeffs_json", *dist_coeffs_json);
    else stmt.bind(":dist_coeffs_json");

    if (reprojection_error) stmt.bind(":reprojection_error", *reprojection_error);
    else stmt.bind(":reprojection_error");

    if (device_info_json) stmt.bind(":device_info_json", *device_info_json);
    else stmt.bind(":device_info_json");

    if (resolution_json) stmt.bind(":resolution_json", *resolution_json);
    else stmt.bind(":resolution_json");

    if (framerate) stmt.bind(":framerate", *framerate);
    else stmt.bind(":framerate");

    stmt.bind(":depth_enabled", depth_enabled ? 1 : 0);
}

nlohmann::json DeviceInfo::toJson() const {
    nlohmann::json j;
    j["identifier"] = identifier;
    j["name"] = name;
    j["camera_type"] = camera_type;
    if (serial_number) j["serial_number"] = *serial_number;
    if (manufacturer) j["manufacturer"] = *manufacturer;
    if (product) j["product"] = *product;
    return j;
}

nlohmann::json CameraProfile::toJson() const {
    nlohmann::json j;
    j["width"] = width;
    j["height"] = height;
    j["fps"] = fps;
    return j;
}

} // namespace vision
