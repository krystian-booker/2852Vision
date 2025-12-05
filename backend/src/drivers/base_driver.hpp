#pragma once

#include "models/camera.hpp"
#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <tuple>

namespace vision {

// Frame result can be just color, or color + depth
struct FrameResult {
    cv::Mat color;
    std::optional<cv::Mat> depth;

    bool empty() const { return color.empty(); }
};

class BaseDriver {
public:
    virtual ~BaseDriver() = default;

    // Connect to the camera
    virtual bool connect(bool silent = false) = 0;

    // Disconnect from the camera
    virtual void disconnect() = 0;

    // Check if connected
    virtual bool isConnected() const = 0;

    // Get a frame (color and optionally depth)
    virtual FrameResult getFrame() = 0;

    // Check if this driver supports depth
    virtual bool supportsDepth() const { return false; }

    // Set exposure mode and value
    virtual void setExposure(ExposureMode mode, int value) {}

    // Set gain mode and value
    virtual void setGain(GainMode mode, int value) {}

    // Get current exposure value
    virtual int getExposure() const { return 0; }

    // Range metadata
    struct Range {
        int min;
        int max;
        int step;
        int default_value;
    };

    // Get current gain value
    virtual int getGain() const { return 0; }
    
    // Get exposure range
    virtual Range getExposureRange() const { return {0, 10000, 1, 500}; }

    // Get gain range
    virtual Range getGainRange() const { return {0, 100, 1, 0}; }

    // Factory method to create appropriate driver
    static std::unique_ptr<BaseDriver> create(const Camera& camera);

    // Discovery methods (static, implemented by each driver type)
    // These are called through specific driver classes
};

// Interface for drivers that support device discovery
class DiscoverableDriver {
public:
    virtual ~DiscoverableDriver() = default;

    // List available devices of this type
    static std::vector<DeviceInfo> listDevices() { return {}; }

    // Get supported resolution/framerate profiles for a device
    static std::vector<CameraProfile> getSupportedProfiles(const std::string& identifier) { return {}; }
};

} // namespace vision
