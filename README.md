# 2852Vision

Vision system for real-time camera streaming, processing, and calibration. Similar to PhotonVision or Limelight, but fully open and extensible.

## Features

- Supports USB, RealSense, and Spinnaker (FLIR) cameras
- AprilTag + object/shape detection pipelines
- Camera calibration with distortion correction
- Web-based dashboard for configuration

## Quick Start

### Requirements

- Node 18+

### Install
```bash
npm install
```

### Development

Run the backend and frontend separately in two terminals:

**Terminal 1 - Backend:**
```bash
```

**Terminal 2 - Frontend:**
```bash
npm run dev
```

- Frontend Dev Server: http://localhost:8080

Open: http://localhost:8080

### Production
```bash
npm run build
```

### Testing
```bash
npm run test:e2e
```