// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/pipelines.hpp"
#include "services/camera_service.hpp"
#include "services/pipeline_service.hpp"
#include "threads/thread_manager.hpp"
#include "hw/accel.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace vision {

void PipelinesController::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;
    using json = nlohmann::json;

    // GET /api/pipelines/ml/availability - Get ML runtime availability
    app.registerHandler(
        "/api/pipelines/ml/availability",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto availability = hw::getMLAvailability();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(availability.dump());
            callback(resp);
        },
        {Get});

    // GET /api/pipelines/{id}/labels - Get pipeline labels
    app.registerHandler(
        "/api/pipelines/{id}/labels",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int pipelineId) {
            auto pipeline = PipelineService::instance().getPipelineById(pipelineId);
            if (!pipeline) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Pipeline not found"})");
                callback(resp);
                return;
            }

            if (pipeline->pipeline_type != PipelineType::ObjectDetectionML) {
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
    app.registerHandler(
        "/api/pipelines/{id}/files",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int pipelineId) {
            auto pipeline = PipelineService::instance().getPipelineById(pipelineId);
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
                PipelineService::instance().updatePipeline(*pipeline);

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
    app.registerHandler(
        "/api/pipelines/{id}/files",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int pipelineId) {
            auto pipeline = PipelineService::instance().getPipelineById(pipelineId);
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
                PipelineService::instance().updatePipeline(*pipeline);

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

    // GET /api/pipelines/cameras - List cameras (for pipeline management)
    app.registerHandler(
        "/api/pipelines/cameras",
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

    // GET /api/cameras/{id}/pipelines - Get pipelines for camera
    app.registerHandler(
        "/api/cameras/{id}/pipelines",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto pipelines = PipelineService::instance().getPipelinesForCamera(cameraId);
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
    app.registerHandler(
        "/api/cameras/{id}/pipelines",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            try {
                auto body = json::parse(req->getBody());

                Pipeline pipeline;
                pipeline.name = body.at("name").get<std::string>();
                pipeline.pipeline_type = body.at("pipeline_type").get<PipelineType>();
                pipeline.camera_id = cameraId;

                if (body.contains("config")) {
                    pipeline.config = body["config"].dump();
                }

                auto created = PipelineService::instance().createPipeline(pipeline);

                // Ensure owning camera thread is running and start this pipeline thread immediately
                auto camera = CameraService::instance().getCameraById(cameraId);
                if (camera) {
                    if (!ThreadManager::instance().isCameraRunning(cameraId)) {
                        ThreadManager::instance().startCamera(*camera);
                    }
                    ThreadManager::instance().startPipeline(created, cameraId);
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
    app.registerHandler(
        "/api/pipelines/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto body = json::parse(req->getBody());

                auto existing = PipelineService::instance().getPipelineById(id);
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
                    existing->pipeline_type = body["pipeline_type"].get<PipelineType>();
                }

                if (PipelineService::instance().updatePipeline(*existing)) {
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
    app.registerHandler(
        "/api/pipelines/{id}/config",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            try {
                auto config = json::parse(req->getBody());

                if (PipelineService::instance().updatePipelineConfig(id, config)) {
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
    app.registerHandler(
        "/api/pipelines/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int id) {
            // Stop processing thread first
            ThreadManager::instance().stopPipeline(id);

            if (PipelineService::instance().deletePipeline(id)) {
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

    spdlog::info("Pipeline routes registered");
}

} // namespace vision
