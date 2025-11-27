#include "drivers/usb_driver.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <dshow.h>
#pragma comment(lib, "strmiids")
#include <map>
#endif

namespace vision {

// Helper for DirectShow enumeration
#ifdef _WIN32
struct DShowDevice {
    int index;
    std::string name;
    std::string path;
};

static std::vector<DShowDevice> enumDShowDevices() {
    std::vector<DShowDevice> devices;
    HRESULT hr;
    ICreateDevEnum* pDevEnum = NULL;
    IEnumMoniker* pEnum = NULL;

    CoInitialize(NULL);

    hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                          IID_ICreateDevEnum, (void**)&pDevEnum);
    if (SUCCEEDED(hr)) {
        hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
        if (hr == S_OK) {
            IMoniker* pMoniker = NULL;
            int index = 0;
            while (pEnum->Next(1, &pMoniker, NULL) == S_OK) {
                IPropertyBag* pPropBag;
                hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
                if (SUCCEEDED(hr)) {
                    VARIANT var;
                    VariantInit(&var);

                    // Get Friendly Name
                    std::string name = "USB Camera";
                    hr = pPropBag->Read(L"FriendlyName", &var, 0);
                    if (SUCCEEDED(hr)) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, NULL, 0, NULL, NULL);
                        if (len > 0) {
                            name.resize(len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, &name[0], len, NULL, NULL);
                        }
                        VariantClear(&var);
                    }

                    // Get Device Path
                    std::string path = "";
                    hr = pPropBag->Read(L"DevicePath", &var, 0);
                    if (SUCCEEDED(hr)) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, NULL, 0, NULL, NULL);
                        if (len > 0) {
                            path.resize(len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, var.bstrVal, -1, &path[0], len, NULL, NULL);
                        }
                        VariantClear(&var);
                    }

                    devices.push_back({index, name, path});
                    pPropBag->Release();
                }
                pMoniker->Release();
                index++;
            }
            pEnum->Release();
        }
        pDevEnum->Release();
    }
    CoUninitialize();
    CoUninitialize();
    return devices;
}

// Helper to get capabilities via DirectShow
static std::vector<CameraProfile> getDShowCapabilities(int deviceIndex) {
    std::vector<CameraProfile> profiles;
    HRESULT hr;
    ICreateDevEnum* pDevEnum = NULL;
    IEnumMoniker* pEnum = NULL;
    IMoniker* pMoniker = NULL;
    IBaseFilter* pFilter = NULL;
    IPin* pPin = NULL;
    IEnumPins* pEnumPins = NULL;
    IAMStreamConfig* pStreamConfig = NULL;

    CoInitialize(NULL);

    // Create Device Enumerator
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                          IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr)) {
        CoUninitialize();
        return profiles;
    }

    // Create Class Enumerator for Video Input Devices
    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    if (hr != S_OK) {
        pDevEnum->Release();
        CoUninitialize();
        return profiles;
    }

    // Skip to the requested device index
    pEnum->Skip(deviceIndex);
    
    if (pEnum->Next(1, &pMoniker, NULL) == S_OK) {
        // Bind Moniker to Filter
        hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pFilter);
        if (SUCCEEDED(hr)) {
            // Find the output pin
            hr = pFilter->EnumPins(&pEnumPins);
            if (SUCCEEDED(hr)) {
                while (pEnumPins->Next(1, &pPin, NULL) == S_OK) {
                    PIN_DIRECTION pinDir;
                    pPin->QueryDirection(&pinDir);
                    if (pinDir == PINDIR_OUTPUT) {
                        // Found output pin, get IAMStreamConfig
                        hr = pPin->QueryInterface(IID_IAMStreamConfig, (void**)&pStreamConfig);
                        if (SUCCEEDED(hr)) {
                            int count = 0, size = 0;
                            pStreamConfig->GetNumberOfCapabilities(&count, &size);

                            if (size == sizeof(VIDEO_STREAM_CONFIG_CAPS)) {
                                for (int i = 0; i < count; i++) {
                                    VIDEO_STREAM_CONFIG_CAPS scc;
                                    AM_MEDIA_TYPE* pmtConfig;
                                    hr = pStreamConfig->GetStreamCaps(i, &pmtConfig, (BYTE*)&scc);
                                    if (SUCCEEDED(hr)) {
                                        if (pmtConfig->formattype == FORMAT_VideoInfo) {
                                            VIDEOINFOHEADER* pVih = (VIDEOINFOHEADER*)pmtConfig->pbFormat;
                                            int w = pVih->bmiHeader.biWidth;
                                            int h = pVih->bmiHeader.biHeight;
                                            
                                            // Calculate FPS from AvgTimePerFrame (100ns units)
                                            // 10,000,000 / AvgTimePerFrame = FPS
                                            int fps = 0;
                                            if (pVih->AvgTimePerFrame > 0) {
                                                fps = static_cast<int>(10000000 / pVih->AvgTimePerFrame);
                                            } else if (scc.MinFrameInterval > 0) {
                                                 fps = static_cast<int>(10000000 / scc.MinFrameInterval);
                                            }

                                            if (w > 0 && h > 0 && fps > 0) {
                                                profiles.push_back({w, h, fps});
                                            }
                                        }
                                        // DeleteMediaType(pmtConfig); // Helper needed or manual delete
                                        if (pmtConfig->cbFormat != 0) CoTaskMemFree((PVOID)pmtConfig->pbFormat);
                                        if (pmtConfig->pUnk != NULL) pmtConfig->pUnk->Release();
                                        CoTaskMemFree(pmtConfig);
                                    }
                                }
                            }
                            pStreamConfig->Release();
                        }
                    }
                    pPin->Release();
                }
                pEnumPins->Release();
            }
            pFilter->Release();
        }
        pMoniker->Release();
    }

    pEnum->Release();
    pDevEnum->Release();
    CoUninitialize();

    // Remove duplicates and sort
    std::sort(profiles.begin(), profiles.end(), [](const CameraProfile& a, const CameraProfile& b) {
        if (a.width != b.width) return a.width > b.width;
        if (a.height != b.height) return a.height > b.height;
        return a.fps > b.fps;
    });
    
    profiles.erase(std::unique(profiles.begin(), profiles.end(), 
        [](const CameraProfile& a, const CameraProfile& b) {
            return a.width == b.width && a.height == b.height && a.fps == b.fps;
        }), profiles.end());

    return profiles;
}
#endif


