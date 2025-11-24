// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

#include "core/config.hpp"
#include "core/database.hpp"
#include "services/camera_service.hpp"
#include "services/pipeline_service.hpp"
#include "services/settings_service.hpp"
#include "services/streamer_service.hpp"
#include "drivers/usb_driver.hpp"
#include "drivers/spinnaker_driver.hpp"
#include "drivers/realsense_driver.hpp"
#include "threads/thread_manager.hpp"
#include "metrics/registry.hpp"
#include "routes/calibration.hpp"
#include "routes/networktables.hpp"
#include "hw/accel.hpp"
#include "utils/network_utils.hpp"

using json = nlohmann::json;
using namespace drogon;

int main(int argc, char** argv) {
    spdlog::info("2852-Vision");

    // Load configuration
    auto& config = vision::Config::instance();
    config.load();

    // Initialize database
    vision::Database::instance().initialize(config.database_path);

    // Initialize Spinnaker SDK for FLIR camera support
    vision::SpinnakerDriver::initialize();

    // Initialize MJPEG Streamer
    vision::StreamerService::instance().initialize(5805);

    // Start all configured cameras and pipelines at startup so acquisition/processing is always running
    {
        auto cameras = vision::CameraService::instance().getAllCameras();
        spdlog::info("Startup: found {} cameras in database", cameras.size());
        for (const auto& cam : cameras) {
            spdlog::info("Startup: starting camera {} (id={}, identifier={})", cam.name, cam.id, cam.identifier);
            if (!vision::ThreadManager::instance().startCamera(cam)) {
                spdlog::warn("Startup: failed to start camera {} ({})", cam.id, cam.name);
            }
        }

        auto pipelines = vision::PipelineService::instance().getAllPipelines();
        spdlog::info("Startup: found {} pipelines in database", pipelines.size());
        for (const auto& pipeline : pipelines) {
            // Ensure the owning camera thread exists before starting the pipeline
            auto camera = vision::CameraService::instance().getCameraById(pipeline.camera_id);
            if (!camera) {
                spdlog::warn("Startup: pipeline {} references missing camera {}; skipping start", pipeline.id, pipeline.camera_id);
                continue;
            }
            if (!vision::ThreadManager::instance().isCameraRunning(camera->id)) {
                spdlog::info("Startup: camera {} not running when starting pipeline {}; starting camera", camera->id, pipeline.id);
                vision::ThreadManager::instance().startCamera(*camera);
            }
            spdlog::info("Startup: starting pipeline {} for camera {}", pipeline.id, pipeline.camera_id);
            if (!vision::ThreadManager::instance().startPipeline(pipeline, pipeline.camera_id)) {
                spdlog::warn("Startup: failed to start pipeline {} for camera {}", pipeline.id, pipeline.camera_id);
            }
        }
    }

    // ============== Camera Routes ==============

    // GET /api/cameras - List all cameras
    app().registerHandler(
        "/api/cameras",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto cameras = vision::CameraService::instance().getAllCameras();
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
    app().registerHandler(
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

            vision::CameraType type = vision::CameraType::USB;
            if (typeStr == "Spinnaker") type = vision::CameraType::Spinnaker;
            else if (typeStr == "RealSense") type = vision::CameraType::RealSense;

            auto devices = vision::CameraService::instance().discoverCameras(type);
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
    app().registerHandler(
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

            vision::CameraType type = vision::CameraType::USB;
            if (typeStr == "Spinnaker") type = vision::CameraType::Spinnaker;
            else if (typeStr == "RealSense") type = vision::CameraType::RealSense;

            auto profiles = vision::CameraService::instance().getCameraProfiles(identifier, type);
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
    app().registerHandler(
        "/api/cameras/add",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());

                vision::Camera camera;
                camera.name = body.at("name").get<std::string>();
                camera.camera_type = body.at("camera_type").get<vision::CameraType>();
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

                auto created = vision::CameraService::instance().createCamera(camera);

                // Create a default pipeline (AprilTag) for the new camera
                vision::Pipeline defaultPipeline;
                defaultPipeline.name = "Default AprilTag";
                defaultPipeline.pipeline_type = vision::PipelineType::AprilTag;
                defaultPipeline.camera_id = created.id;
                auto createdPipeline = vision::PipelineService::instance().createPipeline(defaultPipeline);

                // Start camera and default pipeline threads so processing is active immediately
                vision::ThreadManager::instance().startCamera(created);
                vision::ThreadManager::instance().startPipeline(createdPipeline, created.id);

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
    app().registerHandler(
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

                    if (vision::CameraService::instance().updateCameraSettings(id, name, resolutionJson, framerate)) {
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
                    if (vision::CameraService::instance().updateCameraName(id, name)) {
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
    app().registerHandler(
        "/api/cameras/delete/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            // Stop pipelines for this camera and remove them from the database
            auto pipelines = vision::PipelineService::instance().getPipelinesForCamera(id);
            for (const auto& pipeline : pipelines) {
                vision::ThreadManager::instance().stopPipeline(pipeline.id);
                vision::PipelineService::instance().deletePipeline(pipeline.id);
            }

            // Stop camera thread
            vision::ThreadManager::instance().stopCamera(id);

            if (vision::CameraService::instance().deleteCamera(id)) {
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
    app().registerHandler(
        "/api/cameras/controls/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            auto camera = vision::CameraService::instance().getCameraById(id);
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
    app().registerHandler(
        "/api/cameras/update_controls/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto body = json::parse(req->getBody());

                int orientation = body.value("orientation", 0);
                vision::ExposureMode exposureMode = body.value("exposure_mode", vision::ExposureMode::Auto);
                int exposureValue = body.value("exposure_value", 500);
                vision::GainMode gainMode = body.value("gain_mode", vision::GainMode::Auto);
                int gainValue = body.value("gain_value", 50);

                if (vision::CameraService::instance().updateCameraControls(
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

    // ============== Spinnaker Routes ==============

    // GET /api/cameras/spinnaker/nodes/{id} - Get camera node map
    app().registerHandler(
        "/api/cameras/spinnaker/nodes/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto camera = vision::CameraService::instance().getCameraById(cameraId);
            if (!camera) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Camera not found"})");
                callback(resp);
                return;
            }

            if (camera->camera_type != vision::CameraType::Spinnaker) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Camera is not a Spinnaker/Spinnaker device"})");
                callback(resp);
                return;
            }

            if (!vision::SpinnakerDriver::isAvailable()) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Spinnaker support not compiled in"})");
                callback(resp);
                return;
            }

            auto [nodes, error] = vision::SpinnakerDriver::getNodeMap(camera->identifier);
            if (!error.empty()) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", error}}.dump());
                callback(resp);
                return;
            }

            json result = json::array();
            for (const auto& node : nodes) {
                result.push_back(node.toJson());
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // POST /api/cameras/spinnaker/nodes/{id} - Update camera node
    app().registerHandler(
        "/api/cameras/spinnaker/nodes/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            try {
                auto camera = vision::CameraService::instance().getCameraById(cameraId);
                if (!camera) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Camera not found"})");
                    callback(resp);
                    return;
                }

                if (camera->camera_type != vision::CameraType::Spinnaker) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Camera is not a Spinnaker/Spinnaker device"})");
                    callback(resp);
                    return;
                }

                if (!vision::SpinnakerDriver::isAvailable()) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Spinnaker support not compiled in"})");
                    callback(resp);
                    return;
                }

                auto body = json::parse(req->getBody());
                std::string nodeName = body.at("node_name").get<std::string>();
                std::string value = body.at("value").get<std::string>();

                auto [success, message, statusCode, updatedNode] =
                    vision::SpinnakerDriver::updateNode(camera->identifier, nodeName, value);

                json result = {
                    {"success", success},
                    {"message", message}
                };
                if (!updatedNode.is_null()) {
                    result["node"] = updatedNode;
                }
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(result.dump());
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // GET /api/spinnaker/status - Get Spinnaker SDK status
    app().registerHandler(
        "/api/spinnaker/status",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            json result = {
                {"available", vision::SpinnakerDriver::isAvailable()},
                {"sdk", "Spinnaker"}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // ============== ML Availability Routes ==============

    // GET /api/pipelines/ml/availability - Get ML runtime availability
    app().registerHandler(
        "/api/pipelines/ml/availability",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto availability = vision::hw::getMLAvailability();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(availability.dump());
            callback(resp);
        },
        {Get});

    // GET /api/pipelines/{id}/labels - Get pipeline labels
    app().registerHandler(
        "/api/pipelines/{id}/labels",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int pipelineId) {
            auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
            if (!pipeline) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Pipeline not found"})");
                callback(resp);
                return;
            }

            if (pipeline->pipeline_type != vision::PipelineType::ObjectDetectionML) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Pipeline is not an ML pipeline"})");
                callback(resp);
                return;
            }

            auto config = pipeline->getObjectDetectionMLConfig();
            std::vector<std::string> labels;

            // Try to load labels from file
            if (!config.labels_filename.empty()) {
                std::filesystem::path labelsPath = config.labels_filename;
                if (!labelsPath.is_absolute()) {
                    labelsPath = std::filesystem::current_path() / "data" / "models" / config.labels_filename;
                }

                if (std::filesystem::exists(labelsPath)) {
                    std::ifstream file(labelsPath);
                    std::string line;
                    while (std::getline(file, line)) {
                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        line.erase(line.find_last_not_of(" \t\r\n") + 1);
                        if (!line.empty()) {
                            labels.push_back(line);
                        }
                    }
                }
            }

            json result = labels;
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // POST /api/pipelines/{id}/files - Upload model/labels files
    app().registerHandler(
        "/api/pipelines/{id}/files",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int pipelineId) {
            auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
            if (!pipeline) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Pipeline not found"})");
                callback(resp);
                return;
            }

            // Create models directory
            std::filesystem::path modelsDir = std::filesystem::current_path() / "data" / "models";
            std::filesystem::create_directories(modelsDir);

            try {
                auto body = json::parse(req->getBody());

                std::string fileType = body.at("file_type").get<std::string>();
                std::string filename = body.at("filename").get<std::string>();
                std::string content = body.at("content").get<std::string>();

                // Save file
                std::filesystem::path filePath = modelsDir / filename;
                std::ofstream file(filePath, std::ios::binary);
                if (!file.is_open()) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to create file"})");
                    callback(resp);
                    return;
                }
                file << content;

                if (!file.good()) {
                    file.close();
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to write file content"})");
                    callback(resp);
                    return;
                }
                file.close();
                if (file.fail()) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to close file properly"})");
                    callback(resp);
                    return;
                }

                // Update pipeline config
                auto configJson = pipeline->getConfigJson();
                if (fileType == "model") {
                    configJson["model_filename"] = filename;
                } else if (fileType == "labels") {
                    configJson["labels_filename"] = filename;
                }
                pipeline->setConfigJson(configJson);
                vision::PipelineService::instance().updatePipeline(*pipeline);

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{
                    {"success", true},
                    {"filename", filename},
                    {"path", filePath.string()}
                }.dump());
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // DELETE /api/pipelines/{id}/files - Delete model/labels files
    app().registerHandler(
        "/api/pipelines/{id}/files",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int pipelineId) {
            auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
            if (!pipeline) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Pipeline not found"})");
                callback(resp);
                return;
            }

            try {
                auto body = json::parse(req->getBody());
                std::string fileType = body.at("file_type").get<std::string>();

                auto configJson = pipeline->getConfigJson();
                std::string filename;

                if (fileType == "model" && configJson.contains("model_filename")) {
                    filename = configJson["model_filename"].get<std::string>();
                    configJson.erase("model_filename");
                } else if (fileType == "labels" && configJson.contains("labels_filename")) {
                    filename = configJson["labels_filename"].get<std::string>();
                    configJson.erase("labels_filename");
                }

                // Delete file
                if (!filename.empty()) {
                    std::filesystem::path filePath = std::filesystem::current_path() / "data" / "models" / filename;
                    if (std::filesystem::exists(filePath)) {
                        std::filesystem::remove(filePath);
                    }
                }

                // Update pipeline config
                pipeline->setConfigJson(configJson);
                vision::PipelineService::instance().updatePipeline(*pipeline);

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Delete});

    // ============== Pipeline Routes ==============

    // GET /api/pipelines/cameras - List cameras (for pipeline management)
    app().registerHandler(
        "/api/pipelines/cameras",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto cameras = vision::CameraService::instance().getAllCameras();
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

    // GET /api/cameras/{id}/pipelines - Get pipelines for camera
    app().registerHandler(
        "/api/cameras/{id}/pipelines",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto pipelines = vision::PipelineService::instance().getPipelinesForCamera(cameraId);
            json result = json::array();
            for (const auto& p : pipelines) {
                result.push_back(p.toJson());
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // POST /api/cameras/{id}/pipelines - Create pipeline
    app().registerHandler(
        "/api/cameras/{id}/pipelines",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            try {
                auto body = json::parse(req->getBody());

                vision::Pipeline pipeline;
                pipeline.name = body.at("name").get<std::string>();
                pipeline.pipeline_type = body.at("pipeline_type").get<vision::PipelineType>();
                pipeline.camera_id = cameraId;

                if (body.contains("config")) {
                    pipeline.config = body["config"].dump();
                }

                auto created = vision::PipelineService::instance().createPipeline(pipeline);

                // Ensure owning camera thread is running and start this pipeline thread immediately
                auto camera = vision::CameraService::instance().getCameraById(cameraId);
                if (camera) {
                    if (!vision::ThreadManager::instance().isCameraRunning(cameraId)) {
                        vision::ThreadManager::instance().startCamera(*camera);
                    }
                    vision::ThreadManager::instance().startPipeline(created, cameraId);
                } else {
                    spdlog::warn("Created pipeline {} for missing camera {}", created.id, cameraId);
                }

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k201Created);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(created.toJson().dump());
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // PUT /api/pipelines/{id} - Update pipeline
    app().registerHandler(
        "/api/pipelines/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto body = json::parse(req->getBody());

                auto existing = vision::PipelineService::instance().getPipelineById(id);
                if (!existing) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Pipeline not found"})");
                    callback(resp);
                    return;
                }

                if (body.contains("name")) {
                    existing->name = body["name"].get<std::string>();
                }
                if (body.contains("pipeline_type")) {
                    existing->pipeline_type = body["pipeline_type"].get<vision::PipelineType>();
                }

                if (vision::PipelineService::instance().updatePipeline(*existing)) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(existing->toJson().dump());
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to update pipeline"})");
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
        {Put});

    // PUT /api/pipelines/{id}/config - Update pipeline config
    app().registerHandler(
        "/api/pipelines/{id}/config",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto config = json::parse(req->getBody());

                if (vision::PipelineService::instance().updatePipelineConfig(id, config)) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"success": true})");
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Pipeline not found"})");
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
        {Put});

    // DELETE /api/pipelines/{id} - Delete pipeline
    app().registerHandler(
        "/api/pipelines/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            // Stop processing thread first
            vision::ThreadManager::instance().stopPipeline(id);

            if (vision::PipelineService::instance().deletePipeline(id)) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } else {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Pipeline not found"})");
                callback(resp);
            }
        },
        {Delete});

    // ============== Settings Routes ==============

    // GET /api/settings - Get all settings
    app().registerHandler(
        "/api/settings",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto& settingsService = vision::SettingsService::instance();

                json result;
                result["global"] = settingsService.getGlobalSettings().toJson();
                result["network_tables"] = settingsService.getNetworkTableSettings().toJson();
                json fields = json::array();
                for (const auto& f : settingsService.getAvailableFields()) {
                    fields.push_back({
                        {"name", f.name},
                        {"is_system", f.is_system}
                    });
                }
                result["apriltag"] = {
                    {"selected_field", settingsService.getSelectedField()},
                    {"available_fields", fields}
                };
                result["spinnaker_available"] = vision::SpinnakerDriver::isAvailable();

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(result.dump());
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Get});

    // PUT /api/settings/global - Update global settings
    app().registerHandler(
        "/api/settings/global",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                auto settings = vision::GlobalSettings::fromJson(body);
                auto& settingsService = vision::SettingsService::instance();
                auto currentSettings = settingsService.getGlobalSettings();

                std::string platform = vision::network::getPlatform();
                bool isLinux = (platform == "linux");

                // Validate hostname if it changed and we're on Linux
                if (isLinux && settings.hostname != currentSettings.hostname) {
                    std::string hostnameError = vision::network::validateHostname(settings.hostname);
                    if (!hostnameError.empty()) {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k400BadRequest);
                        resp->setContentTypeCode(CT_APPLICATION_JSON);
                        resp->setBody(json{{"error", hostnameError}}.dump());
                        callback(resp);
                        return;
                    }
                }

                // Save settings to database
                settingsService.setGlobalSettings(settings);

                // Apply network configuration on Linux only
                if (isLinux) {
                    std::string error;

                    // Set hostname if changed
                    if (settings.hostname != currentSettings.hostname) {
                        if (!vision::network::setHostname(settings.hostname, error)) {
                            spdlog::warn("Failed to set hostname: {}", error);
                        }
                    }

                    // Configure network interface if specified
                    if (!settings.network_interface.empty()) {
                        if (settings.ip_mode == "static") {
                            std::string staticIp = settings.static_ip;
                            std::string gateway = settings.gateway;
                            std::string subnetMask = settings.subnet_mask;

                            if (staticIp.empty()) {
                                staticIp = vision::network::calculateStaticIP(settings.team_number);
                            }
                            if (gateway.empty()) {
                                gateway = vision::network::calculateDefaultGateway(settings.team_number);
                            }
                            if (subnetMask.empty()) {
                                subnetMask = "255.255.255.0";
                            }

                            if (!vision::network::setStaticIP(settings.network_interface, staticIp, gateway, subnetMask, error)) {
                                spdlog::warn("Failed to set static IP: {}", error);
                            }
                        } else if (settings.ip_mode == "dhcp") {
                            if (!vision::network::setDHCP(settings.network_interface, error)) {
                                spdlog::warn("Failed to set DHCP: {}", error);
                            }
                        }
                    }
                }

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Put});

    // GET /api/settings/apriltag/fields - Get available fields
    app().registerHandler(
        "/api/settings/apriltag/fields",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto fields = vision::SettingsService::instance().getAvailableFields();
            json result = json::array();
            for (const auto& f : fields) {
                result.push_back({
                    {"name", f.name},
                    {"is_system", f.is_system}
                });
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // PUT /api/settings/apriltag/select - Select field
    app().registerHandler(
        "/api/settings/apriltag/select",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                std::string fieldName = body.at("field").get<std::string>();
                vision::SettingsService::instance().setSelectedField(fieldName);
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Put});

    // POST /api/settings/control/factory-reset - Factory reset
    app().registerHandler(
        "/api/settings/control/factory-reset",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            vision::SettingsService::instance().factoryReset();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"success": true})");
            callback(resp);
        },
        {Post});

    // POST /api/apriltag/upload - Upload custom field layout
    app().registerHandler(
        "/api/apriltag/upload",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                std::string name = body.at("name").get<std::string>();
                std::string content = body.at("content").get<std::string>();

                // Validate JSON content
                try {
                    (void)json::parse(content);
                } catch (...) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Invalid JSON content"})");
                    callback(resp);
                    return;
                }

                if (vision::SettingsService::instance().addCustomField(name, content)) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"success": true})");
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to save field layout"})");
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

    // POST /api/apriltag/delete - Delete custom field layout
    app().registerHandler(
        "/api/apriltag/delete",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                std::string name = body.at("name").get<std::string>();

                if (vision::SettingsService::instance().deleteField(name)) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"success": true})");
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Field not found or cannot be deleted"})");
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

    // POST /api/control/restart-app - Restart application
    app().registerHandler(
        "/api/control/restart-app",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            spdlog::info("Application restart requested");
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"success": true, "message": "Restart requested"})");
            callback(resp);
        },
        {Post});

    // POST /api/control/reboot - Reboot device
    app().registerHandler(
        "/api/control/reboot",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            spdlog::info("Device reboot requested");
#ifdef __linux__
            int result = system("sudo reboot");
            if (result == 0) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } else {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Reboot command failed"})");
                callback(resp);
            }
