# Third-party dependencies configuration

# Apriltag (Static Library)
file(GLOB APRILTAG_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/apriltag/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/apriltag/common/*.c"
)
list(FILTER APRILTAG_SOURCES EXCLUDE REGEX ".*apriltag_pywrap.c")

add_library(apriltag STATIC ${APRILTAG_SOURCES})
target_include_directories(apriltag PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/apriltag"
    "${CMAKE_CURRENT_SOURCE_DIR}/apriltag/common"
)
target_compile_definitions(apriltag PRIVATE _GNU_SOURCE)

if(WIN32)
    target_compile_definitions(apriltag PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif()

# Allwpilib (NetworkTables)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/allwpilib/CMakeLists.txt")
    set(WITH_JAVA OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_SIMULATION_MODULES OFF CACHE BOOL "" FORCE)
    set(WITH_PROTOBUF OFF CACHE BOOL "" FORCE)
    set(WITH_WPILIB OFF CACHE BOOL "" FORCE)
    set(WITH_GUI OFF CACHE BOOL "" FORCE)
    set(WITH_CSCORE OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
    
    set(USE_SYSTEM_FMTLIB ON CACHE BOOL "" FORCE)
    
    add_subdirectory(allwpilib)
else()
    message(WARNING "allwpilib submodule not found in third_party/allwpilib")
endif()

# OpenPnP-Capture (Manual Build)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/common/context.cpp")
    set(OPENPNP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/common/libmain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/common/context.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/common/logging.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/common/stream.cpp"
    )

    if(WIN32)
        list(APPEND OPENPNP_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/win/platformcontext.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/win/platformstream.cpp"
        )
    elseif(APPLE)
        list(APPEND OPENPNP_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/mac/platformcontext.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/mac/platformstream.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/mac/uvcctrl.mm"
        )
    else()
        list(APPEND OPENPNP_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/linux/platformcontext.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/linux/platformstream.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/linux/mjpeghelper.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/linux/yuvconverters.cpp"
        )
    endif()

    add_library(openpnp-capture STATIC ${OPENPNP_SOURCES})
    target_include_directories(openpnp-capture PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/openpnp-capture/include"
    )
    target_compile_definitions(openpnp-capture PUBLIC OPENPNPCAPTURE_STATIC)

    if(WIN32)
        target_compile_definitions(openpnp-capture PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(openpnp-capture PRIVATE strmiids)
    elseif(APPLE)
        target_link_libraries(openpnp-capture PRIVATE
            "-framework AVFoundation"
            "-framework Foundation"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework Accelerate"
            "-framework IOKit"
        )
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(openpnp-capture PRIVATE Threads::Threads)
        # Note: Skipping libjpeg-turbo for now as it requires pkg-config or more complex setup.
        # If needed, we can add it later.
    endif()

else()
    message(WARNING "openpnp-capture sources not found")
endif()
