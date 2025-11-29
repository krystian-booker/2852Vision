# FindRealSense.cmake
# Detects the Intel RealSense SDK (librealsense2)
#
# Sets the following variables:
# RealSense_FOUND - True if RealSense SDK is found
# REALSENSE_INCLUDE_DIRS - Include directories for RealSense
# REALSENSE_LIBRARIES - Libraries to link against
# REALSENSE_LIBRARY_DIRS - Library directories
# REALSENSE_DLL_DIR - Directory containing DLLs (Windows only)

function(detect_realsense_sdk)
    # First check environment variable
    if(DEFINED ENV{REALSENSE_SDK_DIR})
        set(REALSENSE_ROOT_CANDIDATE "$ENV{REALSENSE_SDK_DIR}")
        if(EXISTS "${REALSENSE_ROOT_CANDIDATE}/include/librealsense2/rs.hpp")
            set(REALSENSE_ROOT "${REALSENSE_ROOT_CANDIDATE}" PARENT_SCOPE)
            message(STATUS "Found RealSense SDK via REALSENSE_SDK_DIR: ${REALSENSE_ROOT_CANDIDATE}")
            return()
        endif()
    endif()

    # Check CMake variable
    if(DEFINED REALSENSE_SDK_DIR)
        set(REALSENSE_ROOT_CANDIDATE "${REALSENSE_SDK_DIR}")
        if(EXISTS "${REALSENSE_ROOT_CANDIDATE}/include/librealsense2/rs.hpp")
            set(REALSENSE_ROOT "${REALSENSE_ROOT_CANDIDATE}" PARENT_SCOPE)
            message(STATUS "Found RealSense SDK via CMake variable: ${REALSENSE_ROOT_CANDIDATE}")
            return()
        endif()
    endif()

    if(WIN32)
        # Windows: Check common installation paths
        set(SEARCH_PATHS
            "$ENV{USERPROFILE}/Documents/Intel RealSense SDK 2.0"
            "C:/Program Files (x86)/Intel RealSense SDK 2.0"
            "C:/Program Files/Intel RealSense SDK 2.0"
        )

        foreach(PATH ${SEARCH_PATHS})
            if(EXISTS "${PATH}/include/librealsense2/rs.hpp")
                set(REALSENSE_ROOT "${PATH}" PARENT_SCOPE)
                message(STATUS "Found RealSense SDK at ${PATH}")
                return()
            endif()
        endforeach()
    elseif(APPLE)
        # macOS: Check Homebrew paths
        set(SEARCH_PATHS
            "/opt/homebrew"
            "/usr/local"
        )

        foreach(PATH ${SEARCH_PATHS})
            if(EXISTS "${PATH}/include/librealsense2/rs.hpp")
                set(REALSENSE_ROOT "${PATH}" PARENT_SCOPE)
                message(STATUS "Found RealSense SDK at ${PATH}")
                return()
            endif()
        endforeach()
    else()
        # Linux: Check standard paths
        set(SEARCH_PATHS
            "/usr/local"
            "/usr"
            "/opt/librealsense"
        )

        foreach(PATH ${SEARCH_PATHS})
            if(EXISTS "${PATH}/include/librealsense2/rs.hpp")
                set(REALSENSE_ROOT "${PATH}" PARENT_SCOPE)
                message(STATUS "Found RealSense SDK at ${PATH}")
                return()
            endif()
        endforeach()
    endif()

    set(REALSENSE_ROOT "" PARENT_SCOPE)
endfunction()

detect_realsense_sdk()

if(REALSENSE_ROOT)
    set(REALSENSE_INCLUDE_DIR "${REALSENSE_ROOT}/include")

    if(WIN32)
        # Windows uses lib/x64/realsense2.lib and bin/x64/realsense2.dll
        if(EXISTS "${REALSENSE_ROOT}/lib/x64/realsense2.lib")
            set(REALSENSE_LIB_DIR "${REALSENSE_ROOT}/lib/x64")
            set(REALSENSE_LIBS "realsense2")
            set(REALSENSE_DLL_DIR "${REALSENSE_ROOT}/bin/x64")
            set(RealSense_FOUND TRUE)
        endif()
    elseif(APPLE)
        # macOS uses lib/librealsense2.dylib
        if(EXISTS "${REALSENSE_ROOT}/lib/librealsense2.dylib")
            set(REALSENSE_LIB_DIR "${REALSENSE_ROOT}/lib")
            set(REALSENSE_LIBS "realsense2")
            set(RealSense_FOUND TRUE)
        endif()
    else()
        # Linux uses lib/librealsense2.so
        if(EXISTS "${REALSENSE_ROOT}/lib/librealsense2.so")
            set(REALSENSE_LIB_DIR "${REALSENSE_ROOT}/lib")
            set(REALSENSE_LIBS "realsense2")
            set(RealSense_FOUND TRUE)
        # Also check lib/x86_64-linux-gnu for Ubuntu package layout
        elseif(EXISTS "${REALSENSE_ROOT}/lib/x86_64-linux-gnu/librealsense2.so")
            set(REALSENSE_LIB_DIR "${REALSENSE_ROOT}/lib/x86_64-linux-gnu")
            set(REALSENSE_LIBS "realsense2")
            set(RealSense_FOUND TRUE)
        endif()
    endif()
endif()

if(RealSense_FOUND)
    set(REALSENSE_INCLUDE_DIRS ${REALSENSE_INCLUDE_DIR})
    set(REALSENSE_LIBRARIES ${REALSENSE_LIBS})
    set(REALSENSE_LIBRARY_DIRS ${REALSENSE_LIB_DIR})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RealSense DEFAULT_MSG REALSENSE_INCLUDE_DIRS REALSENSE_LIBRARIES)

mark_as_advanced(REALSENSE_INCLUDE_DIRS REALSENSE_LIBRARIES REALSENSE_LIBRARY_DIRS REALSENSE_DLL_DIR)
