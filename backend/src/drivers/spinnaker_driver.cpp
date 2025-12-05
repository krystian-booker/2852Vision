#include "drivers/spinnaker_driver.hpp"
#include "drivers/spinnaker_loader.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <delayimp.h>
#endif

namespace vision {

// Static member initialization
std::mutex SpinnakerDriver::systemMutex_;
bool SpinnakerDriver::initialized_ = false;

#ifdef VISION_WITH_SPINNAKER
Spinnaker::SystemPtr SpinnakerDriver::system_ = nullptr;

// Helper function to initialize Spinnaker system
// Separated from initialize() to avoid mixing SEH with C++ exceptions (MSVC limitation)
static bool initializeSpinnakerSystem(Spinnaker::SystemPtr& system) {
    try {
        system = Spinnaker::System::GetInstance();

        const Spinnaker::LibraryVersion libVersion = system->GetLibraryVersion();
        spdlog::info("Spinnaker SDK initialized - version {}.{}.{}.{}",
                     libVersion.major, libVersion.minor, libVersion.type, libVersion.build);

        return true;
    } catch (Spinnaker::Exception& e) {
        spdlog::error("Failed to initialize Spinnaker SDK: {}", e.what());
        return false;
    }
}

#ifdef _WIN32
// SEH wrapper function - must be separate from functions with C++ objects requiring unwinding
static bool initializeWithSEH(Spinnaker::SystemPtr& system) {
    __try {
        return initializeSpinnakerSystem(system);
    } __except (GetExceptionCode() == VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND) ||
                GetExceptionCode() == VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)
                ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        spdlog::warn("Spinnaker SDK DLL could not be loaded. FLIR/Point Grey cameras will not be available.");
        return false;
    }
}
#endif
#endif

nlohmann::json SpinnakerNode::toJson() const {
    nlohmann::json j = {
        {"name", name},
        {"display_name", display_name},
        {"description", description},
        {"interface_type", interface_type},
        {"access_mode", access_mode},
        {"is_readable", is_readable},
        {"is_writable", is_writable},
        {"value", value},
        {"choices", choices}
    };

    // Add numeric range info if present
    if (!min_value.empty()) {
        j["min_value"] = min_value;
    }
    if (!max_value.empty()) {
        j["max_value"] = max_value;
    }
    if (!increment.empty()) {
        j["increment"] = increment;
    }

    return j;
}

SpinnakerDriver::SpinnakerDriver(const Camera& camera)
    : camera_(camera) {
}

SpinnakerDriver::~SpinnakerDriver() {
    disconnect();
}

bool SpinnakerDriver::isAvailable() {
#ifdef VISION_WITH_SPINNAKER
    return initialized_;
#else
    return false;
#endif
}

void SpinnakerDriver::initialize() {
    std::lock_guard<std::mutex> lock(systemMutex_);

    if (initialized_) {
        return;
    }

#ifdef VISION_WITH_SPINNAKER
    // First, check if Spinnaker SDK DLLs are available
    if (!SpinnakerLoader::tryLoad()) {
        spdlog::warn("Spinnaker SDK not available: {}", SpinnakerLoader::getLoadError());
        initialized_ = false;
        return;
    }

#ifdef _WIN32
    // Use SEH to catch delay-load DLL failures on Windows
    // Note: SEH and C++ exceptions cannot be mixed in the same function,
    // so the SEH block is in initializeWithSEH() helper function
    initialized_ = initializeWithSEH(system_);
#else
    initialized_ = initializeSpinnakerSystem(system_);
#endif
#else
    spdlog::warn("Spinnaker support not compiled in. Rebuild with --spinnaker=y");
#endif
}

void SpinnakerDriver::shutdown() {
    std::lock_guard<std::mutex> lock(systemMutex_);

    if (!initialized_) {
        return;
    }

#ifdef VISION_WITH_SPINNAKER
    try {
        if (system_) {
            system_->ReleaseInstance();
            system_ = nullptr;
        }
        spdlog::info("Spinnaker SDK shutdown complete");
    } catch (Spinnaker::Exception& e) {
        spdlog::error("Error during Spinnaker shutdown: {}", e.what());
    }

    // Unload the dynamically loaded library
    SpinnakerLoader::unload();
#endif

    initialized_ = false;
}