#else
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"error": "Reboot only supported on Linux"})");
            callback(resp);
#endif
        },
        {Post});

    // ============== Metrics ==============

    // GET /api/metrics/summary - Get combined metrics snapshot
    app().registerHandler(
        "/api/metrics/summary",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto summary = vision::MetricsRegistry::instance().getSummary();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(summary.toJson().dump());
            callback(resp);
        },
        {Get});

    // GET /api/metrics/system - Get system metrics only
    app().registerHandler(
        "/api/metrics/system",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto metrics = vision::MetricsRegistry::instance().getSystemMetrics();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(metrics.toJson().dump());
            callback(resp);
        },
        {Get});


    // GET /api/cameras/results/{id} - Get pipeline results for camera
    app().registerHandler(
        "/api/cameras/results/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto results = vision::ThreadManager::instance().getCameraResults(cameraId);
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(results.dump());
            callback(resp);
        },
        {Get});

    // GET /api/cameras/status/{id} - Check camera connection status
    app().registerHandler(
        "/api/cameras/status/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto camera = vision::CameraService::instance().getCameraById(cameraId);
            if (!camera) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", "Camera not found"}}.dump());
                callback(resp);
                return;
            }

            bool running = vision::ThreadManager::instance().isCameraRunning(cameraId);

            // Check if physical camera is connected
            bool physicallyConnected = false;
            std::vector<vision::DeviceInfo> devices;

            switch (camera->camera_type) {
                case vision::CameraType::USB:
                    devices = vision::USBDriver::listDevices();
                    break;
                case vision::CameraType::Spinnaker:
                    if (vision::SpinnakerDriver::isAvailable()) {
                        devices = vision::SpinnakerDriver::listDevices();
                    }
                    break;
                case vision::CameraType::RealSense:
                    if (vision::RealSenseDriver::isAvailable()) {
                        devices = vision::RealSenseDriver::listDevices();
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

    // ============== Health Check ==============

    app().registerHandler(
        "/",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"status": "ok", "version": "1.0.0"})");
            callback(resp);
        },
        {Get});

    app().registerHandler(
        "/health",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"status": "healthy"})");
            callback(resp);
        },
        {Get});

    // GET /api/network - Get network information
    app().registerHandler(
        "/api/network",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto networkInfo = vision::network::getNetworkInfo();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(networkInfo.toJson().dump());
            callback(resp);
        },
        {Get});

    // GET /api/system/platform - Get current platform
    app().registerHandler(
        "/api/system/platform",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            json result = {
                {"platform", vision::network::getPlatform()}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // GET /api/network/interfaces - Get list of network interfaces
    app().registerHandler(
        "/api/network/interfaces",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto interfaces = vision::network::getNetworkInterfaces();
            json result = json::array();
            for (const auto& iface : interfaces) {
                result.push_back(iface);
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // GET /api/network/calculate-ip - Calculate static IP from team number
    app().registerHandler(
        "/api/network/calculate-ip",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            int teamNumber = 0;
            std::string teamStr = req->getParameter("team");
            if (!teamStr.empty()) {
                try {
                    teamNumber = std::stoi(teamStr);
                } catch (...) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Invalid team number"})");
                    callback(resp);
                    return;
                }
            }

            json result = {
                {"static_ip", vision::network::calculateStaticIP(teamNumber)},
                {"gateway", vision::network::calculateDefaultGateway(teamNumber)},
                {"subnet_mask", "255.255.255.0"}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // ============== Database Export/Import ==============

    // GET /api/database/export - Export SQLite database file
    app().registerHandler(
        "/api/database/export",
        [&config](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                std::filesystem::path dbPath = config.database_path;

                if (!std::filesystem::exists(dbPath)) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Database file not found"})");
                    callback(resp);
                    return;
                }

                std::ifstream file(dbPath, std::ios::binary);
                if (!file) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to read database file"})");
                    callback(resp);
                    return;
                }

                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                file.close();

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setBody(content);
                resp->setContentTypeString("application/octet-stream");
                resp->addHeader("Content-Disposition", "attachment; filename=\"2852vision.db\"");
                callback(resp);
            } catch (const std::exception& e) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Get});

    // POST /api/database/import - Import SQLite database file
    app().registerHandler(
        "/api/database/import",
        [&config](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                std::filesystem::path dbPath = config.database_path;
                std::filesystem::path backupPath = dbPath.string() + ".backup";

                const std::string& fileData = std::string(req->getBody());

                if (fileData.empty()) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "No database file provided"})");
                    callback(resp);
                    return;
                }

                if (fileData.size() < 16 || fileData.substr(0, 15) != "SQLite format 3") {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Invalid SQLite database file"})");
                    callback(resp);
                    return;
                }

                if (std::filesystem::exists(dbPath)) {
                    std::filesystem::copy_file(dbPath, backupPath,
                        std::filesystem::copy_options::overwrite_existing);
                }

                std::ofstream outFile(dbPath, std::ios::binary | std::ios::trunc);
                if (!outFile) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to open database file for writing"})");
                    callback(resp);
                    return;
                }

                outFile.write(fileData.c_str(), fileData.size());
                outFile.close();

                if (!outFile.good()) {
                    if (std::filesystem::exists(backupPath)) {
                        std::filesystem::copy_file(backupPath, dbPath,
                            std::filesystem::copy_options::overwrite_existing);
                    }
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to write database file"})");
                    callback(resp);
                    return;
                }

                vision::Database::instance().initialize(config.database_path);

                if (std::filesystem::exists(backupPath)) {
                    std::filesystem::remove(backupPath);
                }

                spdlog::info("Database imported successfully from uploaded file");
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"success": true})");
                callback(resp);
            } catch (const std::exception& e) {
                spdlog::error("Database import failed: {}", e.what());
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(json{{"error", e.what()}}.dump());
                callback(resp);
            }
        },
        {Post});

    // ============== Calibration Routes ==============
    vision::CalibrationService::registerRoutes(app());

    // ============== NetworkTables Routes ==============
    vision::NetworkTablesRoutes::registerRoutes(app());

    // Start server
    spdlog::info("Starting server on {}:{}", config.server.host, config.server.port);

    app().setLogLevel(trantor::Logger::kWarn)
        .addListener(config.server.host, config.server.port)
        .setThreadNum(config.server.threads)
        .run();

    // Shutdown threads on exit
    vision::ThreadManager::instance().shutdown();

    // Shutdown Spinnaker SDK
    vision::SpinnakerDriver::shutdown();

    return 0;
}
