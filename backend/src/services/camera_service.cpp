#include "services/camera_service.hpp"
#include "core/database.hpp"
#include "drivers/usb_driver.hpp"
#include "drivers/realsense_driver.hpp"
#include "drivers/spinnaker_driver.hpp"
#include <spdlog/spdlog.h>

namespace vision {

CameraService& CameraService::instance() {
    static CameraService instance;
    return instance;
}

std::vector<Camera> CameraService::getAllCameras() {
    auto& db = Database::instance();
    return db.withLock([](SQLite::Database& sqlDb) {
        std::vector<Camera> cameras;
        SQLite::Statement query(sqlDb, "SELECT * FROM cameras ORDER BY id");

        while (query.executeStep()) {
            cameras.push_back(Camera::fromRow(query));
        }

        return cameras;
    });
}

std::optional<Camera> CameraService::getCameraById(int id) {
    auto& db = Database::instance();
    return db.withLock([id](SQLite::Database& sqlDb) -> std::optional<Camera> {
        SQLite::Statement query(sqlDb, "SELECT * FROM cameras WHERE id = ?");
        query.bind(1, id);

        if (query.executeStep()) {
            return Camera::fromRow(query);
        }

        return std::nullopt;
    });
}

std::optional<Camera> CameraService::getCameraByIdentifier(const std::string& identifier) {
    auto& db = Database::instance();
    return db.withLock([&identifier](SQLite::Database& sqlDb) -> std::optional<Camera> {
        SQLite::Statement query(sqlDb, "SELECT * FROM cameras WHERE identifier = ?");
        query.bind(1, identifier);

        if (query.executeStep()) {
            return Camera::fromRow(query);
        }

        return std::nullopt;
    });
}

Camera CameraService::createCamera(Camera& camera) {
    auto& db = Database::instance();
    return db.withLock([&camera](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            INSERT INTO cameras (
                name, camera_type, identifier, orientation,
                exposure_value, gain_value, exposure_mode, gain_mode,
                camera_matrix_json, dist_coeffs_json, reprojection_error,
                device_info_json, resolution_json, framerate, depth_enabled
            ) VALUES (
                :name, :camera_type, :identifier, :orientation,
                :exposure_value, :gain_value, :exposure_mode, :gain_mode,
                :camera_matrix_json, :dist_coeffs_json, :reprojection_error,
                :device_info_json, :resolution_json, :framerate, :depth_enabled
            )
        )");

        camera.bindToStatement(stmt);
        stmt.exec();

        camera.id = static_cast<int>(sqlDb.getLastInsertRowid());
        spdlog::info("Created camera '{}' with id {}", camera.name, camera.id);

        return camera;
    });
}

bool CameraService::updateCamera(const Camera& camera) {
    auto& db = Database::instance();
    return db.withLock([&camera](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            UPDATE cameras SET
                name = :name, camera_type = :camera_type, identifier = :identifier,
                orientation = :orientation, exposure_value = :exposure_value,
                gain_value = :gain_value, exposure_mode = :exposure_mode,
                gain_mode = :gain_mode, camera_matrix_json = :camera_matrix_json,
                dist_coeffs_json = :dist_coeffs_json, reprojection_error = :reprojection_error,
                device_info_json = :device_info_json, resolution_json = :resolution_json,
                framerate = :framerate, depth_enabled = :depth_enabled
            WHERE id = :id
        )");

        camera.bindToStatement(stmt);
        stmt.bind(":id", camera.id);

        return stmt.exec() > 0;
    });
}

bool CameraService::updateCameraName(int id, const std::string& name) {
    auto& db = Database::instance();
    return db.withLock([id, &name](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, "UPDATE cameras SET name = ? WHERE id = ?");
        stmt.bind(1, name);
        stmt.bind(2, id);

        return stmt.exec() > 0;
    });
}

