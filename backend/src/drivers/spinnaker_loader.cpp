#include "drivers/spinnaker_loader.hpp"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace vision {

// Static member initialization
bool SpinnakerLoader::loaded_ = false;
std::string SpinnakerLoader::loadError_;
void* SpinnakerLoader::spinnakerHandle_ = nullptr;

bool SpinnakerLoader::tryLoad() {
    if (loaded_) {
        return true;
    }

    // First check if the required DLLs/shared libraries exist
    if (!checkDllsExist()) {
        spdlog::warn("Spinnaker SDK not found on this system. FLIR/Point Grey cameras will not be available.");
        return false;
    }

    // Try to load the library
    if (!loadLibrary()) {
        spdlog::warn("Failed to load Spinnaker SDK: {}. FLIR/Point Grey cameras will not be available.",
                     loadError_);
        return false;
    }

    loaded_ = true;
    spdlog::debug("Spinnaker SDK libraries loaded successfully");
    return true;
}

bool SpinnakerLoader::isLoaded() {
    return loaded_;
}

void SpinnakerLoader::unload() {
    if (!loaded_) {
        return;
    }

#ifdef _WIN32
    if (spinnakerHandle_) {
        FreeLibrary(static_cast<HMODULE>(spinnakerHandle_));
        spinnakerHandle_ = nullptr;
    }
#else
    if (spinnakerHandle_) {
        dlclose(spinnakerHandle_);
        spinnakerHandle_ = nullptr;
    }
#endif

    loaded_ = false;
}

std::string SpinnakerLoader::getLoadError() {
    return loadError_;
}

bool SpinnakerLoader::checkDllsExist() {
#ifdef _WIN32
    // Windows: Check for Spinnaker DLL in system paths
    // The DLL could be in:
    // 1. System PATH
    // 2. Same directory as executable
    // 3. Spinnaker SDK bin directory

    // Check common Spinnaker installation paths
    std::vector<std::string> searchPaths = {
        "C:/Program Files/Teledyne/Spinnaker/bin64/vs2015/Spinnaker_v140.dll",
        "C:/Program Files/FLIR Systems/Spinnaker/bin64/vs2015/Spinnaker_v140.dll",
        "C:/Program Files/Point Grey Research/Spinnaker/bin64/vs2015/Spinnaker_v140.dll"
    };

    for (const auto& dllPath : searchPaths) {
        if (std::filesystem::exists(dllPath)) {
            spdlog::debug("Found Spinnaker DLL at: {}", dllPath);
            return true;
        }
    }

    // Also try to find it in the system PATH using SearchPath
    char foundPath[MAX_PATH];
    DWORD result = SearchPathA(nullptr, "Spinnaker_v140.dll", nullptr, MAX_PATH, foundPath, nullptr);
    if (result > 0) {
        spdlog::debug("Found Spinnaker DLL in PATH: {}", foundPath);
        return true;
    }

    loadError_ = "Spinnaker_v140.dll not found. Please install the Spinnaker SDK from https://www.flir.com/products/spinnaker-sdk/";
    return false;

#elif defined(__linux__)
    // Linux: Check for libSpinnaker.so
    std::vector<std::string> searchPaths = {
        "/opt/spinnaker/lib/libSpinnaker.so",
        "/usr/lib/libSpinnaker.so",
        "/usr/local/lib/libSpinnaker.so"
    };

    struct stat buffer;
    for (const auto& soPath : searchPaths) {
        if (stat(soPath.c_str(), &buffer) == 0) {
            spdlog::debug("Found Spinnaker SO at: {}", soPath);
            return true;
        }
    }

    loadError_ = "libSpinnaker.so not found. Please install the Spinnaker SDK from https://www.flir.com/products/spinnaker-sdk/";
    return false;

#elif defined(__APPLE__)
    // macOS: Check for libSpinnaker.dylib or framework
    std::vector<std::string> searchPaths = {
        "/usr/local/lib/libSpinnaker.dylib",
        "/Library/Frameworks/Spinnaker.framework/Spinnaker"
    };

    struct stat buffer;
    for (const auto& dylibPath : searchPaths) {
        if (stat(dylibPath.c_str(), &buffer) == 0) {
            spdlog::debug("Found Spinnaker dylib at: {}", dylibPath);
            return true;
        }
    }

    loadError_ = "libSpinnaker.dylib not found. Please install the Spinnaker SDK from https://www.flir.com/products/spinnaker-sdk/";
    return false;

#else
    loadError_ = "Unsupported platform for Spinnaker SDK";
    return false;
#endif
}

bool SpinnakerLoader::loadLibrary() {
#ifdef _WIN32
    // On Windows, we use delay-loaded DLLs, so we just verify the DLL can be found
    // The actual loading happens when Spinnaker functions are first called

    // Try to load the DLL to verify it's accessible
    HMODULE handle = LoadLibraryA("Spinnaker_v140.dll");
    if (!handle) {
        // Try the full path
        handle = LoadLibraryA("C:/Program Files/Teledyne/Spinnaker/bin64/vs2015/Spinnaker_v140.dll");
    }

    if (!handle) {
        DWORD error = GetLastError();
        char errorMsg[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, errorMsg, sizeof(errorMsg), nullptr);
        loadError_ = std::string("LoadLibrary failed: ") + errorMsg;
        return false;
    }

    spinnakerHandle_ = handle;
    return true;

#elif defined(__linux__)
    // Linux: Load libSpinnaker.so
    void* handle = dlopen("libSpinnaker.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        // Try with full path
        handle = dlopen("/opt/spinnaker/lib/libSpinnaker.so", RTLD_NOW | RTLD_GLOBAL);
    }

    if (!handle) {
        loadError_ = std::string("dlopen failed: ") + dlerror();
        return false;
    }

    spinnakerHandle_ = handle;
    return true;

#elif defined(__APPLE__)
    // macOS: Load libSpinnaker.dylib
    void* handle = dlopen("libSpinnaker.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        // Try with full path
        handle = dlopen("/usr/local/lib/libSpinnaker.dylib", RTLD_NOW | RTLD_GLOBAL);
    }

    if (!handle) {
        loadError_ = std::string("dlopen failed: ") + dlerror();
        return false;
    }

    spinnakerHandle_ = handle;
    return true;

#else
    loadError_ = "Unsupported platform";
    return false;
#endif
}

} // namespace vision
