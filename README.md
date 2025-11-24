# 2852Vision

Vision system for real-time camera streaming, processing, and calibration. Similar to PhotonVision or Limelight, but fully open and extensible.

## Features

- Supports USB, RealSense, and Spinnaker (FLIR) cameras
- AprilTag + object/shape detection pipelines
- Camera calibration with distortion correction
- Web-based dashboard for configuration

## Prerequisites (Ubuntu/Linux)

Before building, ensure you have node 18+, xmake and the necessary system tools and compilers installed.

### System Packages
Install required packages:

```bash
sudo apt update
sudo apt install -y build-essential ninja-build git patch unzip pkg-config libgtk-3-dev libgl1-mesa-dev libglu1-mesa-dev
```

## Quick Start

### Install Frontend Dependencies
```bash
npm install
```

### Development

Run the backend and frontend separately in two terminals:

**Terminal 1 - Backend (C++):**
This will configure dependencies (Drogon, OpenCV, etc.) and run the server.
```bash
# First time configuration
xmake f -y

# Build and Run
xmake run backend
```

**Terminal 2 - Frontend (React):**
```bash
npm run dev
```

- Frontend Dev Server: http://localhost:8080
- Backend API: http://localhost:5800 (default Drogon port)

### Production Build

**Frontend:**
```bash
npm run build
```

**Backend:**
```bash
xmake f -m release
xmake
```

### Testing
```bash
npm run test:e2e
```