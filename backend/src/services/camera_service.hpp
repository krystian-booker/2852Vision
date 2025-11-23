#pragma once

#include "models/camera.hpp"
#include <vector>
#include <optional>

namespace vision {

class CameraService {
public:
    // Singleton access
    static CameraService& instance();

    // CRUD operations
    std::vector<Camera> getAllCameras();
    std::optional<Camera> getCameraById(int id);
    std::optional<Camera> getCameraByIdentifier(const std::string& identifier);
    Camera createCamera(Camera& camera);
    bool updateCamera(const Camera& camera);
    bool updateCameraName(int id, const std::string& name);
    bool updateCameraSettings(int id, const std::string& name,
                               const std::string& resolutionJson, int framerate);
    bool deleteCamera(int id);

    // Camera controls
    bool updateCameraControls(int id, int orientation, ExposureMode exposureMode,
                              int exposureValue, GainMode gainMode, int gainValue);

    // Calibration
    bool saveCalibration(int id, const std::string& cameraMatrixJson,
                         const std::string& distCoeffsJson, double reprojectionError);

    // Discovery
    std::vector<DeviceInfo> discoverCameras(CameraType type);
    std::vector<CameraProfile> getCameraProfiles(const std::string& identifier, CameraType type);

private:
    CameraService() = default;
};

} // namespace vision
