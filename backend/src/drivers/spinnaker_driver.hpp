#pragma once

#include "drivers/base_driver.hpp"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

#ifdef VISION_WITH_SPINNAKER
#include <Spinnaker.h>
#include <SpinGenApi/SpinnakerGenApi.h>
#endif

namespace vision {

// Node information for Spinnaker/Spinnaker node map
struct SpinnakerNode {
    std::string name;
    std::string display_name;
    std::string description;
    std::string interface_type;  // "integer", "float", "string", "boolean", "enumeration", "command"
    std::string access_mode;
    bool is_readable;
    bool is_writable;
    std::string value;
    std::vector<std::string> choices;  // For enumeration types

    // Numeric range info (for integer/float types)
    std::string min_value;
    std::string max_value;
    std::string increment;

    nlohmann::json toJson() const;
};

class SpinnakerDriver : public BaseDriver {
public:
    explicit SpinnakerDriver(const Camera& camera);
    ~SpinnakerDriver() override;

    // BaseDriver interface
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

    // Spinnaker system management
    static void initialize();
    static void shutdown();
    static bool isAvailable();

    // Node map operations (Spinnaker-compliant)
    static std::pair<std::vector<SpinnakerNode>, std::string> getNodeMap(const std::string& identifier);
    static std::tuple<bool, std::string, int, nlohmann::json> updateNode(
        const std::string& identifier,
        const std::string& nodeName,
        const std::string& value
    );

private:
    Camera camera_;
    bool connected_ = false;
    bool is_mono_camera_ = false;  // True if camera only supports mono formats

    // Spinnaker system singleton
    static std::mutex systemMutex_;
    static bool initialized_;

#ifdef VISION_WITH_SPINNAKER
    // Spinnaker SDK objects
    Spinnaker::CameraPtr cameraPtr_;

    // Helper methods
    void configureCamera();
    void configureStreamBuffers();
    cv::Mat convertFrame(const Spinnaker::ImagePtr& image);

    // Static system instance
    static Spinnaker::SystemPtr system_;

    // Node map helpers
    static std::string getAccessModeString(Spinnaker::GenApi::EAccessMode mode);
    static std::string getNodeValue(Spinnaker::GenApi::INode* node);
    static bool setNodeValue(Spinnaker::GenApi::INode* node, const std::string& value);
    static SpinnakerNode nodeToStruct(Spinnaker::GenApi::INode* node);
#endif
};

} // namespace vision
