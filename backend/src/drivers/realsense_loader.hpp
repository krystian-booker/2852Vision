#pragma once

#include <string>

namespace vision {

/**
 * RealSenseLoader - Handles runtime detection and loading of the RealSense SDK
 *
 * This class checks if the RealSense SDK DLLs are available at runtime before
 * attempting to use any RealSense functionality. This allows the application
 * to gracefully degrade when the SDK is not installed, rather than crashing
 * on startup due to missing DLLs.
 */
class RealSenseLoader {
public:
    /**
     * Attempts to load the RealSense SDK.
     *
     * @return true if the SDK was successfully loaded, false otherwise
     */
    static bool tryLoad();

    /**
     * Checks if the RealSense SDK is loaded and available.
     *
     * @return true if the SDK is available for use
     */
    static bool isLoaded();

    /**
     * Unloads the RealSense SDK if it was dynamically loaded.
     */
    static void unload();

    /**
     * Gets an error message describing why loading failed.
     *
     * @return Error message, or empty string if no error
     */
    static std::string getLoadError();

private:
    static bool loaded_;
    static std::string loadError_;

#ifdef _WIN32
    static void* realsenseHandle_;
#else
    static void* realsenseHandle_;
#endif

    // Check if the required RealSense DLLs/SOs exist on the system
    static bool checkDllsExist();

    // Actually load the library
    static bool loadLibrary();
};

} // namespace vision
