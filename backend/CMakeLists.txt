cmake_minimum_required(VERSION 3.24)
project(2852Vision C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan setup
if(EXISTS "${CMAKE_BINARY_DIR}/conan_toolchain.cmake")
    include("${CMAKE_BINARY_DIR}/conan_toolchain.cmake")
endif()

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find dependencies
find_package(drogon REQUIRED)
find_package(OpenCV REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(realsense2 REQUIRED)
find_package(onnxruntime REQUIRED)
find_package(fmt REQUIRED)

add_compile_definitions(NOMINMAX)

# Third-party libraries
add_subdirectory(third_party)

# Backend executable
file(GLOB_RECURSE BACKEND_SOURCES "src/*.cpp")
add_executable(backend ${BACKEND_SOURCES})

target_include_directories(backend PRIVATE 
    "src"
    "third_party/cpp-mjpeg-streamer/include"
)

# Link dependencies
target_link_libraries(backend PRIVATE
    Drogon::Drogon
    opencv::opencv
    nlohmann_json::nlohmann_json
    SQLiteCpp
    spdlog::spdlog
    Eigen3::Eigen
    realsense2::realsense2
    onnxruntime::onnxruntime
    apriltag
    ntcore
    wpiutil
    wpinet
)

if(WIN32)
    target_compile_definitions(backend PRIVATE _WIN32_WINNT=0x0601)
    # Link system libraries
    target_link_libraries(backend PRIVATE shell32 ole32 strmiids delayimp)
else()
    target_link_libraries(backend PRIVATE pthread dl)
endif()

# Spinnaker SDK Support
find_package(Spinnaker)
if(Spinnaker_FOUND)
    target_compile_definitions(backend PRIVATE VISION_WITH_SPINNAKER)
    target_include_directories(backend PRIVATE ${SPINNAKER_INCLUDE_DIRS})
    target_link_directories(backend PRIVATE ${SPINNAKER_LIBRARY_DIRS})
    target_link_libraries(backend PRIVATE ${SPINNAKER_LIBRARIES})
    
    if(WIN32)
        # Delay load Spinnaker DLL
        target_link_options(backend PRIVATE "/DELAYLOAD:Spinnaker_v140.dll")
    endif()
else()
    message(STATUS "Spinnaker SDK not found. Building without Spinnaker support.")
endif()

# Post-build actions
if(WIN32)
    add_custom_command(TARGET backend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:backend>/data"
        COMMENT "Copying data directory..."
    )

    add_custom_command(TARGET backend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ntcore>
        $<TARGET_FILE:wpinet>
        $<TARGET_FILE:wpiutil>
        "$<TARGET_FILE_DIR:backend>"
        COMMENT "Copying WPILib DLLs..."
    )
    
endif()
