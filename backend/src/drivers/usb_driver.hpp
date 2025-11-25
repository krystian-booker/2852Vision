#pragma once

#include "drivers/base_driver.hpp"
#include <openpnp-capture.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace vision {

class USBDriver : public BaseDriver {
public:
    explicit USBDriver(const Camera& camera);
    ~USBDriver() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    FrameResult getFrame() override;

    void setExposure(ExposureMode mode, int value) override;
    void setGain(GainMode mode, int value) override;
    int getExposure() const override;
    int getGain() const override;

    // Static discovery methods
    static std::vector<DeviceInfo> listDevices();
    static std::vector<CameraProfile> getSupportedProfiles(const std::string& identifier);

private:
    Camera camera_;

    // openpnp-capture handles
    CapContext ctx_ = nullptr;
    CapStream stream_ = -1;
    CapDeviceID deviceId_ = 0;
    CapFormatID formatId_ = 0;

    // Frame buffer
    std::vector<uint8_t> frameBuffer_;
    int frameWidth_ = 0;
    int frameHeight_ = 0;

    bool connected_ = false;

    // Helper methods
    CapDeviceID findDeviceId() const;
    CapFormatID findBestFormat() const;

    // Static helper to find device by identifier
    static CapDeviceID findDeviceByIdentifier(CapContext ctx, const std::string& identifier);
};

} // namespace vision
