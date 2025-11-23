#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace vision {
namespace hw {

// Platform detection
bool isMacOS();
bool isWindows();
bool isLinux();
bool hasNvidiaGPU();
bool isOrangePi5();

// ONNX Runtime providers
std::vector<std::string> getAvailableOnnxProviders();

// TFLite delegates (stub for now, TFLite not integrated)
std::vector<std::string> getAvailableTfLiteDelegates();

// RKNN support
bool hasRknnSupport();

// Combined ML availability report
nlohmann::json getMLAvailability();

} // namespace hw
} // namespace vision
