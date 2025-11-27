#include "drivers/realsense_driver.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <set>

namespace vision {

RealSenseDriver::RealSenseDriver(const Camera& camera)
    : camera_(camera) {
}

RealSenseDriver::~RealSenseDriver() {
    disconnect();
}

bool RealSenseDriver::isAvailable() {
    return true;
}

bool RealSenseDriver::connect(bool silent) {
    if (connected_) {
        return true;
    }

    try {
        spdlog::info("Connecting to RealSense camera: {}", camera_.identifier);

        configurePipeline();

        // Start the pipeline
        profile_ = pipeline_.start(config_);

        // Find sensor references for exposure/gain control
        findSensors();

        connected_ = true;
        spdlog::info("RealSense camera connected successfully");
        return true;
    } catch (const rs2::error& e) {
        spdlog::error("RealSense error: {} ({})", e.what(), e.get_failed_function());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect RealSense camera: {}", e.what());
        return false;
    }
}

void RealSenseDriver::configurePipeline() {
    // If we have a specific serial number, use it
    if (!camera_.identifier.empty() && camera_.identifier != "auto") {
        config_.enable_device(camera_.identifier);
    }

    // Parse resolution from camera settings
    int width = 1920;
    int height = 1080;
    int fps = 30;

    if (camera_.resolution_json) {
        try {
            auto res = nlohmann::json::parse(*camera_.resolution_json);
            width = res["width"].get<int>();
            height = res["height"].get<int>();
        } catch (...) {
            spdlog::warn("Could not parse resolution, using defaults");
        }
    }

    if (camera_.framerate) {
        fps = *camera_.framerate;
    }

    // Enable color stream
    spdlog::info("Configuring RealSense: Color {}x{} @ {} fps", width, height, fps);
    config_.enable_stream(RS2_STREAM_COLOR, width, height, RS2_FORMAT_BGR8, fps);

    // Only enable depth stream if requested
    if (camera_.depth_enabled) {
        // Use smaller resolution for depth to improve performance
        int depthWidth = 1280;
        int depthHeight = 720;
        spdlog::info("Configuring RealSense: Depth {}x{} @ {} fps", depthWidth, depthHeight, fps);
        config_.enable_stream(RS2_STREAM_DEPTH, depthWidth, depthHeight, RS2_FORMAT_Z16, fps);
    }

    spdlog::debug("RealSense configured for {}x{} @ {} fps (depth: {})", width, height, fps, camera_.depth_enabled);
}

void RealSenseDriver::findSensors() {
    auto device = profile_.get_device();

    for (auto& sensor : device.query_sensors()) {
        if (sensor.supports(RS2_CAMERA_INFO_NAME)) {
            std::string name = sensor.get_info(RS2_CAMERA_INFO_NAME);
            if (name.find("RGB") != std::string::npos || name.find("Color") != std::string::npos) {
                colorSensor_ = sensor;
            } else if (name.find("Stereo") != std::string::npos || name.find("Depth") != std::string::npos) {
                depthSensor_ = sensor;
            }
        }
    }
}

void RealSenseDriver::disconnect() {
    if (connected_) {
        try {
            pipeline_.stop();
        } catch (...) {
            // Ignore errors during disconnect
        }
        connected_ = false;
        spdlog::info("RealSense camera disconnected");
    }
}

bool RealSenseDriver::isConnected() const {
    return connected_;
}

FrameResult RealSenseDriver::getFrame() {
    FrameResult result;

    if (!connected_) {
        return result;
    }

    try {
        // Wait for frames with timeout
        rs2::frameset frames = pipeline_.wait_for_frames(5000);

        // Align depth to color if depth is enabled
        if (camera_.depth_enabled) {
            frames = align_.process(frames);
        }

        // Get color frame
        rs2::video_frame colorFrame = frames.get_color_frame();
        if (colorFrame) {
            int w = colorFrame.get_width();
            int h = colorFrame.get_height();
            result.color = cv::Mat(cv::Size(w, h), CV_8UC3,
                                   (void*)colorFrame.get_data(), cv::Mat::AUTO_STEP).clone();
        }

        // Get depth frame only if enabled
        if (camera_.depth_enabled) {
            rs2::depth_frame depthFrame = frames.get_depth_frame();
            if (depthFrame) {
                int w = depthFrame.get_width();
                int h = depthFrame.get_height();
                result.depth = cv::Mat(cv::Size(w, h), CV_16UC1,
                                       (void*)depthFrame.get_data(), cv::Mat::AUTO_STEP).clone();
            }
        }
    } catch (const rs2::error& e) {
        spdlog::warn("RealSense frame error: {}", e.what());
    }

    return result;
}

void RealSenseDriver::setExposure(ExposureMode mode, int value) {
    if (!connected_ || !colorSensor_) return;

    try {
        if (mode == ExposureMode::Auto) {
            if (colorSensor_.supports(RS2_OPTION_ENABLE_AUTO_EXPOSURE)) {
                colorSensor_.set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, 1);
            }
        } else {
            if (colorSensor_.supports(RS2_OPTION_ENABLE_AUTO_EXPOSURE)) {
                colorSensor_.set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, 0);
            }
            if (colorSensor_.supports(RS2_OPTION_EXPOSURE)) {
                // RealSense exposure is in microseconds
                colorSensor_.set_option(RS2_OPTION_EXPOSURE, static_cast<float>(value));
            }
        }
    } catch (const rs2::error& e) {
        spdlog::warn("Failed to set RealSense exposure: {}", e.what());
    }
}

