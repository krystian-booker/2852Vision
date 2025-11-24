#include "drivers/usb_driver.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <dshow.h>
#pragma comment(lib, "strmiids.lib")

namespace {
// RAII wrapper for COM initialization
class COMInitializer {
public:
    COMInitializer() { CoInitialize(nullptr); }
    ~COMInitializer() { CoUninitialize(); }
    COMInitializer(const COMInitializer&) = delete;
    COMInitializer& operator=(const COMInitializer&) = delete;
};

// RAII wrapper for COM interfaces
template<typename T>
class ComPtr {
public:
    ComPtr() : ptr_(nullptr) {}
    ~ComPtr() { if (ptr_) ptr_->Release(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** addressOf() { return &ptr_; }
    T* get() { return ptr_; }
    T* operator->() { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_;
};
} // anonymous namespace
#endif

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

    int deviceIndex = getDeviceIndex();
    spdlog::info("Connecting to USB camera at index {}", deviceIndex);

    // Try DirectShow on Windows for better compatibility
#ifdef _WIN32
    capture_.open(deviceIndex, cv::CAP_DSHOW);
#else
    capture_.open(deviceIndex);
#endif

    if (!capture_.isOpened()) {
        spdlog::error("Failed to open USB camera at index {}", deviceIndex);
        return false;
    }

    applySettings();

    // Log actual camera properties after settings
    int actualWidth = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
    int actualHeight = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
    int actualFps = static_cast<int>(capture_.get(cv::CAP_PROP_FPS));
    spdlog::info("USB camera actual settings: {}x{} @ {} fps", actualWidth, actualHeight, actualFps);

    connected_ = true;
    spdlog::info("USB camera connected successfully at index {}", deviceIndex);
    return true;
}

void USBDriver::disconnect() {
    if (capture_.isOpened()) {
        capture_.release();
    }
    connected_ = false;
}

bool USBDriver::isConnected() const {
    return connected_ && capture_.isOpened();
}

FrameResult USBDriver::getFrame() {
    FrameResult result;

    if (!isConnected()) {
        spdlog::debug("getFrame called but camera not connected");
        return result;
    }

    // Use grab() + retrieve() instead of read() for better control
    // grab() is non-blocking on some backends
    if (!capture_.grab()) {
        spdlog::warn("Failed to grab frame from USB camera (camera_id: {})", camera_.id);
        return result;
    }

    cv::Mat frame;
    if (!capture_.retrieve(frame)) {
        spdlog::warn("Failed to retrieve frame from USB camera (camera_id: {})", camera_.id);
        return result;
    }

    if (frame.empty()) {
        spdlog::warn("Retrieved empty frame from USB camera (camera_id: {})", camera_.id);
        return result;
    }

    result.color = frame;
    return result;
}

void USBDriver::setExposure(ExposureMode mode, int value) {
    if (!isConnected()) return;

    if (mode == ExposureMode::Auto) {
        capture_.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);  // Auto
    } else {
        capture_.set(cv::CAP_PROP_AUTO_EXPOSURE, 0);  // Manual
        capture_.set(cv::CAP_PROP_EXPOSURE, value);
    }
}

void USBDriver::setGain(GainMode mode, int value) {
    if (!isConnected()) return;

    // Gain control varies by camera
    if (mode == GainMode::Manual) {
        capture_.set(cv::CAP_PROP_GAIN, value);
    }
}

int USBDriver::getExposure() const {
    if (!capture_.isOpened()) return 0;
    return static_cast<int>(capture_.get(cv::CAP_PROP_EXPOSURE));
}

int USBDriver::getGain() const {
    if (!capture_.isOpened()) return 0;
    return static_cast<int>(capture_.get(cv::CAP_PROP_GAIN));
}

int USBDriver::getDeviceIndex() const {
    // Try to parse index from identifier
    // Format could be: "usb:0", "0", or VID:PID:Serial format

    // Simple numeric index
    try {
        return std::stoi(camera_.identifier);
    } catch (...) {
        // Not a simple number
    }

    // usb:N format
    std::regex indexRegex("usb:(\\d+)");
    std::smatch match;
    if (std::regex_search(camera_.identifier, match, indexRegex)) {
        return std::stoi(match[1]);
    }

    // VID:PID:Serial format - need to enumerate and find matching device
    // For now, default to 0
    spdlog::warn("Could not parse device index from identifier '{}', using 0", camera_.identifier);
    return 0;
}

void USBDriver::applySettings() {
    if (!capture_.isOpened()) return;

    // Apply resolution if specified
    if (camera_.resolution_json) {
        try {
            auto res = nlohmann::json::parse(*camera_.resolution_json);
            int width = res["width"].get<int>();
            int height = res["height"].get<int>();
            capture_.set(cv::CAP_PROP_FRAME_WIDTH, width);
            capture_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
            spdlog::debug("Set resolution to {}x{}", width, height);
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse resolution: {}", e.what());
        }
    }

    // Apply framerate if specified
    if (camera_.framerate) {
        capture_.set(cv::CAP_PROP_FPS, *camera_.framerate);
        spdlog::debug("Set framerate to {}", *camera_.framerate);
    }

    // Apply exposure
    setExposure(camera_.exposure_mode, camera_.exposure_value);

    // Apply gain
    setGain(camera_.gain_mode, camera_.gain_value);
}

std::vector<DeviceInfo> USBDriver::listDevices() {
    std::vector<DeviceInfo> devices;

#ifdef _WIN32
    // Use DirectShow to enumerate devices with RAII for all COM objects
    COMInitializer comInit;

    ComPtr<ICreateDevEnum> devEnum;
    ComPtr<IEnumMoniker> enumMoniker;

    HRESULT hr = CoCreateInstance(
        CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum, (void**)devEnum.addressOf()
    );

    if (SUCCEEDED(hr)) {
        hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, enumMoniker.addressOf(), 0);

        if (hr == S_OK) {
            ComPtr<IMoniker> moniker;
            int index = 0;

            while (enumMoniker->Next(1, moniker.addressOf(), nullptr) == S_OK) {
                ComPtr<IPropertyBag> propBag;
                hr = moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)propBag.addressOf());

                if (SUCCEEDED(hr)) {
                    VARIANT var;
                    VariantInit(&var);

                    DeviceInfo info;
                    info.camera_type = CameraType::USB;
                    info.identifier = std::to_string(index);

                    // Get friendly name
                    hr = propBag->Read(L"FriendlyName", &var, nullptr);
                    if (SUCCEEDED(hr)) {
                        char nameBuffer[256];
                        WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, nameBuffer, 256, nullptr, nullptr);
                        info.name = nameBuffer;
                    } else {
                        info.name = "USB Camera " + std::to_string(index);
                    }
                    VariantClear(&var);  // Always clear, even on failure

                    // Get device path for more stable identifier
                    VariantInit(&var);
                    hr = propBag->Read(L"DevicePath", &var, nullptr);
                    if (SUCCEEDED(hr)) {
                        char pathBuffer[512];
                        WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, pathBuffer, 512, nullptr, nullptr);
                        // Extract VID/PID from path if available
                        std::string path(pathBuffer);
                        std::regex vidpidRegex("vid_([0-9a-f]+)&pid_([0-9a-f]+)", std::regex::icase);
                        std::smatch match;
                        if (std::regex_search(path, match, vidpidRegex)) {
                            info.identifier = match[1].str() + ":" + match[2].str() + ":" + std::to_string(index);
                        }
                    }
                    VariantClear(&var);  // Always clear, even on failure

