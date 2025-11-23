# 2852Vision Backend

C++ backend for FRC robot vision processing with AprilTag detection, pose estimation, and camera support.

## Prerequisites

- **xmake** - Build system ([install](https://xmake.io/#/guide/installation))
- **Visual Studio 2022** - C++ build tools (Windows)
- **CMake** - For building dependencies (auto-built by xmake)
- **C++20** compiler support

## Quick Start

```bash
# Initialize submodules
git submodule update --init --recursive

# Build (dependencies are built automatically if needed)
cd backend
xmake f -p windows -a x64
xmake
```

On first build, xmake will automatically build:
- **allwpilib** (NetworkTables support)

This may take several minutes on the first run.

## Build Options

### View All Options

```bash
xmake f --help
```

### Build Configurations

```bash
# Debug build (default)
xmake f -m debug
xmake

# Release build
xmake f -m release
xmake
```

## Optional Features

### Intel RealSense

For depth camera support with Intel RealSense cameras (D415, D435, D455, etc.)

**Step 1: Install SDK**: Download and install [Intel RealSense SDK 2.0](https://github.com/IntelRealSense/librealsense/releases)

Default installation path: `C:/Program Files (x86)/Intel RealSense SDK 2.0/`

**Step 2: Enable in xmake**:
```bash
xmake f --realsense=y -p windows -a x64
xmake
```

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
│   ├── apriltag/       # AprilTag3 library (built from source by xmake)
│   └── allwpilib/      # WPILib (git submodule, auto-built)
└── xmake.lua           # Build configuration
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

For IntelliSense and code navigation, generate `compile_commands.json`:

```bash
xmake project -k compile_commands
```

Then configure VS Code's C/C++ extension to use it. Create/update `.vscode/c_cpp_properties.json`:

```json
{
    "configurations": [
        {
            "name": "Win32",
            "compileCommands": "${workspaceFolder}/compile_commands.json"
        }
    ],
    "version": 4
}
```

**Note**: `compile_commands.json` is machine-specific and should not be committed to git. Each developer generates their own.

## Running

```bash
# Run the built executable
xmake run

# Or directly
./build/windows/x64/debug/backend.exe
```

The server starts on `http://localhost:5800` by default.

## Rebuilding Dependencies

If you need to rebuild the third-party dependencies:

```bash
# Clean allwpilib build
rm -rf third_party/allwpilib/build-cmake

# Rebuild
xmake f -c
xmake
```
