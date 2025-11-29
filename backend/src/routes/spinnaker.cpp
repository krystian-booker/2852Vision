// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/spinnaker.hpp"
#include "services/camera_service.hpp"
#include "drivers/spinnaker_driver.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vision {

void SpinnakerController::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;
    using json = nlohmann::json;

    // GET /api/cameras/spinnaker/nodes/{id} - Get camera node map
    app.registerHandler(
        "/api/cameras/spinnaker/nodes/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            auto camera = CameraService::instance().getCameraById(cameraId);
            if (!camera) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k404NotFound);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Camera not found"})");
                callback(resp);
                return;
            }

            if (camera->camera_type != CameraType::Spinnaker) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Camera is not a Spinnaker/Spinnaker device"})");
                callback(resp);
                return;
            }

            if (!SpinnakerDriver::isAvailable()) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Spinnaker support not compiled in"})");
                callback(resp);
                return;
            }

            auto [nodes, error] = SpinnakerDriver::getNodeMap(camera->identifier);
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
    app.registerHandler(
        "/api/cameras/spinnaker/nodes/{id}",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback,
           int cameraId) {
            try {
                auto camera = CameraService::instance().getCameraById(cameraId);
                if (!camera) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Camera not found"})");
                    callback(resp);
                    return;
                }

                if (camera->camera_type != CameraType::Spinnaker) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Camera is not a Spinnaker/Spinnaker device"})");
                    callback(resp);
                    return;
                }

                if (!SpinnakerDriver::isAvailable()) {
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
                    SpinnakerDriver::updateNode(camera->identifier, nodeName, value);

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
    app.registerHandler(
        "/api/spinnaker/status",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            json result = {
                {"available", SpinnakerDriver::isAvailable()},
                {"sdk", "Spinnaker"}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    spdlog::info("Spinnaker routes registered");
}

} // namespace vision