void RealSenseDriver::setGain(GainMode mode, int value) {
    if (!connected_ || !colorSensor_) return;

    try {
        if (colorSensor_.supports(RS2_OPTION_GAIN)) {
            colorSensor_.set_option(RS2_OPTION_GAIN, static_cast<float>(value));
        }
    } catch (const rs2::error& e) {
        spdlog::warn("Failed to set RealSense gain: {}", e.what());
    }
}

int RealSenseDriver::getExposure() const {
    if (!connected_ || !colorSensor_) return 0;

    try {
        if (colorSensor_.supports(RS2_OPTION_EXPOSURE)) {
            return static_cast<int>(colorSensor_.get_option(RS2_OPTION_EXPOSURE));
        }
    } catch (...) {}
    return 0;
}

int RealSenseDriver::getGain() const {
    if (!connected_ || !colorSensor_) return 0;

    try {
        if (colorSensor_.supports(RS2_OPTION_GAIN)) {
            return static_cast<int>(colorSensor_.get_option(RS2_OPTION_GAIN));
        }
    } catch (...) {}
    return 0;
}

std::vector<DeviceInfo> RealSenseDriver::listDevices() {
    std::vector<DeviceInfo> devices;

    try {
        rs2::context ctx;
        auto deviceList = ctx.query_devices();

        for (auto&& dev : deviceList) {
            DeviceInfo info;
            info.camera_type = CameraType::RealSense;

            // Get serial number as identifier
            if (dev.supports(RS2_CAMERA_INFO_SERIAL_NUMBER)) {
                info.identifier = dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
                info.serial_number = info.identifier;
            }

            // Get device name
            if (dev.supports(RS2_CAMERA_INFO_NAME)) {
                info.name = dev.get_info(RS2_CAMERA_INFO_NAME);
            } else {
                info.name = "Intel RealSense";
            }

            // Get product info
            if (dev.supports(RS2_CAMERA_INFO_PRODUCT_LINE)) {
                info.product = dev.get_info(RS2_CAMERA_INFO_PRODUCT_LINE);
            }

            info.manufacturer = "Intel";

            devices.push_back(info);
        }
    } catch (const rs2::error& e) {
        spdlog::warn("RealSense enumeration error: {}", e.what());
    }

    spdlog::info("Discovered {} RealSense cameras", devices.size());
    return devices;
}

std::vector<CameraProfile> RealSenseDriver::getSupportedProfiles(const std::string& identifier) {
    std::vector<CameraProfile> profiles;

    try {
        rs2::context ctx;
        auto deviceList = ctx.query_devices();

        for (auto&& dev : deviceList) {
            // Check if this is the device we want
            std::string serial;
            if (dev.supports(RS2_CAMERA_INFO_SERIAL_NUMBER)) {
                serial = dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
            }

            if (!identifier.empty() && serial != identifier) {
                continue;
            }

            // Query sensors for available profiles
            for (auto&& sensor : dev.query_sensors()) {
                for (auto&& profile : sensor.get_stream_profiles()) {
                    if (profile.stream_type() == RS2_STREAM_COLOR) {
                        auto vp = profile.as<rs2::video_stream_profile>();
                        CameraProfile cp;
                        cp.width = vp.width();
                        cp.height = vp.height();
                        cp.fps = vp.fps();

                        // Check if already in list
                        bool exists = false;
                        for (const auto& p : profiles) {
                            if (p.width == cp.width && p.height == cp.height && p.fps == cp.fps) {
                                exists = true;
                                break;
                            }
                        }

                        if (!exists) {
                            profiles.push_back(cp);
                        }
                    }
                }
            }
            break; // Found our device
        }
    } catch (const rs2::error& e) {
        spdlog::warn("RealSense profile query error: {}", e.what());
    }

    // Sort profiles by resolution (descending), then by framerate (descending)
    std::sort(profiles.begin(), profiles.end(), [](const CameraProfile& a, const CameraProfile& b) {
        if (a.width != b.width) return a.width > b.width;
        if (a.height != b.height) return a.height > b.height;
        return a.fps > b.fps;
    });

    return profiles;
}

} // namespace vision