                    devices.push_back(info);
                }
                moniker.reset();  // Release for next iteration
                index++;
            }
        }
    }
#else
    // Linux: probe common video device indices
    for (int i = 0; i < 10; i++) {
        cv::VideoCapture cap(i);
        if (cap.isOpened()) {
            DeviceInfo info;
            info.identifier = std::to_string(i);
            info.name = "USB Camera " + std::to_string(i);
            info.camera_type = CameraType::USB;
            devices.push_back(info);
            cap.release();
        }
    }
#endif

    spdlog::info("Discovered {} USB cameras", devices.size());
    return devices;
}

std::vector<CameraProfile> USBDriver::getSupportedProfiles(const std::string& identifier) {
    std::vector<CameraProfile> profiles;

    // Common profiles to test
    std::vector<std::tuple<int, int, int>> testProfiles = {
        {640, 480, 30},
        {640, 480, 60},
        {1280, 720, 30},
        {1280, 720, 60},
        {1920, 1080, 30},
        {1920, 1080, 60}
    };

    // Parse device index
    int index = 0;
    try {
        index = std::stoi(identifier);
    } catch (...) {
        // Try extracting from VID:PID:index format
        std::regex indexRegex(":(\\d+)$");
        std::smatch match;
        if (std::regex_search(identifier, match, indexRegex)) {
            index = std::stoi(match[1]);
        }
    }

    // Check if camera is already running to avoid conflict
    // Note: This requires including thread_manager.hpp which creates a circular dependency
    // Instead, we'll just try to open and catch failure gracefully
    
    // Open camera and test profiles
#ifdef _WIN32
    cv::VideoCapture cap(index, cv::CAP_DSHOW);
#else
    cv::VideoCapture cap(index);
#endif

    if (!cap.isOpened()) {
        spdlog::warn("Could not open camera {} to query profiles (might be in use)", identifier);
        // Return default profiles
        profiles.push_back({640, 480, 30});
        profiles.push_back({1280, 720, 30});
        return profiles;
    }

    for (const auto& [width, height, fps] : testProfiles) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        cap.set(cv::CAP_PROP_FPS, fps);

        // Check if settings were accepted
        int actualWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        int actualHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        int actualFps = static_cast<int>(cap.get(cv::CAP_PROP_FPS));

        // Check if this profile is already in the list
        bool exists = false;
        for (const auto& p : profiles) {
            if (p.width == actualWidth && p.height == actualHeight && p.fps == actualFps) {
                exists = true;
                break;
            }
        }

        if (!exists && actualWidth > 0 && actualHeight > 0) {
            profiles.push_back({actualWidth, actualHeight, actualFps > 0 ? actualFps : 30});
        }
    }

    cap.release();

    // Sort profiles
    std::sort(profiles.begin(), profiles.end(), [](const CameraProfile& a, const CameraProfile& b) {
        if (a.width != b.width) return a.width < b.width;
        if (a.height != b.height) return a.height < b.height;
        return a.fps < b.fps;
    });

    return profiles;
}

} // namespace vision
