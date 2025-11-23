#pragma once

#include "drivers/base_driver.hpp"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>
#include <librealsense2/rs.hpp>

namespace vision {

class RealSenseDriver : public BaseDriver {
public:
    explicit RealSenseDriver(const Camera& camera);
    ~RealSenseDriver() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    FrameResult getFrame() override;

    bool supportsDepth() const override { return true; }

    void setExposure(ExposureMode mode, int value) override;
    void setGain(GainMode mode, int value) override;
    int getExposure() const override;
    int getGain() const override;

    // Static discovery methods
    static std::vector<DeviceInfo> listDevices();
    static std::vector<CameraProfile> getSupportedProfiles(const std::string& identifier);

    // Check if RealSense support is available
    static bool isAvailable();

private:
    Camera camera_;
    bool connected_ = false;

    rs2::pipeline pipeline_;
    rs2::pipeline_profile profile_;
    rs2::align align_{RS2_STREAM_COLOR};
    rs2::config config_;

    // Cached sensor references for controls
    rs2::sensor colorSensor_;
    rs2::sensor depthSensor_;

    // Helper methods
    void configurePipeline();
    void findSensors();
};

} // namespace vision