#ifdef VISION_WITH_SPINNAKER

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool SpinnakerDriver::connect(bool silent) {
    if (connected_) {
        return true;
    }

    if (!initialized_) {
        if (!silent) {
            spdlog::error("Spinnaker SDK not initialized. Call initialize() first.");
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(systemMutex_);

    try {
        if (!silent) {
            spdlog::info("Connecting to Spinnaker camera: {}", camera_.identifier);
        }

        // Get camera list
        Spinnaker::CameraList camList = system_->GetCameras();

        if (camList.GetSize() == 0) {
            if (!silent) {
                spdlog::error("No Spinnaker cameras found");
            }
            camList.Clear();
            return false;
        }

        // Find camera by serial number
        cameraPtr_ = nullptr;
        for (unsigned int i = 0; i < camList.GetSize(); i++) {
            Spinnaker::CameraPtr cam = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap& nodeMapTL = cam->GetTLDeviceNodeMap();

            Spinnaker::GenApi::CStringPtr serialPtr = nodeMapTL.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsAvailable(serialPtr) && Spinnaker::GenApi::IsReadable(serialPtr)) {
                std::string serial = serialPtr->GetValue().c_str();
                if (serial == camera_.identifier) {
                    cameraPtr_ = cam;
                    break;
                }
            }
        }

        if (!cameraPtr_) {
            if (!silent) {
                spdlog::error("Camera with serial {} not found", camera_.identifier);
            }
            camList.Clear();
            return false;
        }

        // Initialize camera
        cameraPtr_->Init();

        // Configure camera settings
        configureCamera();
        configureStreamBuffers();

        // Begin acquisition
        cameraPtr_->BeginAcquisition();

        connected_ = true;
        spdlog::info("Successfully connected to Spinnaker camera {}", camera_.identifier);

        camList.Clear();
        return true;

    } catch (Spinnaker::Exception& e) {
        if (!silent) {
            spdlog::error("Failed to connect to Spinnaker camera {}: {}", camera_.identifier, e.what());
        }
        cameraPtr_ = nullptr;
        return false;
    }
}

void SpinnakerDriver::disconnect() {
    if (!connected_) {
        return;
    }

    spdlog::info("Disconnecting Spinnaker camera {}", camera_.identifier);

    try {
        if (cameraPtr_) {
            if (cameraPtr_->IsStreaming()) {
                cameraPtr_->EndAcquisition();
            }
            cameraPtr_->DeInit();
            cameraPtr_ = nullptr;
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Error during Spinnaker disconnect: {}", e.what());
    }

    connected_ = false;
}

bool SpinnakerDriver::isConnected() const {
    return connected_;
}

void SpinnakerDriver::configureCamera() {
    Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();

    try {
        // Set acquisition mode to continuous
        Spinnaker::GenApi::CEnumerationPtr acqMode = nodeMap.GetNode("AcquisitionMode");
        if (Spinnaker::GenApi::IsAvailable(acqMode) && Spinnaker::GenApi::IsWritable(acqMode)) {
            Spinnaker::GenApi::CEnumEntryPtr acqModeContinuous = acqMode->GetEntryByName("Continuous");
            if (Spinnaker::GenApi::IsAvailable(acqModeContinuous) && Spinnaker::GenApi::IsReadable(acqModeContinuous)) {
                acqMode->SetIntValue(acqModeContinuous->GetValue());
            }
        }

        // Set pixel format based on camera capabilities
        Spinnaker::GenApi::CEnumerationPtr pixelFormat = nodeMap.GetNode("PixelFormat");
        if (Spinnaker::GenApi::IsAvailable(pixelFormat) && Spinnaker::GenApi::IsWritable(pixelFormat)) {
            // Check if this is a mono-only camera by testing for Mono8
            Spinnaker::GenApi::CEnumEntryPtr mono8Entry = pixelFormat->GetEntryByName("Mono8");
            Spinnaker::GenApi::CEnumEntryPtr bgr8Entry = pixelFormat->GetEntryByName("BGR8");
            Spinnaker::GenApi::CEnumEntryPtr rgb8Entry = pixelFormat->GetEntryByName("RGB8");
            Spinnaker::GenApi::CEnumEntryPtr bayerEntry = pixelFormat->GetEntryByName("BayerRG8");

            bool hasMono8 = Spinnaker::GenApi::IsAvailable(mono8Entry) && Spinnaker::GenApi::IsReadable(mono8Entry);
            bool hasBGR8 = Spinnaker::GenApi::IsAvailable(bgr8Entry) && Spinnaker::GenApi::IsReadable(bgr8Entry);
            bool hasRGB8 = Spinnaker::GenApi::IsAvailable(rgb8Entry) && Spinnaker::GenApi::IsReadable(rgb8Entry);
            bool hasBayer = Spinnaker::GenApi::IsAvailable(bayerEntry) && Spinnaker::GenApi::IsReadable(bayerEntry);

            // Detect mono camera: has Mono8 but no color formats
            is_mono_camera_ = hasMono8 && !hasBGR8 && !hasRGB8 && !hasBayer;

            Spinnaker::GenApi::CEnumEntryPtr pixelFormatEntry = nullptr;

            if (is_mono_camera_) {
                // Mono camera - use Mono8 directly (optimal)
                pixelFormatEntry = mono8Entry;
                spdlog::info("Detected mono camera - using Mono8 format (no conversion needed)");
            } else {
                // Color camera - try color formats first
                if (hasBGR8) {
                    pixelFormatEntry = bgr8Entry;
                } else if (hasRGB8) {
                    pixelFormatEntry = rgb8Entry;
                } else if (hasBayer) {
                    pixelFormatEntry = bayerEntry;
                } else if (hasMono8) {
                    pixelFormatEntry = mono8Entry;
                }
            }

            if (pixelFormatEntry) {
                pixelFormat->SetIntValue(pixelFormatEntry->GetValue());
                spdlog::debug("Set pixel format to: {}", pixelFormatEntry->GetSymbolic().c_str());
            }
        }

        // Apply resolution if specified
        if (camera_.resolution_json) {
            try {
                auto res = nlohmann::json::parse(*camera_.resolution_json);

                Spinnaker::GenApi::CIntegerPtr width = nodeMap.GetNode("Width");
                Spinnaker::GenApi::CIntegerPtr height = nodeMap.GetNode("Height");

                if (Spinnaker::GenApi::IsAvailable(width) && Spinnaker::GenApi::IsWritable(width)) {
                    int64_t reqWidth = res["width"].get<int64_t>();
                    int64_t maxWidth = width->GetMax();
                    width->SetValue((std::min)(reqWidth, maxWidth));
                }

                if (Spinnaker::GenApi::IsAvailable(height) && Spinnaker::GenApi::IsWritable(height)) {
                    int64_t reqHeight = res["height"].get<int64_t>();
                    int64_t maxHeight = height->GetMax();
                    height->SetValue((std::min)(reqHeight, maxHeight));
                }
            } catch (const std::exception& e) {
                spdlog::warn("Failed to parse/apply resolution: {}", e.what());
            }
        }

        // Apply framerate if specified
        if (camera_.framerate) {
            try {
                // Enable frame rate control
                Spinnaker::GenApi::CBooleanPtr frameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
                if (Spinnaker::GenApi::IsAvailable(frameRateEnable) && Spinnaker::GenApi::IsWritable(frameRateEnable)) {
                    frameRateEnable->SetValue(true);
                }

                Spinnaker::GenApi::CFloatPtr frameRate = nodeMap.GetNode("AcquisitionFrameRate");
                if (Spinnaker::GenApi::IsAvailable(frameRate) && Spinnaker::GenApi::IsWritable(frameRate)) {
                    double maxFps = frameRate->GetMax();
                    double reqFps = static_cast<double>(*camera_.framerate);
                    frameRate->SetValue((std::min)(reqFps, maxFps));
                }
            } catch (Spinnaker::Exception& e) {
                spdlog::warn("Failed to set framerate: {}", e.what());
            }
        }

        // Apply initial exposure settings
        setExposure(camera_.exposure_mode, camera_.exposure_value);
        setGain(camera_.gain_mode, camera_.gain_value);

    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Error during camera configuration: {}", e.what());
    }
}

void SpinnakerDriver::configureStreamBuffers() {
    try {
        // Configure stream buffer handling for performance
        Spinnaker::GenApi::INodeMap& streamNodeMap = cameraPtr_->GetTLStreamNodeMap();

        // Set buffer handling mode to NewestOnly to reduce latency
        Spinnaker::GenApi::CEnumerationPtr bufferHandlingMode = streamNodeMap.GetNode("StreamBufferHandlingMode");
        if (Spinnaker::GenApi::IsAvailable(bufferHandlingMode) && Spinnaker::GenApi::IsWritable(bufferHandlingMode)) {
            Spinnaker::GenApi::CEnumEntryPtr newestOnly = bufferHandlingMode->GetEntryByName("NewestOnly");
            if (Spinnaker::GenApi::IsAvailable(newestOnly) && Spinnaker::GenApi::IsReadable(newestOnly)) {
                bufferHandlingMode->SetIntValue(newestOnly->GetValue());
            }
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Failed to configure stream buffers: {}", e.what());
    }
}

// ============================================================================
// FRAME ACQUISITION
// ============================================================================

FrameResult SpinnakerDriver::getFrame() {
    FrameResult result;

    if (!connected_ || !cameraPtr_) {
        return result;
    }

    try {
        // Get next image with 5 second timeout
        Spinnaker::ImagePtr image = cameraPtr_->GetNextImage(5000);

        if (image->IsIncomplete()) {
            spdlog::warn("Image incomplete with status: {}",
                        Spinnaker::Image::GetImageStatusDescription(image->GetImageStatus()));
            image->Release();
            return result;
        }

        // Convert frame (preserves Mono8 for mono cameras, converts to BGR for color cameras)
        result.color = convertFrame(image);

        image->Release();

    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Spinnaker getFrame error: {}", e.what());
    }

    return result;
}

cv::Mat SpinnakerDriver::convertFrame(const Spinnaker::ImagePtr& image) {
    cv::Mat result;

    try {
        size_t width = image->GetWidth();
        size_t height = image->GetHeight();
        Spinnaker::PixelFormatEnums pixelFormat = image->GetPixelFormat();

        if (pixelFormat == Spinnaker::PixelFormat_BGR8) {
            // Already BGR8, use directly
            cv::Mat temp(static_cast<int>(height), static_cast<int>(width), CV_8UC3,
                        image->GetData(), image->GetStride());
            result = temp.clone();
        } else if (pixelFormat == Spinnaker::PixelFormat_Mono8) {
            // Grayscale - preserve as CV_8UC1 (no conversion needed for mono cameras)
            cv::Mat temp(static_cast<int>(height), static_cast<int>(width), CV_8UC1,
                        image->GetData(), image->GetStride());
            result = temp.clone();
        } else {
            // Use Spinnaker conversion for other formats (Bayer, RGB, etc.)
            Spinnaker::ImageProcessor processor;
            processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_HQ_LINEAR);
            Spinnaker::ImagePtr convertedImage = processor.Convert(image, Spinnaker::PixelFormat_BGR8);

            cv::Mat temp(static_cast<int>(convertedImage->GetHeight()),
                        static_cast<int>(convertedImage->GetWidth()),
                        CV_8UC3,
                        convertedImage->GetData(),
                        convertedImage->GetStride());
            result = temp.clone();
        }

    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Image conversion error: {}", e.what());
    }

    return result;
}

// ============================================================================
// CAMERA CONTROLS
// ============================================================================

void SpinnakerDriver::setExposure(ExposureMode mode, int value) {
    if (!connected_ || !cameraPtr_) return;

    try {
        Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();

        // Set exposure auto mode
        Spinnaker::GenApi::CEnumerationPtr exposureAuto = nodeMap.GetNode("ExposureAuto");
        if (Spinnaker::GenApi::IsAvailable(exposureAuto) && Spinnaker::GenApi::IsWritable(exposureAuto)) {
            if (mode == ExposureMode::Auto) {
                Spinnaker::GenApi::CEnumEntryPtr autoEntry = exposureAuto->GetEntryByName("Continuous");
                if (Spinnaker::GenApi::IsAvailable(autoEntry) && Spinnaker::GenApi::IsReadable(autoEntry)) {
                    exposureAuto->SetIntValue(autoEntry->GetValue());
                }
            } else {
                Spinnaker::GenApi::CEnumEntryPtr offEntry = exposureAuto->GetEntryByName("Off");
                if (Spinnaker::GenApi::IsAvailable(offEntry) && Spinnaker::GenApi::IsReadable(offEntry)) {
                    exposureAuto->SetIntValue(offEntry->GetValue());
                }

                // Set manual exposure value (in microseconds)
                Spinnaker::GenApi::CFloatPtr exposureTime = nodeMap.GetNode("ExposureTime");
                if (Spinnaker::GenApi::IsAvailable(exposureTime) && Spinnaker::GenApi::IsWritable(exposureTime)) {
                    double minExp = exposureTime->GetMin();
                    double maxExp = exposureTime->GetMax();
                    double expValue = static_cast<double>(value);
                    exposureTime->SetValue(std::clamp(expValue, minExp, maxExp));
                }
            }
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Failed to set exposure: {}", e.what());
    }
}

void SpinnakerDriver::setGain(GainMode mode, int value) {
    if (!connected_ || !cameraPtr_) return;

    try {
        Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();

        // Set gain auto mode
        Spinnaker::GenApi::CEnumerationPtr gainAuto = nodeMap.GetNode("GainAuto");
        if (Spinnaker::GenApi::IsAvailable(gainAuto) && Spinnaker::GenApi::IsWritable(gainAuto)) {
            if (mode == GainMode::Auto) {
                Spinnaker::GenApi::CEnumEntryPtr autoEntry = gainAuto->GetEntryByName("Continuous");
                if (Spinnaker::GenApi::IsAvailable(autoEntry) && Spinnaker::GenApi::IsReadable(autoEntry)) {
                    gainAuto->SetIntValue(autoEntry->GetValue());
                }
            } else {
                Spinnaker::GenApi::CEnumEntryPtr offEntry = gainAuto->GetEntryByName("Off");
                if (Spinnaker::GenApi::IsAvailable(offEntry) && Spinnaker::GenApi::IsReadable(offEntry)) {
                    gainAuto->SetIntValue(offEntry->GetValue());
                }

                // Set manual gain value (in dB)
                Spinnaker::GenApi::CFloatPtr gain = nodeMap.GetNode("Gain");
                if (Spinnaker::GenApi::IsAvailable(gain) && Spinnaker::GenApi::IsWritable(gain)) {
                    double minGain = gain->GetMin();
                    double maxGain = gain->GetMax();
                    double gainValue = static_cast<double>(value);
                    gain->SetValue(std::clamp(gainValue, minGain, maxGain));
                }
            }
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Failed to set gain: {}", e.what());
    }
}

int SpinnakerDriver::getExposure() const {
    if (!connected_ || !cameraPtr_) return 0;

    try {
        Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();

        Spinnaker::GenApi::CFloatPtr exposureTime = nodeMap.GetNode("ExposureTime");
        if (Spinnaker::GenApi::IsAvailable(exposureTime) && Spinnaker::GenApi::IsReadable(exposureTime)) {
            return static_cast<int>(exposureTime->GetValue());
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Failed to get exposure: {}", e.what());
    }

    return 0;
}

int SpinnakerDriver::getGain() const {
    if (!connected_ || !cameraPtr_) return 0;

    try {
        Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();

        Spinnaker::GenApi::CFloatPtr gain = nodeMap.GetNode("Gain");
        if (Spinnaker::GenApi::IsAvailable(gain) && Spinnaker::GenApi::IsReadable(gain)) {
            return static_cast<int>(gain->GetValue());
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Failed to get gain: {}", e.what());
    }

    return 0;
}

BaseDriver::Range SpinnakerDriver::getExposureRange() const {
    if (!connected_ || !cameraPtr_) return {0, 10000, 1, 500};

    try {
        Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();
        Spinnaker::GenApi::CFloatPtr exposureTime = nodeMap.GetNode("ExposureTime");
        
        if (Spinnaker::GenApi::IsAvailable(exposureTime) && Spinnaker::GenApi::IsReadable(exposureTime)) {
            // Spinnaker exposure is float, we cast to int
            // Step might not be available for float, assume 1
            return {
                static_cast<int>(exposureTime->GetMin()),
                static_cast<int>(exposureTime->GetMax()),
                1, 
                500 // Default fallback as we can't easily query "factory default"
            };
        }
    } catch (...) {}
    return {0, 10000, 1, 500};
}

BaseDriver::Range SpinnakerDriver::getGainRange() const {
    if (!connected_ || !cameraPtr_) return {0, 100, 1, 0};

    try {
        Spinnaker::GenApi::INodeMap& nodeMap = cameraPtr_->GetNodeMap();
        Spinnaker::GenApi::CFloatPtr gain = nodeMap.GetNode("Gain");
        
        if (Spinnaker::GenApi::IsAvailable(gain) && Spinnaker::GenApi::IsReadable(gain)) {
            return {
                static_cast<int>(gain->GetMin()),
                static_cast<int>(gain->GetMax()),
                1,
                0 // Default fallback
            };
        }
    } catch (...) {}
    return {0, 100, 1, 0};
}

// ============================================================================
// DEVICE DISCOVERY
// ============================================================================

std::vector<DeviceInfo> SpinnakerDriver::listDevices() {
    std::vector<DeviceInfo> devices;

    if (!initialized_) {
        return devices;
    }

    std::lock_guard<std::mutex> lock(systemMutex_);

    try {
        Spinnaker::CameraList camList = system_->GetCameras();

        spdlog::debug("Found {} Spinnaker cameras", camList.GetSize());

        for (unsigned int i = 0; i < camList.GetSize(); i++) {
            Spinnaker::CameraPtr cam = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap& nodeMapTL = cam->GetTLDeviceNodeMap();

            DeviceInfo info;
            info.camera_type = CameraType::Spinnaker;  // Keep as Spinnaker for compatibility

            // Get serial number
            Spinnaker::GenApi::CStringPtr serialPtr = nodeMapTL.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsAvailable(serialPtr) && Spinnaker::GenApi::IsReadable(serialPtr)) {
                info.identifier = serialPtr->GetValue().c_str();
                info.serial_number = info.identifier;
            }

            // Get model name
            Spinnaker::GenApi::CStringPtr modelPtr = nodeMapTL.GetNode("DeviceModelName");
            if (Spinnaker::GenApi::IsAvailable(modelPtr) && Spinnaker::GenApi::IsReadable(modelPtr)) {
                info.name = modelPtr->GetValue().c_str();
            }

            // Get manufacturer (vendor)
            Spinnaker::GenApi::CStringPtr vendorPtr = nodeMapTL.GetNode("DeviceVendorName");
            if (Spinnaker::GenApi::IsAvailable(vendorPtr) && Spinnaker::GenApi::IsReadable(vendorPtr)) {
                info.manufacturer = vendorPtr->GetValue().c_str();
            } else {
                info.manufacturer = "FLIR";
            }

            // Get product info
            Spinnaker::GenApi::CStringPtr productPtr = nodeMapTL.GetNode("DeviceDisplayName");
            if (Spinnaker::GenApi::IsAvailable(productPtr) && Spinnaker::GenApi::IsReadable(productPtr)) {
                info.product = productPtr->GetValue().c_str();
            }

            devices.push_back(info);
        }

        camList.Clear();
        spdlog::info("Discovered {} Spinnaker cameras", devices.size());

    } catch (Spinnaker::Exception& e) {
        spdlog::error("Error listing Spinnaker cameras: {}", e.what());
    }

    return devices;
}

std::vector<CameraProfile> SpinnakerDriver::getSupportedProfiles(const std::string& identifier) {
    std::vector<CameraProfile> profiles;

    if (!initialized_) {
        return profiles;
    }

    std::lock_guard<std::mutex> lock(systemMutex_);

    try {
        Spinnaker::CameraList camList = system_->GetCameras();
        Spinnaker::CameraPtr cam = nullptr;

        // Find camera by serial
        for (unsigned int i = 0; i < camList.GetSize(); i++) {
            Spinnaker::CameraPtr c = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap& nodeMapTL = c->GetTLDeviceNodeMap();

            Spinnaker::GenApi::CStringPtr serialPtr = nodeMapTL.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsAvailable(serialPtr) && Spinnaker::GenApi::IsReadable(serialPtr)) {
                if (std::string(serialPtr->GetValue().c_str()) == identifier) {
                    cam = c;
                    break;
                }
            }
        }

        if (!cam) {
            camList.Clear();
            return profiles;
        }

        // Initialize camera temporarily to query capabilities
        cam->Init();
        Spinnaker::GenApi::INodeMap& nodeMap = cam->GetNodeMap();

        // Get max resolution
        int maxWidth = 1920, maxHeight = 1080;
        Spinnaker::GenApi::CIntegerPtr widthNode = nodeMap.GetNode("Width");
        Spinnaker::GenApi::CIntegerPtr heightNode = nodeMap.GetNode("Height");

        if (Spinnaker::GenApi::IsAvailable(widthNode) && Spinnaker::GenApi::IsReadable(widthNode)) {
            maxWidth = static_cast<int>(widthNode->GetMax());
        }
        if (Spinnaker::GenApi::IsAvailable(heightNode) && Spinnaker::GenApi::IsReadable(heightNode)) {
            maxHeight = static_cast<int>(heightNode->GetMax());
        }

        // Get max framerate
        double maxFps = 60.0;
        Spinnaker::GenApi::CFloatPtr fpsNode = nodeMap.GetNode("AcquisitionFrameRate");
        if (Spinnaker::GenApi::IsAvailable(fpsNode) && Spinnaker::GenApi::IsReadable(fpsNode)) {
            maxFps = fpsNode->GetMax();
        }

        cam->DeInit();
        camList.Clear();

        // Generate profiles based on camera capabilities
        std::vector<std::pair<int, int>> resolutions = {
            {maxWidth, maxHeight},
            {1920, 1080},
            {1280, 960},
            {1280, 720},
            {640, 480}
        };

        std::vector<int> framerates = {120, 60, 30, 15};

        for (const auto& [w, h] : resolutions) {
            if (w <= maxWidth && h <= maxHeight) {
                for (int fps : framerates) {
                    if (fps <= static_cast<int>(maxFps)) {
                        profiles.push_back({w, h, fps});
                    }
                }
            }
        }

        // Sort by resolution (highest first)
        std::sort(profiles.begin(), profiles.end(), [](const CameraProfile& a, const CameraProfile& b) {
            int aPixels = a.width * a.height;
            int bPixels = b.width * b.height;
            if (aPixels != bPixels) return aPixels > bPixels;
            return a.fps > b.fps;
        });

        // Remove duplicates
        profiles.erase(std::unique(profiles.begin(), profiles.end(),
            [](const CameraProfile& a, const CameraProfile& b) {
                return a.width == b.width && a.height == b.height && a.fps == b.fps;
            }), profiles.end());

    } catch (Spinnaker::Exception& e) {
        spdlog::error("Error querying Spinnaker profiles: {}", e.what());
    }

    return profiles;
}

// ============================================================================
// NODE MAP OPERATIONS
// ============================================================================

std::string SpinnakerDriver::getAccessModeString(Spinnaker::GenApi::EAccessMode mode) {
    switch (mode) {
        case Spinnaker::GenApi::NI: return "NotImplemented";
        case Spinnaker::GenApi::NA: return "NotAvailable";
        case Spinnaker::GenApi::WO: return "WriteOnly";
        case Spinnaker::GenApi::RO: return "ReadOnly";
        case Spinnaker::GenApi::RW: return "ReadWrite";
        default: return "Unknown";
    }
}

std::string SpinnakerDriver::getNodeValue(Spinnaker::GenApi::INode* node) {
    if (!node) return "";

    try {
        switch (node->GetPrincipalInterfaceType()) {
            case Spinnaker::GenApi::intfIInteger: {
                Spinnaker::GenApi::CIntegerPtr intNode = static_cast<Spinnaker::GenApi::CIntegerPtr>(node);
                return std::to_string(intNode->GetValue());
            }
            case Spinnaker::GenApi::intfIFloat: {
                Spinnaker::GenApi::CFloatPtr floatNode = static_cast<Spinnaker::GenApi::CFloatPtr>(node);
                return std::to_string(floatNode->GetValue());
            }
            case Spinnaker::GenApi::intfIBoolean: {
                Spinnaker::GenApi::CBooleanPtr boolNode = static_cast<Spinnaker::GenApi::CBooleanPtr>(node);
                return boolNode->GetValue() ? "true" : "false";
            }
            case Spinnaker::GenApi::intfIString: {
                Spinnaker::GenApi::CStringPtr strNode = static_cast<Spinnaker::GenApi::CStringPtr>(node);
                return strNode->GetValue().c_str();
            }
            case Spinnaker::GenApi::intfIEnumeration: {
                Spinnaker::GenApi::CEnumerationPtr enumNode = static_cast<Spinnaker::GenApi::CEnumerationPtr>(node);
                Spinnaker::GenApi::CEnumEntryPtr entry = enumNode->GetCurrentEntry();
                if (entry) {
                    return entry->GetSymbolic().c_str();
                }
                return "";
            }
            default:
                return "";
        }
    } catch (...) {
        return "";
    }
}

bool SpinnakerDriver::setNodeValue(Spinnaker::GenApi::INode* node, const std::string& value) {
    if (!node) return false;

    try {
        switch (node->GetPrincipalInterfaceType()) {
            case Spinnaker::GenApi::intfIInteger: {
                Spinnaker::GenApi::CIntegerPtr intNode = static_cast<Spinnaker::GenApi::CIntegerPtr>(node);
                int64_t val = std::stoll(value);
                val = std::clamp(val, intNode->GetMin(), intNode->GetMax());
                intNode->SetValue(val);
                return true;
            }
            case Spinnaker::GenApi::intfIFloat: {
                Spinnaker::GenApi::CFloatPtr floatNode = static_cast<Spinnaker::GenApi::CFloatPtr>(node);
                double val = std::stod(value);
                val = std::clamp(val, floatNode->GetMin(), floatNode->GetMax());
                floatNode->SetValue(val);
                return true;
            }
            case Spinnaker::GenApi::intfIBoolean: {
                Spinnaker::GenApi::CBooleanPtr boolNode = static_cast<Spinnaker::GenApi::CBooleanPtr>(node);
                bool val = (value == "true" || value == "1" || value == "True");
                boolNode->SetValue(val);
                return true;
            }
            case Spinnaker::GenApi::intfIString: {
                Spinnaker::GenApi::CStringPtr strNode = static_cast<Spinnaker::GenApi::CStringPtr>(node);
                strNode->SetValue(value.c_str());
                return true;
            }
            case Spinnaker::GenApi::intfIEnumeration: {
                Spinnaker::GenApi::CEnumerationPtr enumNode = static_cast<Spinnaker::GenApi::CEnumerationPtr>(node);
                Spinnaker::GenApi::CEnumEntryPtr entry = enumNode->GetEntryByName(value.c_str());
                if (Spinnaker::GenApi::IsAvailable(entry) && Spinnaker::GenApi::IsReadable(entry)) {
                    enumNode->SetIntValue(entry->GetValue());
                    return true;
                }
                return false;
            }
            case Spinnaker::GenApi::intfICommand: {
                Spinnaker::GenApi::CCommandPtr cmdNode = static_cast<Spinnaker::GenApi::CCommandPtr>(node);
                cmdNode->Execute();
                return true;
            }
            default:
                return false;
        }
    } catch (Spinnaker::Exception& e) {
        spdlog::warn("Failed to set node value: {}", e.what());
        return false;
    }
}

SpinnakerNode SpinnakerDriver::nodeToStruct(Spinnaker::GenApi::INode* node) {
    SpinnakerNode result;

    if (!node) return result;

    result.name = node->GetName().c_str();
    result.display_name = node->GetDisplayName().c_str();
    result.description = node->GetDescription().c_str();
    result.access_mode = getAccessModeString(node->GetAccessMode());
    result.is_readable = Spinnaker::GenApi::IsReadable(node);
    result.is_writable = Spinnaker::GenApi::IsWritable(node);

    // Get interface type and type-specific info
    switch (node->GetPrincipalInterfaceType()) {
        case Spinnaker::GenApi::intfIInteger: {
            result.interface_type = "integer";
            Spinnaker::GenApi::CIntegerPtr intNode = static_cast<Spinnaker::GenApi::CIntegerPtr>(node);
            if (result.is_readable) {
                result.value = std::to_string(intNode->GetValue());
                result.min_value = std::to_string(intNode->GetMin());
                result.max_value = std::to_string(intNode->GetMax());
                result.increment = std::to_string(intNode->GetInc());
            }
            break;
        }
        case Spinnaker::GenApi::intfIFloat: {
            result.interface_type = "float";
            Spinnaker::GenApi::CFloatPtr floatNode = static_cast<Spinnaker::GenApi::CFloatPtr>(node);
            if (result.is_readable) {
                result.value = std::to_string(floatNode->GetValue());
                result.min_value = std::to_string(floatNode->GetMin());
                result.max_value = std::to_string(floatNode->GetMax());
            }
            break;
        }
        case Spinnaker::GenApi::intfIBoolean: {
            result.interface_type = "boolean";
            Spinnaker::GenApi::CBooleanPtr boolNode = static_cast<Spinnaker::GenApi::CBooleanPtr>(node);
            if (result.is_readable) {
                result.value = boolNode->GetValue() ? "true" : "false";
            }
            break;
        }
        case Spinnaker::GenApi::intfIString: {
            result.interface_type = "string";
            Spinnaker::GenApi::CStringPtr strNode = static_cast<Spinnaker::GenApi::CStringPtr>(node);
            if (result.is_readable) {
                result.value = strNode->GetValue().c_str();
            }
            break;
        }
        case Spinnaker::GenApi::intfIEnumeration: {
            result.interface_type = "enumeration";
            Spinnaker::GenApi::CEnumerationPtr enumNode = static_cast<Spinnaker::GenApi::CEnumerationPtr>(node);
            if (result.is_readable) {
                Spinnaker::GenApi::CEnumEntryPtr entry = enumNode->GetCurrentEntry();
                if (entry) {
                    result.value = entry->GetSymbolic().c_str();
                }

                // Get all available choices
                Spinnaker::GenApi::NodeList_t entries;
                enumNode->GetEntries(entries);
                for (auto& e : entries) {
                    Spinnaker::GenApi::CEnumEntryPtr enumEntry = static_cast<Spinnaker::GenApi::CEnumEntryPtr>(e);
                    if (Spinnaker::GenApi::IsAvailable(enumEntry) && Spinnaker::GenApi::IsReadable(enumEntry)) {
                        result.choices.push_back(enumEntry->GetSymbolic().c_str());
                    }
                }
            }
            break;
        }
        case Spinnaker::GenApi::intfICommand: {
            result.interface_type = "command";
            result.value = "";
            break;
        }
        default:
            result.interface_type = "unknown";
            break;
    }

    return result;
}

std::pair<std::vector<SpinnakerNode>, std::string> SpinnakerDriver::getNodeMap(const std::string& identifier) {
    std::vector<SpinnakerNode> nodes;

    if (!initialized_) {
        return {nodes, "Spinnaker SDK not initialized"};
    }

    std::lock_guard<std::mutex> lock(systemMutex_);

    try {
        Spinnaker::CameraList camList = system_->GetCameras();
        Spinnaker::CameraPtr cam = nullptr;

        // Find camera by serial
        for (unsigned int i = 0; i < camList.GetSize(); i++) {
            Spinnaker::CameraPtr c = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap& nodeMapTL = c->GetTLDeviceNodeMap();

            Spinnaker::GenApi::CStringPtr serialPtr = nodeMapTL.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsAvailable(serialPtr) && Spinnaker::GenApi::IsReadable(serialPtr)) {
                if (std::string(serialPtr->GetValue().c_str()) == identifier) {
                    cam = c;
                    break;
                }
            }
        }

        if (!cam) {
            camList.Clear();
            return {nodes, "Camera not found: " + identifier};
        }

        // Initialize camera to access node map
        cam->Init();
        Spinnaker::GenApi::INodeMap& nodeMap = cam->GetNodeMap();

        // Get all nodes
        Spinnaker::GenApi::NodeList_t nodeList;
        nodeMap.GetNodes(nodeList);

        for (auto& node : nodeList) {
            // Skip nodes that aren't available or are categories
            if (!Spinnaker::GenApi::IsAvailable(node)) continue;
            if (node->GetPrincipalInterfaceType() == Spinnaker::GenApi::intfICategory) continue;

            // Only include readable or writable nodes
            if (!Spinnaker::GenApi::IsReadable(node) && !Spinnaker::GenApi::IsWritable(node)) continue;

            SpinnakerNode spinNode = nodeToStruct(node);
            if (!spinNode.name.empty() && spinNode.interface_type != "unknown") {
                nodes.push_back(spinNode);
            }
        }

        cam->DeInit();
        camList.Clear();

        // Sort nodes by name
        std::sort(nodes.begin(), nodes.end(), [](const SpinnakerNode& a, const SpinnakerNode& b) {
            return a.name < b.name;
        });

        return {nodes, ""};

    } catch (Spinnaker::Exception& e) {
        return {nodes, std::string("Failed to retrieve node map: ") + e.what()};
    }
}

std::tuple<bool, std::string, int, nlohmann::json> SpinnakerDriver::updateNode(
    const std::string& identifier,
    const std::string& nodeName,
    const std::string& value
) {
    if (!initialized_) {
        return {false, "Spinnaker SDK not initialized", 500, nullptr};
    }

    if (nodeName.empty()) {
        return {false, "Node name is required", 400, nullptr};
    }

    std::lock_guard<std::mutex> lock(systemMutex_);

    try {
        Spinnaker::CameraList camList = system_->GetCameras();
        Spinnaker::CameraPtr cam = nullptr;

        // Find camera by serial
        for (unsigned int i = 0; i < camList.GetSize(); i++) {
            Spinnaker::CameraPtr c = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap& nodeMapTL = c->GetTLDeviceNodeMap();

            Spinnaker::GenApi::CStringPtr serialPtr = nodeMapTL.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsAvailable(serialPtr) && Spinnaker::GenApi::IsReadable(serialPtr)) {
                if (std::string(serialPtr->GetValue().c_str()) == identifier) {
                    cam = c;
                    break;
                }
            }
        }

        if (!cam) {
            camList.Clear();
            return {false, "Camera not found: " + identifier, 404, nullptr};
        }

        // Initialize camera
        cam->Init();
        Spinnaker::GenApi::INodeMap& nodeMap = cam->GetNodeMap();

        // Get the node
        Spinnaker::GenApi::INode* node = nodeMap.GetNode(nodeName.c_str());
        if (!node) {
            cam->DeInit();
            camList.Clear();
            return {false, "Node not found: " + nodeName, 404, nullptr};
        }

        // Check if writable
        if (!Spinnaker::GenApi::IsWritable(node)) {
            cam->DeInit();
            camList.Clear();
            return {false, "Node is not writable: " + nodeName, 403, nullptr};
        }

        // Set the value
        if (!setNodeValue(node, value)) {
            cam->DeInit();
            camList.Clear();
            return {false, "Failed to set node value", 400, nullptr};
        }

        // Read back the node state
        SpinnakerNode updatedNode = nodeToStruct(node);
        nlohmann::json nodeJson = updatedNode.toJson();

        cam->DeInit();
        camList.Clear();

        return {true, "Node updated successfully", 200, nodeJson};

    } catch (Spinnaker::Exception& e) {
        return {false, std::string("Spinnaker error: ") + e.what(), 500, nullptr};
    }
}

#else // !VISION_WITH_SPINNAKER

// ============================================================================
// STUB IMPLEMENTATIONS (when Spinnaker is not available)
// ============================================================================

bool SpinnakerDriver::connect() {
    spdlog::error("Spinnaker support not compiled in. Rebuild with --spinnaker=y");
    return false;
}

void SpinnakerDriver::disconnect() {
    connected_ = false;
}

bool SpinnakerDriver::isConnected() const {
    return false;
}

FrameResult SpinnakerDriver::getFrame() {
    return FrameResult{};
}

void SpinnakerDriver::setExposure(ExposureMode, int) {}
void SpinnakerDriver::setGain(GainMode, int) {}
int SpinnakerDriver::getExposure() const { return 0; }
int SpinnakerDriver::getGain() const { return 0; }
BaseDriver::Range SpinnakerDriver::getExposureRange() const { return {0, 10000, 1, 500}; }
BaseDriver::Range SpinnakerDriver::getGainRange() const { return {0, 100, 1, 0}; }

std::vector<DeviceInfo> SpinnakerDriver::listDevices() {
    spdlog::warn("Spinnaker support not compiled in");
    return {};
}

std::vector<CameraProfile> SpinnakerDriver::getSupportedProfiles(const std::string&) {
    return {};
}

std::pair<std::vector<SpinnakerNode>, std::string> SpinnakerDriver::getNodeMap(const std::string&) {
    return {{}, "Spinnaker support not compiled in"};
}

std::tuple<bool, std::string, int, nlohmann::json> SpinnakerDriver::updateNode(
    const std::string&,
    const std::string&,
    const std::string&
) {
    return {false, "Spinnaker support not compiled in", 500, nullptr};
}

#endif // VISION_WITH_SPINNAKER

} // namespace vision
