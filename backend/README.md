# 2852Vision Backend

C++ backend for FRC robot vision processing with AprilTag detection, pose estimation, and camera support.

## Prerequisites

- **CMake** (3.24+)
- **Conan** (2.0+)
- **Visual Studio 2022** (Windows) or GCC/Clang (Linux)
- **C++20** compiler support

## Quick Start

### 1. Install Dependencies

```bash
cd backend
# Install Release dependencies
conan install . --output-folder=build --build=missing -s compiler.cppstd=20

# Install Debug dependencies (Required for Visual Studio)
conan install . --output-folder=build --build=missing -s compiler.cppstd=20 -s build_type=Debug
```

### 2. Build

```bash
# Configure
cmake --preset conan-default

# Build
cmake --build --preset conan-release
```

## Running

```bash
# Run the built executable
./build/build/Release/backend.exe
```

The server starts on `http://localhost:5800` by default.

## Project Structure

```
backend/
├── src/
│   ├── core/           # Database, config, app initialization
│   ├── drivers/        # Camera drivers (USB, RealSense)
│   ├── models/         # Data models (Camera, Pipeline configs)
│   ├── pipelines/      # Vision pipelines (AprilTag, Color detection)
│   ├── routes/         # REST API endpoints
│   ├── services/       # Business logic services
│   ├── utils/          # Geometry utilities
│   └── vision/         # Field layout, coordinate systems
├── third_party/
│   ├── apriltag/       # AprilTag3 library
│   └── allwpilib/      # WPILib
├── CMakeLists.txt      # Main CMake configuration
└── conanfile.txt       # Conan dependency definitions
```

## API Endpoints

### Cameras
- `GET /api/cameras` - List all cameras
- `POST /api/cameras` - Create camera
- `GET /api/cameras/{id}` - Get camera by ID
- `DELETE /api/cameras/{id}` - Delete camera

### NetworkTables
- `GET /api/networktables/status` - Connection status
- `POST /api/networktables/connect` - Connect to team roboRIO
- `POST /api/networktables/disconnect` - Disconnect

### Calibration
- `GET /api/calibration/board` - Generate calibration board image
- `POST /api/calibration/detect` - Detect markers in image
- `POST /api/calibration/calibrate` - Perform camera calibration

## VS Code Setup

For IntelliSense, use the CMake Tools extension.
1. Select the `conan-default` configure preset.
2. Select the `conan-release` build preset.
