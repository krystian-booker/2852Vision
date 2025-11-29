#pragma once

#include "drivers/base_driver.hpp"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace vision {

class USBDriver : public BaseDriver {
public:
    explicit USBDriver(const Camera& camera);
    ~USBDriver() override;

    bool connect(bool silent = false) override;
    void disconnect() override;
    bool isConnected() const override;
    FrameResult getFrame() override;

    void setExposure(ExposureMode mode, int value) override;
    void setGain(GainMode mode, int value) override;
    int getExposure() const override;
    int getGain() const override;

    // Extended controls
    void setFocus(bool autoFocus, int value);
    void setWhiteBalance(bool autoWB, int value);

    // Static discovery methods
    static std::vector<DeviceInfo> listDevices();
    static std::vector<CameraProfile> getSupportedProfiles(const std::string& identifier);

private:
    Camera camera_;
    cv::VideoCapture cap_;
    
    // Helper methods
    int findDeviceIndex(bool silent = false) const;
};

} // namespace vision