USBDriver::USBDriver(const Camera& camera)
    : camera_(camera) {
}

USBDriver::~USBDriver() {
    disconnect();
}

bool USBDriver::connect(bool silent) {
    if (isConnected()) {
        return true;
    }

    int deviceIndex = findDeviceIndex(silent);
    if (deviceIndex < 0) {
        if (!silent) {
            spdlog::error("Could not find device index for camera '{}'", camera_.identifier);
        }
        return false;
    }

    // Open the camera
    // Prefer DirectShow on Windows for better control, otherwise ANY
#ifdef _WIN32
    if (!cap_.open(deviceIndex, cv::CAP_DSHOW)) {
#else
    if (!cap_.open(deviceIndex, cv::CAP_ANY)) {
#endif
        if (!silent) {
            spdlog::error("Failed to open USB camera '{}' at index {}", camera_.name, deviceIndex);
        }
        return false;
    }

    // Set resolution if specified
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

    // Try to set properties
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, reqWidth);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, reqHeight);
    cap_.set(cv::CAP_PROP_FPS, reqFps);

    // Verify what we actually got
    int actualWidth = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    int actualHeight = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    int actualFps = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));

    spdlog::info("USB camera '{}' connected. Requested: {}x{} @ {} fps. Actual: {}x{} @ {} fps",
                 camera_.name, reqWidth, reqHeight, reqFps, actualWidth, actualHeight, actualFps);

    // Apply camera settings
    setExposure(camera_.exposure_mode, camera_.exposure_value);
    setGain(camera_.gain_mode, camera_.gain_value);

    return true;
}

void USBDriver::disconnect() {
    if (cap_.isOpened()) {
        cap_.release();
    }
}

bool USBDriver::isConnected() const {
    return cap_.isOpened();
}

FrameResult USBDriver::getFrame() {
    FrameResult result;

    if (!isConnected()) {
        return result;
    }

    if (!cap_.read(result.color)) {
        spdlog::warn("Failed to read frame from USB camera '{}'", camera_.name);
    }

    return result;
}

void USBDriver::setExposure(ExposureMode mode, int value) {
    if (!isConnected()) return;

    if (mode == ExposureMode::Auto) {
        // OpenCV auto exposure is usually -1 or 0/1 depending on backend
        // For DirectShow: 1 = Manual, 8 = Auto? It varies wildly.
        // Usually setting CAP_PROP_AUTO_EXPOSURE to 3 (auto) or 1 (manual)
        // But for many webcams, just setting exposure value switches to manual.
        
        // Try standard approach:
        cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, 3); // 3 is often 'auto' in v4l2/dshow
    } else {
        cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, 1); // 1 is often 'manual'
        cap_.set(cv::CAP_PROP_EXPOSURE, value);
    }
}

void USBDriver::setGain(GainMode mode, int value) {
    if (!isConnected()) return;

    if (mode == GainMode::Manual) {
        cap_.set(cv::CAP_PROP_GAIN, value);
    }
    // OpenCV doesn't have a standard "Auto Gain" property separate from general auto modes usually
}

int USBDriver::getExposure() const {
    if (!isConnected()) return 0;
    return static_cast<int>(cap_.get(cv::CAP_PROP_EXPOSURE));
}

