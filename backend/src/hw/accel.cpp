#include "hw/accel.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ONNX Runtime
#include <onnxruntime_cxx_api.h>

namespace vision {
namespace hw {

bool isMacOS() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

bool isWindows() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool isLinux() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool hasNvidiaGPU() {
    // Check for nvidia-smi utility
#ifdef _WIN32
    // Try to run nvidia-smi
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    char cmd[] = "nvidia-smi --help";
    if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
#else
    // Check if nvidia-smi exists
    return system("nvidia-smi --help > /dev/null 2>&1") == 0;
#endif
}

bool isOrangePi5() {
    // Check environment variable override
    const char* forceOpi5 = std::getenv("VISIONTOOLS_FORCE_OPI5");
    if (forceOpi5 && std::string(forceOpi5) == "1") {
        return true;
    }

#ifdef __linux__
    // Check device tree model for RK3588/Orange Pi 5
    std::vector<std::string> modelPaths = {
        "/proc/device-tree/model",
        "/sys/firmware/devicetree/base/model"
    };

    for (const auto& path : modelPaths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string model;
            std::getline(file, model);

            // Convert to lowercase for comparison
            std::transform(model.begin(), model.end(), model.begin(), ::tolower);

            if (model.find("orange pi 5") != std::string::npos ||
                model.find("rk3588") != std::string::npos) {
                return true;
            }
        }
    }
#endif

    return false;
}

std::vector<std::string> getAvailableOnnxProviders() {
    std::vector<std::string> providers;

    try {
        // Get available providers from ONNX Runtime
        auto availableProviders = Ort::GetAvailableProviders();

        for (const auto& provider : availableProviders) {
            // Filter providers based on hardware availability
            if (provider == "CoreMLExecutionProvider" && !isMacOS()) {
                continue;
            }

            if (provider == "CUDAExecutionProvider" || provider == "TensorrtExecutionProvider") {
                if (!hasNvidiaGPU()) {
                    continue;
                }
            }

            providers.push_back(provider);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Error getting ONNX providers: {}", e.what());
    }

    // CPU provider should always be available
    if (std::find(providers.begin(), providers.end(), "CPUExecutionProvider") == providers.end()) {
        providers.push_back("CPUExecutionProvider");
    }

    return providers;
}

std::vector<std::string> getAvailableTfLiteDelegates() {
    std::vector<std::string> delegates;

    // TFLite C++ integration is complex and requires separate library
    // For now, return empty - can be implemented when TFLite is added
    // delegates.push_back("CPU");

    return delegates;
}

bool hasRknnSupport() {
    // RKNN support requires Orange Pi 5 and the RKNN toolkit
    if (!isOrangePi5()) {
        return false;
    }

#ifdef __linux__
    // Check if RKNN library exists
    std::vector<std::string> rknnPaths = {
        "/usr/lib/librknnrt.so",
        "/usr/local/lib/librknnrt.so"
    };

    for (const auto& path : rknnPaths) {
        std::ifstream file(path);
        if (file.good()) {
            return true;
        }
    }
#endif

    return false;
}

nlohmann::json getMLAvailability() {
    auto onnxProviders = getAvailableOnnxProviders();
    auto tfliteDelegates = getAvailableTfLiteDelegates();
    bool orangePi5 = isOrangePi5();
    bool rknnSupported = hasRknnSupport();

    nlohmann::json result = {
        {"platform", {
            {"is_macos", isMacOS()},
            {"is_windows", isWindows()},
            {"is_linux", isLinux()},
            {"has_nvidia", hasNvidiaGPU()},
            {"is_orangepi5", orangePi5}
        }},
        {"onnx", {
            {"providers", onnxProviders}
        }},
        {"tflite", {
            {"delegates", tfliteDelegates}
        }},
        {"accelerators", {
            {"rknn", rknnSupported}
        }}
    };

    return result;
}

} // namespace hw
} // namespace vision
