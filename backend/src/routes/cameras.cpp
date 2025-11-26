// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/cameras.hpp"
#include "services/camera_service.hpp"
#include "services/pipeline_service.hpp"
#include "threads/thread_manager.hpp"
#include "drivers/usb_driver.hpp"
#include "drivers/spinnaker_driver.hpp"
#include "drivers/realsense_driver.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vision {

void CamerasController::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;
    using json = nlohmann::json;

    // GET /api/cameras - List all cameras
    app.registerHandler(
        "/api/cameras",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto cameras = CameraService::instance().getAllCameras();
            json result = json::array();
            for (const auto& cam : cameras) {
                result.push_back(cam.toJson());
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // GET /api/cameras/discover - Discover cameras
    app.registerHandler(
        "/api/cameras/discover",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            std::string typeStr = "USB";
            if (req->getParameter("type").length() > 0) {
                typeStr = req->getParameter("type");
            }

            // Parse existing identifiers to exclude
            std::vector<std::string> existingIdentifiers;
            std::string existingStr = req->getParameter("existing");
            if (!existingStr.empty()) {
                // Split by comma
                size_t pos = 0;
                while ((pos = existingStr.find(',')) != std::string::npos) {
                    std::string token = existingStr.substr(0, pos);
                    if (!token.empty()) {
                        existingIdentifiers.push_back(token);
                    }
                    existingStr.erase(0, pos + 1);
                }
                if (!existingStr.empty()) {
                    existingIdentifiers.push_back(existingStr);
                }
            }

            CameraType type = CameraType::USB;
            if (typeStr == "Spinnaker") type = CameraType::Spinnaker;
            else if (typeStr == "RealSense") type = CameraType::RealSense;

            auto devices = CameraService::instance().discoverCameras(type);
            json result = json::array();
            for (const auto& dev : devices) {
                // Filter out existing cameras
                bool isExisting = false;
                for (const auto& existingId : existingIdentifiers) {
                    if (dev.identifier == existingId) {
                        isExisting = true;
                        break;
                    }
                }
                if (!isExisting) {
                    result.push_back(dev.toJson());
                }
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // GET /api/cameras/profiles - Get camera profiles
    app.registerHandler(
        "/api/cameras/profiles",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            std::string identifier = req->getParameter("identifier");
            if (identifier.empty()) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Missing identifier parameter"})");
                callback(resp);
                return;
            }

            std::string typeStr = req->getParameter("type");
            if (typeStr.empty()) typeStr = "USB";

            CameraType type = CameraType::USB;
            if (typeStr == "Spinnaker") type = CameraType::Spinnaker;
            else if (typeStr == "RealSense") type = CameraType::RealSense;

            auto profiles = CameraService::instance().getCameraProfiles(identifier, type);
            json result = json::array();
            for (const auto& profile : profiles) {
                result.push_back(profile.toJson());
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // POST /api/cameras/add - Add new camera
    app.registerHandler(
        "/api/cameras/add",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());

                Camera camera;
                camera.name = body.at("name").get<std::string>();
                camera.camera_type = body.at("camera_type").get<CameraType>();
                camera.identifier = body.at("identifier").get<std::string>();

                if (body.contains("resolution")) {
                    camera.resolution_json = body["resolution"].dump();
                }
                if (body.contains("framerate")) {
                    camera.framerate = body["framerate"].get<int>();
                }
                if (body.contains("depth_enabled")) {
                    camera.depth_enabled = body["depth_enabled"].get<bool>();
                }

                auto created = CameraService::instance().createCamera(camera);

                // Create a default pipeline (AprilTag) for the new camera
                Pipeline defaultPipeline;
                defaultPipeline.name = "Default AprilTag";
                defaultPipeline.pipeline_type = PipelineType::AprilTag;
                defaultPipeline.camera_id = created.id;
                auto createdPipeline = PipelineService::instance().createPipeline(defaultPipeline);

                // Start camera and default pipeline threads so processing is active immediately
                ThreadManager::instance().startCamera(created);
                ThreadManager::instance().startPipeline(createdPipeline, created.id);

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k201Created);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(created.toJson().dump());
                callback(resp);
            } catch (const std::exception& e) {
                spdlog::error("Failed to add camera: {}", e.what());
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // POST /api/cameras/update/{id} - Update camera settings
    app.registerHandler(
        "/api/cameras/update/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto body = json::parse(req->getBody());
                std::string name = body.at("name").get<std::string>();

                // Check if resolution and framerate are provided
                if (body.contains("resolution") && body.contains("framerate")) {
                    std::string resolutionJson = body["resolution"].dump();
                    int framerate = body["framerate"].get<int>();

                    if (CameraService::instance().updateCameraSettings(id, name, resolutionJson, framerate)) {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k200OK);
                        resp->setContentTypeCode(CT_APPLICATION_JSON);
                        resp->setBody(R"({"success": true})");
                        callback(resp);
                    } else {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k404NotFound);
                        resp->setContentTypeCode(CT_APPLICATION_JSON);
                        resp->setBody(R"({"error": "Camera not found"})");
                        callback(resp);
                    }
                } else {
                    // Only update name (backwards compatibility)
                    if (CameraService::instance().updateCameraName(id, name)) {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k200OK);
                        resp->setContentTypeCode(CT_APPLICATION_JSON);
                        resp->setBody(R"({"success": true})");
                        callback(resp);
                    } else {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k404NotFound);
                        resp->setContentTypeCode(CT_APPLICATION_JSON);
                        resp->setBody(R"({"error": "Camera not found"})");
                        callback(resp);
                    }
                }
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // POST /api/cameras/delete/{id} - Delete camera
    app.registerHandler(
        "/api/cameras/delete/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            // Stop pipelines for this camera and remove them from the database
            auto pipelines = PipelineService::instance().getPipelinesForCamera(id);
            for (const auto& pipeline : pipelines) {
                ThreadManager::instance().stopPipeline(pipeline.id);
                PipelineService::instance().deletePipeline(pipeline.id);
            }

            // Stop camera thread
            ThreadManager::instance().stopCamera(id);

            if (CameraService::instance().deleteCamera(id)) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } else {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Camera not found"})");
                callback(resp);
            }
        },
        {Post});

    // GET /api/cameras/controls/{id} - Get camera controls
    app.registerHandler(
        "/api/cameras/controls/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            auto camera = CameraService::instance().getCameraById(id);
            if (!camera) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Camera not found"})");
                callback(resp);
                return;
            }

            json result = {
                {"orientation", camera->orientation},
                {"exposure_mode", camera->exposure_mode},
                {"exposure_value", camera->exposure_value},
                {"gain_mode", camera->gain_mode},
                {"gain_value", camera->gain_value}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // POST /api/cameras/update_controls/{id} - Update camera controls
    app.registerHandler(
        "/api/cameras/update_controls/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto body = json::parse(req->getBody());

                int orientation = body.value("orientation", 0);
                ExposureMode exposureMode = body.value("exposure_mode", ExposureMode::Auto);
                int exposureValue = body.value("exposure_value", 500);
                GainMode gainMode = body.value("gain_mode", GainMode::Auto);
                int gainValue = body.value("gain_value", 50);

                if (CameraService::instance().updateCameraControls(
                        id, orientation, exposureMode, exposureValue, gainMode, gainValue)) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"success": true})");
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Camera not found"})");
                    callback(resp);
                }
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // GET /api/cameras/results/{id} - Get pipeline results for camera
    app.registerHandler(
        "/api/cameras/results/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto results = ThreadManager::instance().getCameraResults(cameraId);
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(results.dump());
            callback(resp);
        },
        {Get});

    // GET /api/cameras/status/{id} - Check camera connection status
    app.registerHandler(
        "/api/cameras/status/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto camera = CameraService::instance().getCameraById(cameraId);
            if (!camera) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", "Camera not found"}}.dump());
                callback(resp);
                return;
            }

            bool running = ThreadManager::instance().isCameraRunning(cameraId);

            // Check if physical camera is connected
            bool physicallyConnected = false;
            std::vector<DeviceInfo> devices;

            switch (camera->camera_type) {
                case CameraType::USB:
                    devices = USBDriver::listDevices();
                    break;
                case CameraType::Spinnaker:
                    if (SpinnakerDriver::isAvailable()) {
                        devices = SpinnakerDriver::listDevices();
                    }
                    break;
                case CameraType::RealSense:
                    if (RealSenseDriver::isAvailable()) {
                        devices = RealSenseDriver::listDevices();
                    }
                    break;
                default:
                    break;
            }

            for (const auto& device : devices) {
                if (device.identifier == camera->identifier) {
                    physicallyConnected = true;
                    break;
                }
            }

            json result = {
                {"camera_id", cameraId},
                {"connected", physicallyConnected},
                {"streaming", running}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    spdlog::info("Camera routes registered");
}

} // namespace vision
