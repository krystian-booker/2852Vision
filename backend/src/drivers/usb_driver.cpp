#include "drivers/usb_driver.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <climits>

namespace vision {

USBDriver::USBDriver(const Camera& camera)
    : camera_(camera) {
}

USBDriver::~USBDriver() {
    disconnect();
}

bool USBDriver::connect() {
    if (connected_) {
        return true;
    }

    // Create capture context
    ctx_ = Cap_createContext();
    if (!ctx_) {
        spdlog::error("Failed to create openpnp-capture context");
        return false;
    }

    // Find the device matching our identifier
    deviceId_ = findDeviceId();

    uint32_t deviceCount = Cap_getDeviceCount(ctx_);
    if (deviceCount == 0) {
        spdlog::error("No USB cameras found");
        Cap_releaseContext(ctx_);
        ctx_ = nullptr;
        return false;
    }

    if (deviceId_ >= deviceCount) {
        spdlog::error("Device ID {} out of range (found {} devices)", deviceId_, deviceCount);
        Cap_releaseContext(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // Find the best format matching our requirements
    formatId_ = findBestFormat();

    // Get format info for logging and buffer allocation
    CapFormatInfo formatInfo;
    if (Cap_getFormatInfo(ctx_, deviceId_, formatId_, &formatInfo) != CAPRESULT_OK) {
        spdlog::error("Failed to get format info for device {}", camera_.identifier);
        Cap_releaseContext(ctx_);
        ctx_ = nullptr;
        return false;
    }

    frameWidth_ = static_cast<int>(formatInfo.width);
    frameHeight_ = static_cast<int>(formatInfo.height);

    spdlog::info("Opening USB camera '{}' with format {}x{} @ {} fps",
                 camera_.name, frameWidth_, frameHeight_, formatInfo.fps);

    // Open the stream
    stream_ = Cap_openStream(ctx_, deviceId_, formatId_);
    if (stream_ < 0) {
        spdlog::error("Failed to open stream for USB camera '{}'", camera_.identifier);
        Cap_releaseContext(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // Allocate frame buffer (RGB24 format)
    frameBuffer_.resize(static_cast<size_t>(frameWidth_) * frameHeight_ * 3);

    // Apply camera settings
    setExposure(camera_.exposure_mode, camera_.exposure_value);
    setGain(camera_.gain_mode, camera_.gain_value);

    connected_ = true;
    spdlog::info("USB camera '{}' connected successfully ({}x{} @ {} fps)",
                 camera_.name, frameWidth_, frameHeight_, formatInfo.fps);

    return true;
}

void USBDriver::disconnect() {
    if (stream_ >= 0 && ctx_) {
        Cap_closeStream(ctx_, stream_);
        stream_ = -1;
    }

    if (ctx_) {
        Cap_releaseContext(ctx_);
        ctx_ = nullptr;
    }

    frameBuffer_.clear();
    frameWidth_ = 0;
    frameHeight_ = 0;
    connected_ = false;
}

bool USBDriver::isConnected() const {
    return connected_ && ctx_ != nullptr && stream_ >= 0;
}

FrameResult USBDriver::getFrame() {
    FrameResult result;

    if (!isConnected()) {
        spdlog::debug("getFrame called but camera not connected");
        return result;
    }

    // Check if a new frame is available
    if (!Cap_hasNewFrame(ctx_, stream_)) {
        return result;  // No new frame available yet
    }

    // Capture the frame into our RGB buffer
    CapResult capResult = Cap_captureFrame(ctx_, stream_,
                                            frameBuffer_.data(),
                                            static_cast<uint32_t>(frameBuffer_.size()));

    if (capResult != CAPRESULT_OK) {
        spdlog::warn("Failed to capture frame from USB camera '{}' (error: {})",
                     camera_.name, capResult);
        return result;
    }

    // Create cv::Mat from RGB buffer and convert to BGR for OpenCV
    cv::Mat rgbFrame(frameHeight_, frameWidth_, CV_8UC3, frameBuffer_.data());

    // OpenCV uses BGR format, openpnp-capture returns RGB
    cv::cvtColor(rgbFrame, result.color, cv::COLOR_RGB2BGR);

    return result;
}

void USBDriver::setExposure(ExposureMode mode, int value) {
    if (!isConnected()) return;

    if (mode == ExposureMode::Auto) {
        CapResult res = Cap_setAutoProperty(ctx_, stream_, CAPPROPID_EXPOSURE, 1);
        if (res != CAPRESULT_OK && res != CAPRESULT_PROPERTYNOTSUPPORTED) {
            spdlog::debug("Failed to set auto exposure on camera '{}'", camera_.name);
        }
    } else {
        // Disable auto exposure first
        Cap_setAutoProperty(ctx_, stream_, CAPPROPID_EXPOSURE, 0);

        CapResult res = Cap_setProperty(ctx_, stream_, CAPPROPID_EXPOSURE, value);
        if (res != CAPRESULT_OK && res != CAPRESULT_PROPERTYNOTSUPPORTED) {
            spdlog::debug("Failed to set manual exposure {} on camera '{}'", value, camera_.name);
        }
    }
}

void USBDriver::setGain(GainMode mode, int value) {
    if (!isConnected()) return;

    // Note: openpnp-capture doesn't have auto-gain, only manual
    if (mode == GainMode::Manual) {
        CapResult res = Cap_setProperty(ctx_, stream_, CAPPROPID_GAIN, value);
        if (res != CAPRESULT_OK && res != CAPRESULT_PROPERTYNOTSUPPORTED) {
            spdlog::debug("Failed to set gain {} on camera '{}'", value, camera_.name);
        }
    }
}

int USBDriver::getExposure() const {
    if (!isConnected()) return 0;

    int32_t value = 0;
    CapResult res = Cap_getProperty(ctx_, stream_, CAPPROPID_EXPOSURE, &value);
    if (res != CAPRESULT_OK) {
        return 0;
    }
    return static_cast<int>(value);
}

int USBDriver::getGain() const {
    if (!isConnected()) return 0;

    int32_t value = 0;
    CapResult res = Cap_getProperty(ctx_, stream_, CAPPROPID_GAIN, &value);
    if (res != CAPRESULT_OK) {
        return 0;
    }
    return static_cast<int>(value);
}

CapDeviceID USBDriver::findDeviceId() const {
    uint32_t count = Cap_getDeviceCount(ctx_);
    if (count == 0) {
        return 0;
    }

    // Try exact match on unique ID first
    for (uint32_t i = 0; i < count; i++) {
        const char* uniqueId = Cap_getDeviceUniqueID(ctx_, i);
        if (uniqueId && camera_.identifier == uniqueId) {
            spdlog::debug("Found camera by unique ID: {}", uniqueId);
            return i;
        }
    }

    // Try matching by device name
    for (uint32_t i = 0; i < count; i++) {
        const char* name = Cap_getDeviceName(ctx_, i);
        if (name && camera_.identifier == name) {
            spdlog::debug("Found camera by name: {}", name);
            return i;
        }
    }

    // Try numeric index
    try {
        int index = std::stoi(camera_.identifier);
        if (index >= 0 && static_cast<uint32_t>(index) < count) {
            spdlog::debug("Using camera at numeric index: {}", index);
            return static_cast<CapDeviceID>(index);
        }
    } catch (...) {
        // Not a numeric index
    }

    // Default to first device
    spdlog::warn("Could not find device '{}', using device 0", camera_.identifier);
    return 0;
}

CapFormatID USBDriver::findBestFormat() const {
    int32_t numFormats = Cap_getNumFormats(ctx_, deviceId_);
    if (numFormats <= 0) {
        spdlog::warn("No formats available for device, using format 0");
        return 0;
    }

    // Parse requested resolution
    int reqWidth = 640;
    int reqHeight = 480;
    int reqFps = 30;

    if (camera_.resolution_json) {
        try {
            auto res = nlohmann::json::parse(*camera_.resolution_json);
            reqWidth = res["width"].get<int>();
            reqHeight = res["height"].get<int>();
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse resolution_json: {}", e.what());
        }
    }

    if (camera_.framerate) {
        reqFps = *camera_.framerate;
    }

    spdlog::debug("Looking for format closest to {}x{} @ {} fps", reqWidth, reqHeight, reqFps);

    // Find exact or closest match
    CapFormatID bestFormat = 0;
    int bestScore = INT_MAX;

    for (int32_t i = 0; i < numFormats; i++) {
        CapFormatInfo info;
        if (Cap_getFormatInfo(ctx_, deviceId_, static_cast<CapFormatID>(i), &info) == CAPRESULT_OK) {
            // Calculate score (lower is better)
            // Weight resolution differences more heavily than fps
            int widthDiff = std::abs(static_cast<int>(info.width) - reqWidth);
            int heightDiff = std::abs(static_cast<int>(info.height) - reqHeight);
            int fpsDiff = std::abs(static_cast<int>(info.fps) - reqFps);

            int score = widthDiff + heightDiff + fpsDiff * 10;

            if (score < bestScore) {
                bestScore = score;
                bestFormat = static_cast<CapFormatID>(i);
            }

            // Exact match - stop searching
            if (score == 0) {
                break;
            }
        }
    }

    // Log the selected format
    CapFormatInfo selectedInfo;
    if (Cap_getFormatInfo(ctx_, deviceId_, bestFormat, &selectedInfo) == CAPRESULT_OK) {
        spdlog::debug("Selected format: {}x{} @ {} fps (score: {})",
                      selectedInfo.width, selectedInfo.height, selectedInfo.fps, bestScore);
    }

    return bestFormat;
}

CapDeviceID USBDriver::findDeviceByIdentifier(CapContext ctx, const std::string& identifier) {
    uint32_t count = Cap_getDeviceCount(ctx);
    if (count == 0) {
        return 0;
    }

    // Try exact match on unique ID first
    for (uint32_t i = 0; i < count; i++) {
        const char* uniqueId = Cap_getDeviceUniqueID(ctx, i);
        if (uniqueId && identifier == uniqueId) {
            return i;
        }
    }

    // Try matching by device name
    for (uint32_t i = 0; i < count; i++) {
        const char* name = Cap_getDeviceName(ctx, i);
        if (name && identifier == name) {
            return i;
        }
    }

    // Try numeric index
    try {
        int index = std::stoi(identifier);
        if (index >= 0 && static_cast<uint32_t>(index) < count) {
            return static_cast<CapDeviceID>(index);
        }
    } catch (...) {
        // Not a numeric index
    }

    return 0;
}

std::vector<DeviceInfo> USBDriver::listDevices() {
    std::vector<DeviceInfo> devices;

    CapContext ctx = Cap_createContext();
    if (!ctx) {
        spdlog::error("Failed to create openpnp-capture context for device enumeration");
        return devices;
    }

    uint32_t count = Cap_getDeviceCount(ctx);
    spdlog::debug("openpnp-capture found {} USB camera(s)", count);

    for (uint32_t i = 0; i < count; i++) {
        DeviceInfo info;
        info.camera_type = CameraType::USB;

        // Get device name
        const char* name = Cap_getDeviceName(ctx, i);
        if (name && name[0] != '\0') {
            info.name = name;
        } else {
            info.name = "USB Camera " + std::to_string(i);
        }

        // Get unique identifier
        const char* uniqueId = Cap_getDeviceUniqueID(ctx, i);
        if (uniqueId && uniqueId[0] != '\0') {
            info.identifier = uniqueId;
        } else {
            info.identifier = std::to_string(i);
        }

        devices.push_back(info);
        spdlog::debug("Found USB camera {}: '{}' (id: '{}')", i, info.name, info.identifier);
    }

    Cap_releaseContext(ctx);

    spdlog::info("Discovered {} USB camera(s)", devices.size());
    return devices;
}

std::vector<CameraProfile> USBDriver::getSupportedProfiles(const std::string& identifier) {
    std::vector<CameraProfile> profiles;

    CapContext ctx = Cap_createContext();
    if (!ctx) {
        spdlog::error("Failed to create openpnp-capture context for profile enumeration");
        return profiles;
    }

    // Find device by identifier
    CapDeviceID deviceId = findDeviceByIdentifier(ctx, identifier);

    int32_t numFormats = Cap_getNumFormats(ctx, deviceId);
    if (numFormats < 0) {
        spdlog::warn("Failed to get formats for device '{}'", identifier);
        Cap_releaseContext(ctx);
        return profiles;
    }

    spdlog::debug("Device '{}' has {} format(s)", identifier, numFormats);

    for (int32_t i = 0; i < numFormats; i++) {
        CapFormatInfo info;
        if (Cap_getFormatInfo(ctx, deviceId, static_cast<CapFormatID>(i), &info) == CAPRESULT_OK) {
            CameraProfile profile;
            profile.width = static_cast<int>(info.width);
            profile.height = static_cast<int>(info.height);
            profile.fps = static_cast<int>(info.fps);

            // Check for duplicates (some cameras report the same resolution with different fourcc)
            bool exists = false;
            for (const auto& p : profiles) {
                if (p.width == profile.width &&
                    p.height == profile.height &&
                    p.fps == profile.fps) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                profiles.push_back(profile);
            }
        }
    }

    Cap_releaseContext(ctx);

    // Sort profiles by resolution (width, then height), then fps
    std::sort(profiles.begin(), profiles.end(), [](const CameraProfile& a, const CameraProfile& b) {
        if (a.width != b.width) return a.width < b.width;
        if (a.height != b.height) return a.height < b.height;
        return a.fps < b.fps;
    });

    spdlog::debug("Found {} unique profile(s) for device '{}'", profiles.size(), identifier);
    return profiles;
}

} // namespace vision
