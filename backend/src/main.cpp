#include <crow.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

#include "core/config.hpp"
#include "core/database.hpp"
#include "services/camera_service.hpp"
#include "services/pipeline_service.hpp"
#include "services/settings_service.hpp"
#include "drivers/usb_driver.hpp"
#include "drivers/spinnaker_driver.hpp"
#include "drivers/realsense_driver.hpp"
#include "threads/thread_manager.hpp"
#include "metrics/registry.hpp"
#include "routes/calibration.hpp"
#include "hw/accel.hpp"
#include "utils/network_utils.hpp"

using json = nlohmann::json;

int main(int argc, char** argv) {
    spdlog::info("2852-Vision");

    // Load configuration
    auto& config = vision::Config::instance();
    config.load();

    // Initialize database
    vision::Database::instance().initialize(config.database_path);

    // Initialize Spinnaker SDK for FLIR camera support
    vision::SpinnakerDriver::initialize();

    // Create Crow app
    crow::SimpleApp app;

    // ============== Camera Routes ==============

    // GET /api/cameras - List all cameras
    CROW_ROUTE(app, "/api/cameras")
    ([]() {
        auto cameras = vision::CameraService::instance().getAllCameras();
        json result = json::array();
        for (const auto& cam : cameras) {
            result.push_back(cam.toJson());
        }
        return crow::response(200, "application/json", result.dump());
    });

    // GET /api/cameras/discover - Discover cameras
    CROW_ROUTE(app, "/api/cameras/discover")
    ([](const crow::request& req) {
        std::string typeStr = "USB";
        if (req.url_params.get("type")) {
            typeStr = req.url_params.get("type");
        }

        // Parse existing identifiers to exclude
        std::vector<std::string> existingIdentifiers;
        if (req.url_params.get("existing")) {
            std::string existingStr = req.url_params.get("existing");
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
        return crow::response(200, "application/json", result.dump());
    });

    // GET /api/cameras/profiles - Get camera profiles
    CROW_ROUTE(app, "/api/cameras/profiles")
    ([](const crow::request& req) {
        if (!req.url_params.get("identifier")) {
            return crow::response(400, "application/json", R"({"error": "Missing identifier parameter"})");
        }

        std::string identifier = req.url_params.get("identifier");
        std::string typeStr = req.url_params.get("type") ? req.url_params.get("type") : "USB";

        vision::CameraType type = vision::CameraType::USB;
        if (typeStr == "Spinnaker") type = vision::CameraType::Spinnaker;
        else if (typeStr == "RealSense") type = vision::CameraType::RealSense;

        auto profiles = vision::CameraService::instance().getCameraProfiles(identifier, type);
        json result = json::array();
        for (const auto& profile : profiles) {
            result.push_back(profile.toJson());
        }
        return crow::response(200, "application/json", result.dump());
    });

    // POST /api/cameras/add - Add new camera
    CROW_ROUTE(app, "/api/cameras/add").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            auto body = json::parse(req.body);

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
            return crow::response(201, "application/json", created.toJson().dump());
        } catch (const std::exception& e) {
            spdlog::error("Failed to add camera: {}", e.what());
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // POST /api/cameras/update/<id> - Update camera settings (name, resolution, framerate)
    CROW_ROUTE(app, "/api/cameras/update/<int>").methods("POST"_method)
    ([](const crow::request& req, int id) {
        try {
            auto body = json::parse(req.body);
            std::string name = body.at("name").get<std::string>();

            // Check if resolution and framerate are provided
            if (body.contains("resolution") && body.contains("framerate")) {
                std::string resolutionJson = body["resolution"].dump();
                int framerate = body["framerate"].get<int>();

                if (vision::CameraService::instance().updateCameraSettings(id, name, resolutionJson, framerate)) {
                    return crow::response(200, "application/json", R"({"success": true})");
                } else {
                    return crow::response(404, "application/json", R"({"error": "Camera not found"})");
                }
            } else {
                // Only update name (backwards compatibility)
                if (vision::CameraService::instance().updateCameraName(id, name)) {
                    return crow::response(200, "application/json", R"({"success": true})");
                } else {
                    return crow::response(404, "application/json", R"({"error": "Camera not found"})");
                }
            }
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // POST /api/cameras/delete/<id> - Delete camera
    CROW_ROUTE(app, "/api/cameras/delete/<int>").methods("POST"_method)
    ([](int id) {
        if (vision::CameraService::instance().deleteCamera(id)) {
            return crow::response(200, "application/json", R"({"success": true})");
        } else {
            return crow::response(404, "application/json", R"({"error": "Camera not found"})");
        }
    });

    // GET /api/cameras/controls/<id> - Get camera controls
    CROW_ROUTE(app, "/api/cameras/controls/<int>")
    ([](int id) {
        auto camera = vision::CameraService::instance().getCameraById(id);
        if (!camera) {
            return crow::response(404, "application/json", R"({"error": "Camera not found"})");
        }

        json result = {
            {"orientation", camera->orientation},
            {"exposure_mode", camera->exposure_mode},
            {"exposure_value", camera->exposure_value},
            {"gain_mode", camera->gain_mode},
            {"gain_value", camera->gain_value}
        };
        return crow::response(200, "application/json", result.dump());
    });

    // POST /api/cameras/update_controls/<id> - Update camera controls
    CROW_ROUTE(app, "/api/cameras/update_controls/<int>").methods("POST"_method)
    ([](const crow::request& req, int id) {
        try {
            auto body = json::parse(req.body);

            int orientation = body.value("orientation", 0);
            vision::ExposureMode exposureMode = body.value("exposure_mode", vision::ExposureMode::Auto);
            int exposureValue = body.value("exposure_value", 500);
            vision::GainMode gainMode = body.value("gain_mode", vision::GainMode::Auto);
            int gainValue = body.value("gain_value", 50);

            if (vision::CameraService::instance().updateCameraControls(
                    id, orientation, exposureMode, exposureValue, gainMode, gainValue)) {
                return crow::response(200, "application/json", R"({"success": true})");
            } else {
                return crow::response(404, "application/json", R"({"error": "Camera not found"})");
            }
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // ============== Spinnaker/Spinnaker Routes ==============

    // GET /api/cameras/spinnaker/nodes/<id> - Get camera node map (Spinnaker/Spinnaker)
    CROW_ROUTE(app, "/api/cameras/spinnaker/nodes/<int>")
    ([](int cameraId) {
        auto camera = vision::CameraService::instance().getCameraById(cameraId);
        if (!camera) {
            return crow::response(404, "application/json", R"({"error": "Camera not found"})");
        }

        if (camera->camera_type != vision::CameraType::Spinnaker) {
            return crow::response(400, "application/json", R"({"error": "Camera is not a Spinnaker/Spinnaker device"})");
        }

        if (!vision::SpinnakerDriver::isAvailable()) {
            return crow::response(500, "application/json", R"({"error": "Spinnaker support not compiled in"})");
        }

        auto [nodes, error] = vision::SpinnakerDriver::getNodeMap(camera->identifier);
        if (!error.empty()) {
            return crow::response(500, "application/json", json{{"error", error}}.dump());
        }

        json result = json::array();
        for (const auto& node : nodes) {
            result.push_back(node.toJson());
        }
        return crow::response(200, "application/json", result.dump());
    });

    // POST /api/cameras/spinnaker/nodes/<id> - Update camera node (Spinnaker/Spinnaker)
    CROW_ROUTE(app, "/api/cameras/spinnaker/nodes/<int>").methods("POST"_method)
    ([](const crow::request& req, int cameraId) {
        try {
            auto camera = vision::CameraService::instance().getCameraById(cameraId);
            if (!camera) {
                return crow::response(404, "application/json", R"({"error": "Camera not found"})");
            }

            if (camera->camera_type != vision::CameraType::Spinnaker) {
                return crow::response(400, "application/json", R"({"error": "Camera is not a Spinnaker/Spinnaker device"})");
            }

            if (!vision::SpinnakerDriver::isAvailable()) {
                return crow::response(500, "application/json", R"({"error": "Spinnaker support not compiled in"})");
            }

            auto body = json::parse(req.body);
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
            return crow::response(statusCode, "application/json", result.dump());
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // GET /api/spinnaker/status - Get Spinnaker SDK status
    CROW_ROUTE(app, "/api/spinnaker/status")
    ([]() {
        json result = {
            {"available", vision::SpinnakerDriver::isAvailable()},
            {"sdk", "Spinnaker"}
        };
        return crow::response(200, "application/json", result.dump());
    });

    // ============== ML Availability Routes ==============

    // GET /api/pipelines/ml/availability - Get ML runtime availability
    CROW_ROUTE(app, "/api/pipelines/ml/availability")
    ([]() {
        auto availability = vision::hw::getMLAvailability();
        return crow::response(200, "application/json", availability.dump());
    });

    // GET /api/pipelines/<id>/labels - Get pipeline labels
    CROW_ROUTE(app, "/api/pipelines/<int>/labels")
    ([](int pipelineId) {
        auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
        if (!pipeline) {
            return crow::response(404, "application/json", R"({"error": "Pipeline not found"})");
        }

        if (pipeline->pipeline_type != vision::PipelineType::ObjectDetectionML) {
            return crow::response(400, "application/json", R"({"error": "Pipeline is not an ML pipeline"})");
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
        return crow::response(200, "application/json", result.dump());
    });

    // POST /api/pipelines/<id>/files - Upload model/labels files
    CROW_ROUTE(app, "/api/pipelines/<int>/files").methods("POST"_method)
    ([](const crow::request& req, int pipelineId) {
        auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
        if (!pipeline) {
            return crow::response(404, "application/json", R"({"error": "Pipeline not found"})");
        }

        // Create models directory
        std::filesystem::path modelsDir = std::filesystem::current_path() / "data" / "models";
        std::filesystem::create_directories(modelsDir);

        // Parse multipart form data
        // Note: This is a simplified implementation - crow's multipart support may need additional handling
        try {
            auto body = json::parse(req.body);

            std::string fileType = body.at("file_type").get<std::string>();  // "model" or "labels"
            std::string filename = body.at("filename").get<std::string>();
            std::string content = body.at("content").get<std::string>();  // Base64 encoded

            // Decode base64 content
            // Note: In production, use proper base64 decoding
            std::vector<uint8_t> data;
            // ... base64 decode ...

            // Save file
            std::filesystem::path filePath = modelsDir / filename;
            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open()) {
                return crow::response(500, "application/json", R"({"error": "Failed to create file"})");
            }
            // file.write(reinterpret_cast<const char*>(data.data()), data.size());
            file << content;  // For now, write as text

            // Check for write errors
            if (!file.good()) {
            file.close();
                return crow::response(500, "application/json", R"({"error": "Failed to write file content"})");
            }
            file.close();
            if (file.fail()) {
                return crow::response(500, "application/json", R"({"error": "Failed to close file properly"})");
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

            return crow::response(200, "application/json", json{
                {"success", true},
                {"filename", filename},
                {"path", filePath.string()}
            }.dump());
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // DELETE /api/pipelines/<id>/files - Delete model/labels files
    CROW_ROUTE(app, "/api/pipelines/<int>/files").methods("DELETE"_method)
    ([](const crow::request& req, int pipelineId) {
        auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
        if (!pipeline) {
            return crow::response(404, "application/json", R"({"error": "Pipeline not found"})");
        }

        try {
            auto body = json::parse(req.body);
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

            return crow::response(200, "application/json", R"({"success": true})");
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // ============== Pipeline Routes ==============

    // GET /api/pipelines/cameras - List cameras (for pipeline management)
    CROW_ROUTE(app, "/api/pipelines/cameras")
    ([]() {
        auto cameras = vision::CameraService::instance().getAllCameras();
        json result = json::array();
        for (const auto& cam : cameras) {
            result.push_back(cam.toJson());
        }
        return crow::response(200, "application/json", result.dump());
    });

    // GET /api/cameras/<id>/pipelines - Get pipelines for camera
    CROW_ROUTE(app, "/api/cameras/<int>/pipelines")
    ([](int cameraId) {
        auto pipelines = vision::PipelineService::instance().getPipelinesForCamera(cameraId);
        json result = json::array();
        for (const auto& p : pipelines) {
            result.push_back(p.toJson());
        }
        return crow::response(200, "application/json", result.dump());
    });

    // POST /api/cameras/<id>/pipelines - Create pipeline
    CROW_ROUTE(app, "/api/cameras/<int>/pipelines").methods("POST"_method)
    ([](const crow::request& req, int cameraId) {
        try {
            auto body = json::parse(req.body);

            vision::Pipeline pipeline;
            pipeline.name = body.at("name").get<std::string>();
            pipeline.pipeline_type = body.at("pipeline_type").get<vision::PipelineType>();
            pipeline.camera_id = cameraId;

            if (body.contains("config")) {
                pipeline.config = body["config"].dump();
            }

            auto created = vision::PipelineService::instance().createPipeline(pipeline);
            return crow::response(201, "application/json", created.toJson().dump());
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // PUT /api/pipelines/<id> - Update pipeline
    CROW_ROUTE(app, "/api/pipelines/<int>").methods("PUT"_method)
    ([](const crow::request& req, int id) {
        try {
            auto body = json::parse(req.body);

            auto existing = vision::PipelineService::instance().getPipelineById(id);
            if (!existing) {
                return crow::response(404, "application/json", R"({"error": "Pipeline not found"})");
            }

            if (body.contains("name")) {
                existing->name = body["name"].get<std::string>();
            }
            if (body.contains("pipeline_type")) {
                existing->pipeline_type = body["pipeline_type"].get<vision::PipelineType>();
            }

            if (vision::PipelineService::instance().updatePipeline(*existing)) {
                return crow::response(200, "application/json", existing->toJson().dump());
            } else {
                return crow::response(500, "application/json", R"({"error": "Failed to update pipeline"})");
            }
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // PUT /api/pipelines/<id>/config - Update pipeline config
    CROW_ROUTE(app, "/api/pipelines/<int>/config").methods("PUT"_method)
    ([](const crow::request& req, int id) {
        try {
            auto config = json::parse(req.body);

            if (vision::PipelineService::instance().updatePipelineConfig(id, config)) {
                return crow::response(200, "application/json", R"({"success": true})");
            } else {
                return crow::response(404, "application/json", R"({"error": "Pipeline not found"})");
            }
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // DELETE /api/pipelines/<id> - Delete pipeline
    CROW_ROUTE(app, "/api/pipelines/<int>").methods("DELETE"_method)
    ([](int id) {
        if (vision::PipelineService::instance().deletePipeline(id)) {
            return crow::response(200, "application/json", R"({"success": true})");
        } else {
            return crow::response(404, "application/json", R"({"error": "Pipeline not found"})");
        }
    });

    // ============== Settings Routes ==============

    // GET /api/settings - Get all settings
    CROW_ROUTE(app, "/api/settings")
    ([]() {
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

            return crow::response(200, "application/json", result.dump());
        } catch (const std::exception& e) {
            return crow::response(500, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // PUT /api/settings/global - Update global settings
    CROW_ROUTE(app, "/api/settings/global").methods("PUT"_method)
    ([](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            auto settings = vision::GlobalSettings::fromJson(body);
            auto& settingsService = vision::SettingsService::instance();
            auto currentSettings = settingsService.getGlobalSettings();

            std::string platform = vision::network::getPlatform();
            bool isLinux = (platform == "linux");

            // Validate hostname if it changed and we're on Linux
            if (isLinux && settings.hostname != currentSettings.hostname) {
                std::string hostnameError = vision::network::validateHostname(settings.hostname);
                if (!hostnameError.empty()) {
                    return crow::response(400, "application/json",
                        json{{"error", hostnameError}}.dump());
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
                        // Don't fail the request, just log warning
                    }
                }

                // Configure network interface if specified
                if (!settings.network_interface.empty()) {
                    if (settings.ip_mode == "static") {
                        // Apply static IP configuration
                        std::string staticIp = settings.static_ip;
                        std::string gateway = settings.gateway;
                        std::string subnetMask = settings.subnet_mask;

                        // Use defaults if not provided
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
                            // Don't fail the request, just log warning
                        }
                    } else if (settings.ip_mode == "dhcp") {
                        // Configure DHCP
                        if (!vision::network::setDHCP(settings.network_interface, error)) {
                            spdlog::warn("Failed to set DHCP: {}", error);
                            // Don't fail the request, just log warning
                        }
                    }
                }
            }

            return crow::response(200, "application/json", R"({"success": true})");
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // GET /api/settings/apriltag/fields - Get available fields
    CROW_ROUTE(app, "/api/settings/apriltag/fields")
    ([]() {
        auto fields = vision::SettingsService::instance().getAvailableFields();
        json result = json::array();
        for (const auto& f : fields) {
            result.push_back({
                {"name", f.name},
                {"is_system", f.is_system}
            });
        }
        return crow::response(200, "application/json", result.dump());
    });

    // PUT /api/settings/apriltag/select - Select field
    CROW_ROUTE(app, "/api/settings/apriltag/select").methods("PUT"_method)
    ([](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            std::string fieldName = body.at("field").get<std::string>();
            vision::SettingsService::instance().setSelectedField(fieldName);
            return crow::response(200, "application/json", R"({"success": true})");
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // POST /api/settings/control/factory-reset - Factory reset
    CROW_ROUTE(app, "/api/settings/control/factory-reset").methods("POST"_method)
    ([]() {
        vision::SettingsService::instance().factoryReset();
        return crow::response(200, "application/json", R"({"success": true})");
    });

    // POST /api/apriltag/upload - Upload custom field layout
    CROW_ROUTE(app, "/api/apriltag/upload").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            std::string name = body.at("name").get<std::string>();
            std::string content = body.at("content").get<std::string>();

            // Validate JSON content
            try {
                (void)json::parse(content);
            } catch (...) {
                return crow::response(400, "application/json", R"({"error": "Invalid JSON content"})");
            }

            if (vision::SettingsService::instance().addCustomField(name, content)) {
                return crow::response(200, "application/json", R"({"success": true})");
            } else {
                return crow::response(500, "application/json", R"({"error": "Failed to save field layout"})");
            }
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // POST /api/apriltag/delete - Delete custom field layout
    CROW_ROUTE(app, "/api/apriltag/delete").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            std::string name = body.at("name").get<std::string>();

            if (vision::SettingsService::instance().deleteField(name)) {
                return crow::response(200, "application/json", R"({"success": true})");
            } else {
                return crow::response(404, "application/json", R"({"error": "Field not found or cannot be deleted"})");
            }
        } catch (const std::exception& e) {
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // POST /api/control/restart-app - Restart application
    CROW_ROUTE(app, "/api/control/restart-app").methods("POST"_method)
    ([]() {
        spdlog::info("Application restart requested");
        // Note: Actual restart would require external process manager
        // For now, just signal that restart is requested
        return crow::response(200, "application/json", R"({"success": true, "message": "Restart requested"})");
    });

    // POST /api/control/reboot - Reboot device
    CROW_ROUTE(app, "/api/control/reboot").methods("POST"_method)
    ([]() {
        spdlog::info("Device reboot requested");
#ifdef __linux__
        // Only allow on Linux
        int result = system("sudo reboot");
        if (result == 0) {
            return crow::response(200, "application/json", R"({"success": true})");
        } else {
            return crow::response(500, "application/json", R"({"error": "Reboot command failed"})");
        }
#else
        return crow::response(400, "application/json", R"({"error": "Reboot only supported on Linux"})");
#endif
    });

    // ============== Metrics ==============

    // GET /api/metrics/summary - Get combined metrics snapshot
    CROW_ROUTE(app, "/api/metrics/summary")
    ([]() {
        auto summary = vision::MetricsRegistry::instance().getSummary();
        return crow::response(200, "application/json", summary.toJson().dump());
    });

    // GET /api/metrics/system - Get system metrics only
    CROW_ROUTE(app, "/api/metrics/system")
    ([]() {
        auto metrics = vision::MetricsRegistry::instance().getSystemMetrics();
        return crow::response(200, "application/json", metrics.toJson().dump());
    });

    // ============== Video Streaming ==============

    // GET /api/video_feed/<camera_id> - Raw camera MJPEG stream
    CROW_ROUTE(app, "/api/video_feed/<int>")
    ([](const crow::request& req, crow::response& res, int cameraId) {
        auto& tm = vision::ThreadManager::instance();

        // Start camera if not running
        if (!tm.isCameraRunning(cameraId)) {
            auto camera = vision::CameraService::instance().getCameraById(cameraId);
            if (!camera) {
                res.code = 404;
                res.set_header("Content-Type", "application/json");
                res.write(R"({"error": "Camera not found"})");
                res.end();
                return;
            }
            if (!tm.startCamera(*camera)) {
                res.code = 500;
                res.set_header("Content-Type", "application/json");
                res.write(R"({"error": "Failed to start camera - check connection and device availability"})");
                res.end();
                return;
            }
        }

        // Stream frames with timeout detection
        static constexpr auto FRAME_INTERVAL = std::chrono::milliseconds(33);  // ~30fps
        static constexpr int MAX_EMPTY_FRAMES = 300;  // ~10 seconds at 30fps
        int emptyFrameCount = 0;
        int totalFrameCount = 0;
        bool firstFrameSent = false;

        spdlog::info("Video feed streaming started for camera {}", cameraId);

        // Use chunked response for streaming
        res.set_header("Content-Type", "multipart/x-mixed-replace; boundary=frame");
        res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        res.set_header("Pragma", "no-cache");
        res.set_header("Expires", "0");
        res.set_header("Connection", "close");

        // Build complete response body with streaming frames
        std::ostringstream body;
        char headerBuffer[128];

        while (true) {
            auto frame = tm.getCameraFrame(cameraId);
            if (frame && !frame->empty()) {
                if (!firstFrameSent) {
                    spdlog::info("Video feed sending first frame for camera {}", cameraId);
                    firstFrameSent = true;
                }
                emptyFrameCount = 0;
                totalFrameCount++;
                const auto& jpeg = frame->getJpeg(85);
                if (!jpeg.empty()) {
                    // Build MJPEG frame
                    int headerLen = snprintf(headerBuffer, sizeof(headerBuffer),
                        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                        jpeg.size());

                    std::string frameData;
                    frameData.reserve(headerLen + jpeg.size() + 2);
                    frameData.append(headerBuffer, headerLen);
                    frameData.append(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                    frameData.append("\r\n");

                    // Write and flush immediately
                    try {
                        res.write(frameData);
                        // Force flush by ending and restarting - this is a workaround
                        // Actually, we need to check if write succeeded
                    } catch (...) {
                        spdlog::debug("Video feed client disconnected for camera {} (sent {} frames)", cameraId, totalFrameCount);
                        break;
                    }
                }
            } else {
                emptyFrameCount++;
                if (emptyFrameCount == 1) {
                    spdlog::debug("Video feed waiting for frames from camera {}", cameraId);
                }
                if (emptyFrameCount > MAX_EMPTY_FRAMES) {
                    spdlog::warn("Video feed timeout - no frames for camera {} after {} attempts (sent {} frames total)",
                        cameraId, MAX_EMPTY_FRAMES, totalFrameCount);
                    break;
                }
            }
            std::this_thread::sleep_for(FRAME_INTERVAL);
        }
        spdlog::info("Video feed streaming ended for camera {} (sent {} frames)", cameraId, totalFrameCount);
        res.end();
    });

    // GET /api/processed_video_feed/<pipeline_id> - Processed pipeline MJPEG stream
    CROW_ROUTE(app, "/api/processed_video_feed/<int>")
    ([](const crow::request& req, crow::response& res, int pipelineId) {
        auto& tm = vision::ThreadManager::instance();

        // Start pipeline if not running
        if (!tm.isPipelineRunning(pipelineId)) {
            auto pipeline = vision::PipelineService::instance().getPipelineById(pipelineId);
            if (!pipeline) {
                res.code = 404;
                res.set_header("Content-Type", "application/json");
                res.write(R"({"error": "Pipeline not found"})");
                res.end();
                return;
            }

            // Ensure camera is running
            int cameraId = pipeline->camera_id;
            if (!tm.isCameraRunning(cameraId)) {
                auto camera = vision::CameraService::instance().getCameraById(cameraId);
                if (!camera) {
                    res.code = 404;
                    res.set_header("Content-Type", "application/json");
                    res.write(R"({"error": "Camera not found for pipeline"})");
                    res.end();
                    return;
                }
                if (!tm.startCamera(*camera)) {
                    res.code = 500;
                    res.set_header("Content-Type", "application/json");
                    res.write(R"({"error": "Failed to start camera - check connection and device availability"})");
                    res.end();
                    return;
                }
            }

            if (!tm.startPipeline(*pipeline, cameraId)) {
                res.code = 500;
                res.set_header("Content-Type", "application/json");
                res.write(R"({"error": "Failed to start pipeline"})");
                res.end();
                return;
            }
        }

        res.set_header("Content-Type", "multipart/x-mixed-replace; boundary=frame");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "close");

        // Stream frames with timeout detection
        static constexpr auto FRAME_INTERVAL = std::chrono::milliseconds(33);  // ~30fps
        static constexpr int MAX_EMPTY_FRAMES = 300;  // ~10 seconds at 30fps
        char headerBuffer[128];
        int emptyFrameCount = 0;

        while (true) {
            auto frame = tm.getPipelineFrame(pipelineId);
            if (frame && !frame->empty()) {
                emptyFrameCount = 0;
                const auto& jpeg = frame->getJpeg(75);
                if (!jpeg.empty()) {
                    // Use snprintf for efficient header building
                    int headerLen = snprintf(headerBuffer, sizeof(headerBuffer),
                        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                        jpeg.size());

                    // Write frame data - check for write failures indicating client disconnect
                    try {
                        res.write(std::string(headerBuffer, headerLen));
                        res.write(std::string(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()));
                        res.write("\r\n");
                    } catch (...) {
                        // Client disconnected
                        spdlog::debug("Processed video feed client disconnected for pipeline {}", pipelineId);
                        break;
                    }
                }
            } else {
                emptyFrameCount++;
                if (emptyFrameCount > MAX_EMPTY_FRAMES) {
                    spdlog::warn("Processed video feed timeout - no frames for pipeline {} after {} attempts", pipelineId, MAX_EMPTY_FRAMES);
                    break;
                }
            }
            std::this_thread::sleep_for(FRAME_INTERVAL);
        }
        res.end();
    });

    // GET /api/cameras/results/<id> - Get pipeline results for camera
    CROW_ROUTE(app, "/api/cameras/results/<int>")
    ([](int cameraId) {
        auto results = vision::ThreadManager::instance().getCameraResults(cameraId);
        return crow::response(200, "application/json", results.dump());
    });

    // GET /api/cameras/status/<id> - Check camera connection status
    CROW_ROUTE(app, "/api/cameras/status/<int>")
    ([](int cameraId) {
        auto camera = vision::CameraService::instance().getCameraById(cameraId);
        if (!camera) {
            return crow::response(404, "application/json",
                json{{"error", "Camera not found"}}.dump());
        }

        bool running = vision::ThreadManager::instance().isCameraRunning(cameraId);

        // Check if physical camera is connected by querying device list
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
        }

        // Check if the camera's identifier matches any connected device
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
        return crow::response(200, "application/json", result.dump());
    });

    // ============== Health Check ==============

    CROW_ROUTE(app, "/")
    ([]() {
        return crow::response(200, "application/json", R"({"status": "ok", "version": "1.0.0"})");
    });

    CROW_ROUTE(app, "/health")
    ([]() {
        return crow::response(200, "application/json", R"({"status": "healthy"})");
    });

    // GET /api/network - Get network information
    CROW_ROUTE(app, "/api/network")
    ([]() {
        auto networkInfo = vision::network::getNetworkInfo();
        return crow::response(200, "application/json", networkInfo.toJson().dump());
    });

    // GET /api/system/platform - Get current platform
    CROW_ROUTE(app, "/api/system/platform")
    ([]() {
        json result = {
            {"platform", vision::network::getPlatform()}
        };
        return crow::response(200, "application/json", result.dump());
    });

    // GET /api/network/interfaces - Get list of network interfaces
    CROW_ROUTE(app, "/api/network/interfaces")
    ([]() {
        auto interfaces = vision::network::getNetworkInterfaces();
        json result = json::array();
        for (const auto& iface : interfaces) {
            result.push_back(iface);
        }
        return crow::response(200, "application/json", result.dump());
    });

    // GET /api/network/calculate-ip - Calculate static IP from team number
    CROW_ROUTE(app, "/api/network/calculate-ip")
    ([](const crow::request& req) {
        int teamNumber = 0;
        if (req.url_params.get("team")) {
            try {
                teamNumber = std::stoi(req.url_params.get("team"));
            } catch (...) {
                return crow::response(400, "application/json", R"({"error": "Invalid team number"})");
            }
        }

        json result = {
            {"static_ip", vision::network::calculateStaticIP(teamNumber)},
            {"gateway", vision::network::calculateDefaultGateway(teamNumber)},
            {"subnet_mask", "255.255.255.0"}
        };
        return crow::response(200, "application/json", result.dump());
    });

    // ============== Database Export/Import ==============

    // GET /api/database/export - Export SQLite database file
    CROW_ROUTE(app, "/api/database/export")
    ([&config]() {
        try {
            std::filesystem::path dbPath = config.database_path;

            // Check if database file exists
            if (!std::filesystem::exists(dbPath)) {
                return crow::response(500, "application/json",
                    R"({"error": "Database file not found"})");
            }

            // Read the database file
            std::ifstream file(dbPath, std::ios::binary);
            if (!file) {
                return crow::response(500, "application/json",
                    R"({"error": "Failed to read database file"})");
            }

            // Read file contents
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            crow::response res(200, content);
            res.set_header("Content-Type", "application/octet-stream");
            res.set_header("Content-Disposition", "attachment; filename=\"2852vision.db\"");
            return res;
        } catch (const std::exception& e) {
            return crow::response(500, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // POST /api/database/import - Import SQLite database file
    CROW_ROUTE(app, "/api/database/import").methods("POST"_method)
    ([&config](const crow::request& req) {
        try {
            std::filesystem::path dbPath = config.database_path;
            std::filesystem::path backupPath = dbPath.string() + ".backup";

            // Get the raw body data (the SQLite file)
            const std::string& fileData = req.body;

            if (fileData.empty()) {
                return crow::response(400, "application/json",
                    R"({"error": "No database file provided"})");
            }

            // Validate SQLite file header (first 16 bytes should start with "SQLite format 3")
            if (fileData.size() < 16 || fileData.substr(0, 15) != "SQLite format 3") {
                return crow::response(400, "application/json",
                    R"({"error": "Invalid SQLite database file"})");
            }

            // Create backup of current database
            if (std::filesystem::exists(dbPath)) {
                std::filesystem::copy_file(dbPath, backupPath,
                    std::filesystem::copy_options::overwrite_existing);
            }

            // Write the new database file
            std::ofstream outFile(dbPath, std::ios::binary | std::ios::trunc);
            if (!outFile) {
                return crow::response(500, "application/json",
                    R"({"error": "Failed to open database file for writing"})");
            }

            outFile.write(fileData.c_str(), fileData.size());
            outFile.close();

            if (!outFile.good()) {
                // Restore backup on failure
                if (std::filesystem::exists(backupPath)) {
                    std::filesystem::copy_file(backupPath, dbPath,
                        std::filesystem::copy_options::overwrite_existing);
                }
                return crow::response(500, "application/json",
                    R"({"error": "Failed to write database file"})");
            }

            // Reinitialize the database connection
            vision::Database::instance().initialize(config.database_path);

            // Clean up backup file on success
            if (std::filesystem::exists(backupPath)) {
                std::filesystem::remove(backupPath);
            }

            spdlog::info("Database imported successfully from uploaded file");
            return crow::response(200, "application/json", R"({"success": true})");
        } catch (const std::exception& e) {
            spdlog::error("Database import failed: {}", e.what());
            return crow::response(400, "application/json", json{{"error", e.what()}}.dump());
        }
    });

    // ============== Calibration Routes ==============
    vision::CalibrationService::registerRoutes(app);

    // Start server
    spdlog::info("Starting server on {}:{}", config.server.host, config.server.port);

    app.bindaddr(config.server.host)
       .port(config.server.port)
       .concurrency(config.server.threads)
       .run();

    // Shutdown threads on exit
    vision::ThreadManager::instance().shutdown();

    // Shutdown Spinnaker SDK
    vision::SpinnakerDriver::shutdown();

    return 0;
}