int USBDriver::getGain() const {
    if (!isConnected()) return 0;
    return static_cast<int>(cap_.get(cv::CAP_PROP_GAIN));
}

int USBDriver::findDeviceIndex(bool silent) const {
    // If identifier is an integer, use it directly (legacy support)
    try {
        return std::stoi(camera_.identifier);
    } catch (...) {
        // Not an integer, try to match by path using DirectShow
#ifdef _WIN32
        auto devices = enumDShowDevices();
        for (const auto& dev : devices) {
            if (dev.path == camera_.identifier) {
                return dev.index;
            }
        }
        // Fallback: try to match by name
        for (const auto& dev : devices) {
            if (dev.name == camera_.identifier) {
                return dev.index;
            }
        }
#endif
        if (!silent) {
            spdlog::error("Camera identifier '{}' not found", camera_.identifier);
        }
        return -1;
    }
}

std::vector<DeviceInfo> USBDriver::listDevices() {
    std::vector<DeviceInfo> devices;

#ifdef _WIN32
    auto dshowDevices = enumDShowDevices();
    for (const auto& d : dshowDevices) {
        DeviceInfo info;
        info.camera_type = CameraType::USB;
        info.identifier = d.path.empty() ? std::to_string(d.index) : d.path;
        info.name = d.name;
        info.serial_number = d.path; // Use path as serial for uniqueness
        devices.push_back(info);
        spdlog::info("Discovered USB Camera: '{}' ({})", d.name, d.path);
    }
    spdlog::info("Discovered {} USB cameras via DirectShow", devices.size());
#else
    // Fallback for non-Windows (OpenCV scan)
    spdlog::info("Scanning for USB cameras (indices 0-9)...");
    for (int i = 0; i < 10; i++) {
        cv::VideoCapture tempCap;
        if (tempCap.open(i, cv::CAP_ANY)) {
            if (tempCap.isOpened()) {
                DeviceInfo info;
                info.camera_type = CameraType::USB;
                info.identifier = std::to_string(i);
                info.name = "USB Camera " + std::to_string(i);
                devices.push_back(info);
                tempCap.release();
            }
        }
    }
#endif

    return devices;
}

std::vector<CameraProfile> USBDriver::getSupportedProfiles(const std::string& identifier) {
    std::vector<CameraProfile> profiles;
    
    // We need to open the camera to test resolutions
    int index = 0;
    try {
        index = std::stoi(identifier);
    } catch (...) {
#ifdef _WIN32
        auto devices = enumDShowDevices();
        for (const auto& dev : devices) {
            if (dev.path == identifier) {
                index = dev.index;
                break;
            }
        }
#endif
    }

    // Try DirectShow first on Windows
#ifdef _WIN32
    profiles = getDShowCapabilities(index);
    if (!profiles.empty()) {
        spdlog::info("Retrieved {} profiles via DirectShow for camera {}", profiles.size(), identifier);
        return profiles;
    }
    spdlog::warn("DirectShow capability query failed, falling back to probing");
#endif

    cv::VideoCapture cap;
#ifdef _WIN32
    if (!cap.open(index, cv::CAP_DSHOW)) return profiles;
#else
    if (!cap.open(index, cv::CAP_ANY)) return profiles;
#endif

    struct Res { int w; int h; };
    std::vector<Res> commonResolutions = {
        {1920, 1080}, {1280, 720}, {1280, 960}, {1600, 1200},
        {800, 600}, {640, 480}, {320, 240}
    };

    for (const auto& res : commonResolutions) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, res.w);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, res.h);
        
        int w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        int h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        
        if (w == res.w && h == res.h) {
            // Check 30 and 60 fps
            profiles.push_back({w, h, 30});
            
            // Try setting higher FPS
            cap.set(cv::CAP_PROP_FPS, 60);
            if (cap.get(cv::CAP_PROP_FPS) >= 59) {
                profiles.push_back({w, h, 60});
            }
        }
    }
    
    // Sort
    std::sort(profiles.begin(), profiles.end(), [](const CameraProfile& a, const CameraProfile& b) {
        return (a.width * a.height) > (b.width * b.height);
    });

    return profiles;
}

void USBDriver::setFocus(bool autoFocus, int value) {
    if (!isConnected()) return;
    
    if (autoFocus) {
        cap_.set(cv::CAP_PROP_AUTOFOCUS, 1);
    } else {
        cap_.set(cv::CAP_PROP_AUTOFOCUS, 0);
        cap_.set(cv::CAP_PROP_FOCUS, value);
    }
}

void USBDriver::setWhiteBalance(bool autoWB, int value) {
    if (!isConnected()) return;

    if (autoWB) {
        cap_.set(cv::CAP_PROP_AUTO_WB, 1);
    } else {
        cap_.set(cv::CAP_PROP_AUTO_WB, 0);
        cap_.set(cv::CAP_PROP_WB_TEMPERATURE, value);
    }
}

} // namespace vision
