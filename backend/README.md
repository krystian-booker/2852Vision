# 2852Vision Backend

C++20 vision processing backend for FRC. Provides camera acquisition, AprilTag detection, and NetworkTables integration.

## Prerequisites

- CMake 3.24+
- Conan 2.x
- C++20 compiler (Visual Studio 2022 on Windows, GCC 11+ on Linux)

### Optional SDKs

- Intel RealSense SDK 2.x (for RealSense cameras)
- FLIR Spinnaker SDK (for FLIR cameras)

## Build Instructions

### Debug Build

```bash
# Install dependencies
conan install . --output-folder=build --build=missing -s compiler.cppstd=20 -s build_type=Debug

# Configure
cmake --preset conan-default

# Build
cmake --build build/build --config Debug
```

### Release Build

```bash
# Install dependencies
conan install . --output-folder=build --build=missing -s compiler.cppstd=20 -s build_type=Release

# Configure
cmake --preset conan-default

# Build
cmake --build build/build --config Release
```

## Run

```bash
# Windows
./build/build/Debug/backend.exe    # Debug
./build/build/Release/backend.exe  # Release

# Linux
./build/build/backend
```

## Ports

| Port | Service |
|------|---------|
| 8080 | REST API |
| 5805 | MJPEG streaming |
| 1735 | NetworkTables |
