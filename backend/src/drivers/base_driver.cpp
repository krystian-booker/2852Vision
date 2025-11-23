#include "drivers/base_driver.hpp"
#include "drivers/usb_driver.hpp"
#include "drivers/realsense_driver.hpp"
#include "drivers/spinnaker_driver.hpp"
#include <spdlog/spdlog.h>

namespace vision {

std::unique_ptr<BaseDriver> BaseDriver::create(const Camera& camera) {
    switch (camera.camera_type) {
        case CameraType::USB:
            return std::make_unique<USBDriver>(camera);

        case CameraType::Spinnaker:
            if (SpinnakerDriver::isAvailable()) {
                return std::make_unique<SpinnakerDriver>(camera);
            } else {
                spdlog::error("Spinnaker support not compiled in. Rebuild with --spinnaker=y");
                return nullptr;
            }

        case CameraType::RealSense:
            if (RealSenseDriver::isAvailable()) {
                return std::make_unique<RealSenseDriver>(camera);
            } else {
                spdlog::error("RealSense support not compiled in. Rebuild with --realsense=y");
                return nullptr;
            }

        default:
            spdlog::error("Unknown camera type");
            return nullptr;
    }
}

} // namespace vision
