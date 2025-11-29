#include "drivers/realsense_loader.hpp"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <shlobj.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#endif

namespace vision {

// Static member initialization
bool RealSenseLoader::loaded_ = false;
std::string RealSenseLoader::loadError_;
void* RealSenseLoader::realsenseHandle_ = nullptr;

bool RealSenseLoader::tryLoad() {
    if (loaded_) {
        return true;
    }

    // First check if the required DLLs/shared libraries exist
    if (!checkDllsExist()) {
        spdlog::warn("RealSense SDK not found on this system. Intel RealSense cameras will not be available.");
        return false;
    }

    // Try to load the library
    if (!loadLibrary()) {
        spdlog::warn("Failed to load RealSense SDK: {}. Intel RealSense cameras will not be available.",
                     loadError_);
        return false;
    }

    loaded_ = true;
    spdlog::debug("RealSense SDK libraries loaded successfully");
    return true;
}

bool RealSenseLoader::isLoaded() {
    return loaded_;
}

void RealSenseLoader::unload() {
    if (!loaded_) {
        return;
    }

#ifdef _WIN32
    if (realsenseHandle_) {
        FreeLibrary(static_cast<HMODULE>(realsenseHandle_));
        realsenseHandle_ = nullptr;
    }
#else
    if (realsenseHandle_) {
        dlclose(realsenseHandle_);
        realsenseHandle_ = nullptr;
    }
#endif

    loaded_ = false;
}

std::string RealSenseLoader::getLoadError() {
    return loadError_;
}

bool RealSenseLoader::checkDllsExist() {
#ifdef _WIN32
    // Windows: Check for realsense2.dll in various locations
    std::vector<std::string> searchPaths;

    // 1. Check REALSENSE_SDK_DIR environment variable
    char* sdkDir = nullptr;
    size_t len = 0;
    if (_dupenv_s(&sdkDir, &len, "REALSENSE_SDK_DIR") == 0 && sdkDir != nullptr) {
        std::string envPath = std::string(sdkDir) + "/bin/x64/realsense2.dll";
        searchPaths.push_back(envPath);
        free(sdkDir);
    }

    // 2. Check user's Documents folder (installer default)
    char userProfile[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH) > 0) {
        std::string docsPath = std::string(userProfile) + "/Documents/Intel RealSense SDK 2.0/bin/x64/realsense2.dll";
        searchPaths.push_back(docsPath);
    }

    // 3. Check Program Files
    searchPaths.push_back("C:/Program Files (x86)/Intel RealSense SDK 2.0/bin/x64/realsense2.dll");
    searchPaths.push_back("C:/Program Files/Intel RealSense SDK 2.0/bin/x64/realsense2.dll");

    for (const auto& dllPath : searchPaths) {
        if (std::filesystem::exists(dllPath)) {
            spdlog::debug("Found RealSense DLL at: {}", dllPath);
            return true;
        }
    }

    // Also try to find it in the system PATH using SearchPath
    char foundPath[MAX_PATH];
    DWORD result = SearchPathA(nullptr, "realsense2.dll", nullptr, MAX_PATH, foundPath, nullptr);
    if (result > 0) {
        spdlog::debug("Found RealSense DLL in PATH: {}", foundPath);
        return true;
    }

    loadError_ = "realsense2.dll not found. Please install the Intel RealSense SDK from https://github.com/IntelRealSense/librealsense/releases";
    return false;

#elif defined(__linux__)
    // Linux: Check for librealsense2.so
    std::vector<std::string> searchPaths = {
        "/usr/local/lib/librealsense2.so",
        "/usr/lib/librealsense2.so",
        "/usr/lib/x86_64-linux-gnu/librealsense2.so",
        "/opt/librealsense/lib/librealsense2.so"
    };

    struct stat buffer;
    for (const auto& soPath : searchPaths) {
        if (stat(soPath.c_str(), &buffer) == 0) {
            spdlog::debug("Found RealSense SO at: {}", soPath);
            return true;
        }
    }

    loadError_ = "librealsense2.so not found. Please install librealsense2 from https://github.com/IntelRealSense/librealsense";
    return false;

#elif defined(__APPLE__)
    // macOS: Check for librealsense2.dylib
    std::vector<std::string> searchPaths = {
        "/usr/local/lib/librealsense2.dylib",
        "/opt/homebrew/lib/librealsense2.dylib"
    };

    struct stat buffer;
    for (const auto& dylibPath : searchPaths) {
        if (stat(dylibPath.c_str(), &buffer) == 0) {
            spdlog::debug("Found RealSense dylib at: {}", dylibPath);
            return true;
        }
    }

    loadError_ = "librealsense2.dylib not found. Please install librealsense2 via Homebrew: brew install librealsense";
    return false;

#else
    loadError_ = "Unsupported platform for RealSense SDK";
    return false;
#endif
}

bool RealSenseLoader::loadLibrary() {
#ifdef _WIN32
    // On Windows, we use delay-loaded DLLs, so we just verify the DLL can be found
    // The actual loading happens when RealSense functions are first called

    // Try to load the DLL to verify it's accessible
    HMODULE handle = LoadLibraryA("realsense2.dll");

    if (!handle) {
        // Try paths in order
        std::vector<std::string> tryPaths;

        // Check REALSENSE_SDK_DIR
        char* sdkDir = nullptr;
        size_t len = 0;
        if (_dupenv_s(&sdkDir, &len, "REALSENSE_SDK_DIR") == 0 && sdkDir != nullptr) {
            tryPaths.push_back(std::string(sdkDir) + "/bin/x64/realsense2.dll");
            free(sdkDir);
        }

        // Check user's Documents
        char userProfile[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH) > 0) {
            tryPaths.push_back(std::string(userProfile) + "/Documents/Intel RealSense SDK 2.0/bin/x64/realsense2.dll");
        }

        // Check Program Files
        tryPaths.push_back("C:/Program Files (x86)/Intel RealSense SDK 2.0/bin/x64/realsense2.dll");

        for (const auto& path : tryPaths) {
            handle = LoadLibraryA(path.c_str());
            if (handle) {
                break;
            }
        }
    }

    if (!handle) {
        DWORD error = GetLastError();
        char errorMsg[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, errorMsg, sizeof(errorMsg), nullptr);
        loadError_ = std::string("LoadLibrary failed: ") + errorMsg;
        return false;
    }

    realsenseHandle_ = handle;
    return true;

#elif defined(__linux__)
    // Linux: Load librealsense2.so
    void* handle = dlopen("librealsense2.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        // Try with full paths
        std::vector<std::string> tryPaths = {
            "/usr/local/lib/librealsense2.so",
            "/usr/lib/librealsense2.so",
            "/usr/lib/x86_64-linux-gnu/librealsense2.so"
        };

        for (const auto& path : tryPaths) {
            handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                break;
            }
        }
    }

    if (!handle) {
        loadError_ = std::string("dlopen failed: ") + dlerror();
        return false;
    }

    realsenseHandle_ = handle;
    return true;

#elif defined(__APPLE__)
    // macOS: Load librealsense2.dylib
    void* handle = dlopen("librealsense2.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        // Try with full paths
        std::vector<std::string> tryPaths = {
            "/usr/local/lib/librealsense2.dylib",
            "/opt/homebrew/lib/librealsense2.dylib"
        };

        for (const auto& path : tryPaths) {
            handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                break;
            }
        }
    }

    if (!handle) {
        loadError_ = std::string("dlopen failed: ") + dlerror();
        return false;
    }

    realsenseHandle_ = handle;
    return true;

#else
    loadError_ = "Unsupported platform";
    return false;
#endif
}

} // namespace vision
