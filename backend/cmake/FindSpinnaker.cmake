# FindSpinnaker.cmake
# Detects the Spinnaker SDK
#
# Sets the following variables:
# SPINNAKER_FOUND - True if Spinnaker SDK is found
# SPINNAKER_INCLUDE_DIRS - Include directories for Spinnaker
# SPINNAKER_LIBRARIES - Libraries to link against
# SPINNAKER_LIBRARY_DIRS - Library directories

function(detect_spinnaker_sdk)
    if(WIN32)
        set(SPINNAKER_ROOT_CANDIDATE "C:/Program Files/Teledyne/Spinnaker")
        if(EXISTS "${SPINNAKER_ROOT_CANDIDATE}/include" AND EXISTS "${SPINNAKER_ROOT_CANDIDATE}/lib64/vs2015/Spinnaker_v140.lib")
            set(SPINNAKER_INCLUDE_DIR "${SPINNAKER_ROOT_CANDIDATE}/include" PARENT_SCOPE)
            set(SPINNAKER_LIB_DIR "${SPINNAKER_ROOT_CANDIDATE}/lib64/vs2015" PARENT_SCOPE)
            set(SPINNAKER_LIBS "Spinnaker_v140" PARENT_SCOPE)
            set(SPINNAKER_FOUND TRUE PARENT_SCOPE)
            message(STATUS "Found Spinnaker SDK at ${SPINNAKER_ROOT_CANDIDATE}")
            return()
        endif()
    elseif(APPLE)
        # macOS Framework
        if(EXISTS "/Library/Frameworks/Spinnaker.framework")
            set(SPINNAKER_INCLUDE_DIR "/Library/Frameworks/Spinnaker.framework/Headers" PARENT_SCOPE)
            set(SPINNAKER_LIB_DIR "/Library/Frameworks/Spinnaker.framework/Libraries" PARENT_SCOPE)
            set(SPINNAKER_LIBS "Spinnaker" PARENT_SCOPE)
            set(SPINNAKER_FOUND TRUE PARENT_SCOPE)
            message(STATUS "Found Spinnaker Framework at /Library/Frameworks/Spinnaker.framework")
            return()
        endif()
        # macOS Standard
        if(EXISTS "/usr/local/include/spinnaker" AND EXISTS "/usr/local/lib/libSpinnaker.dylib")
            set(SPINNAKER_INCLUDE_DIR "/usr/local/include/spinnaker" PARENT_SCOPE)
            set(SPINNAKER_LIB_DIR "/usr/local/lib" PARENT_SCOPE)
            set(SPINNAKER_LIBS "Spinnaker" PARENT_SCOPE)
            set(SPINNAKER_FOUND TRUE PARENT_SCOPE)
            message(STATUS "Found Spinnaker SDK at /usr/local")
            return()
        endif()
    else()
        # Linux
        if(EXISTS "/opt/spinnaker/include" AND EXISTS "/opt/spinnaker/lib/libSpinnaker.so")
            set(SPINNAKER_INCLUDE_DIR "/opt/spinnaker/include" PARENT_SCOPE)
            set(SPINNAKER_LIB_DIR "/opt/spinnaker/lib" PARENT_SCOPE)
            set(SPINNAKER_LIBS "Spinnaker;SpinVideo" PARENT_SCOPE)
            set(SPINNAKER_FOUND TRUE PARENT_SCOPE)
            message(STATUS "Found Spinnaker SDK at /opt/spinnaker")
            return()
        endif()
    endif()

    set(SPINNAKER_FOUND FALSE PARENT_SCOPE)
endfunction()

detect_spinnaker_sdk()

if(SPINNAKER_FOUND)
    set(SPINNAKER_INCLUDE_DIRS ${SPINNAKER_INCLUDE_DIR})
    set(SPINNAKER_LIBRARIES ${SPINNAKER_LIBS})
    set(SPINNAKER_LIBRARY_DIRS ${SPINNAKER_LIB_DIR})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Spinnaker DEFAULT_MSG SPINNAKER_INCLUDE_DIRS SPINNAKER_LIBRARIES)

mark_as_advanced(SPINNAKER_INCLUDE_DIRS SPINNAKER_LIBRARIES SPINNAKER_LIBRARY_DIRS)