bool CameraService::updateCameraSettings(int id, const std::string& name,
                                          const std::string& resolutionJson, int framerate) {
    auto& db = Database::instance();
    return db.withLock([id, &name, &resolutionJson, framerate](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            UPDATE cameras SET
                name = ?, resolution_json = ?, framerate = ?
            WHERE id = ?
        )");
        stmt.bind(1, name);
        stmt.bind(2, resolutionJson);
        stmt.bind(3, framerate);
        stmt.bind(4, id);

        bool success = stmt.exec() > 0;
        if (success) {
            spdlog::info("Updated camera {} settings (name: {}, resolution: {}, framerate: {})",
                         id, name, resolutionJson, framerate);
        }
        return success;
    });
}

bool CameraService::deleteCamera(int id) {
    auto& db = Database::instance();
    return db.withLock([id](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, "DELETE FROM cameras WHERE id = ?");
        stmt.bind(1, id);

        bool success = stmt.exec() > 0;
        if (success) {
            spdlog::info("Deleted camera with id {}", id);
        }
        return success;
    });
}

bool CameraService::updateCameraControls(int id, int orientation, ExposureMode exposureMode,
                                          int exposureValue, GainMode gainMode, int gainValue) {
    auto& db = Database::instance();
    return db.withLock([id, orientation, exposureMode, exposureValue, gainMode, gainValue](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            UPDATE cameras SET
                orientation = ?, exposure_mode = ?, exposure_value = ?,
                gain_mode = ?, gain_value = ?
            WHERE id = ?
        )");

        stmt.bind(1, orientation);
        stmt.bind(2, exposureMode == ExposureMode::Auto ? "auto" : "manual");
        stmt.bind(3, exposureValue);
        stmt.bind(4, gainMode == GainMode::Auto ? "auto" : "manual");
        stmt.bind(5, gainValue);
        stmt.bind(6, id);

        return stmt.exec() > 0;
    });
}

bool CameraService::saveCalibration(int id, const std::string& cameraMatrixJson,
                                     const std::string& distCoeffsJson, double reprojectionError) {
    auto& db = Database::instance();
    return db.withLock([id, &cameraMatrixJson, &distCoeffsJson, reprojectionError](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            UPDATE cameras SET
                camera_matrix_json = ?, dist_coeffs_json = ?, reprojection_error = ?
            WHERE id = ?
        )");

        stmt.bind(1, cameraMatrixJson);
        stmt.bind(2, distCoeffsJson);
        stmt.bind(3, reprojectionError);
        stmt.bind(4, id);

        bool success = stmt.exec() > 0;
        if (success) {
            spdlog::info("Saved calibration for camera {} (reprojection error: {})", id, reprojectionError);
        }
        return success;
    });
}

std::vector<DeviceInfo> CameraService::discoverCameras(CameraType type) {
    switch (type) {
        case CameraType::USB:
            return USBDriver::listDevices();

        case CameraType::Spinnaker:
            if (SpinnakerDriver::isAvailable()) {
                return SpinnakerDriver::listDevices();
            } else {
                spdlog::warn("Spinnaker support not compiled in");
                return {};
            }

        case CameraType::RealSense:
            if (RealSenseDriver::isAvailable()) {
                return RealSenseDriver::listDevices();
            } else {
                spdlog::warn("RealSense support not compiled in");
                return {};
            }

        default:
            return {};
    }
}

std::vector<CameraProfile> CameraService::getCameraProfiles(const std::string& identifier, CameraType type) {
    switch (type) {
        case CameraType::USB:
            return USBDriver::getSupportedProfiles(identifier);

        case CameraType::RealSense:
            if (RealSenseDriver::isAvailable()) {
                return RealSenseDriver::getSupportedProfiles(identifier);
            } else {
                spdlog::warn("RealSense support not compiled in");
                return {};
            }

        case CameraType::Spinnaker:
            if (SpinnakerDriver::isAvailable()) {
                return SpinnakerDriver::getSupportedProfiles(identifier);
            } else {
                spdlog::warn("Spinnaker support not compiled in");
                return {};
            }

        default:
            return {};
    }
}

} // namespace vision
