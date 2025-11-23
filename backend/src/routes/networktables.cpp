// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/networktables.hpp"
#include "services/networktables_service.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vision {

void NetworkTablesRoutes::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;

    // GET /api/networktables/status - Get connection status
    app.registerHandler(
        "/api/networktables/status",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto status = NetworkTablesService::instance().getStatus();
            nlohmann::json response = status.toJson();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(response.dump());
            callback(resp);
        },
        {Get});

    // POST /api/networktables/connect - Connect to team roboRIO
    app.registerHandler(
        "/api/networktables/connect",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = nlohmann::json::parse(req->getBody());
                int teamNumber = body.at("team_number").get<int>();

                if (teamNumber < 1 || teamNumber > 9999) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Invalid team number"})");
                    callback(resp);
                    return;
                }

                bool success = NetworkTablesService::instance().connect(teamNumber);

                if (success) {
                    auto status = NetworkTablesService::instance().getStatus();
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(status.toJson().dump());
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to connect to NetworkTables"})");
                    callback(resp);
                }
            } catch (const std::exception& e) {
                nlohmann::json error = {{"error", e.what()}};
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(error.dump());
                callback(resp);
            }
        },
        {Post});

    // POST /api/networktables/server - Start as NT server (for testing)
    app.registerHandler(
        "/api/networktables/server",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                int port = 1735;
                std::string body = std::string(req->getBody());
                if (!body.empty()) {
                    auto bodyJson = nlohmann::json::parse(body);
                    port = bodyJson.value("port", 1735);
                }

                bool success = NetworkTablesService::instance().startServer(port);

                if (success) {
                    auto status = NetworkTablesService::instance().getStatus();
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(status.toJson().dump());
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k500InternalServerError);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to start NetworkTables server"})");
                    callback(resp);
                }
            } catch (const std::exception& e) {
                nlohmann::json error = {{"error", e.what()}};
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(error.dump());
                callback(resp);
            }
        },
        {Post});

    // POST /api/networktables/disconnect - Disconnect from NetworkTables
    app.registerHandler(
        "/api/networktables/disconnect",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            NetworkTablesService::instance().disconnect();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"success": true})");
            callback(resp);
        },
        {Post});

    // PUT /api/networktables/autopublish - Set auto-publish setting
    app.registerHandler(
        "/api/networktables/autopublish",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = nlohmann::json::parse(req->getBody());
                bool enabled = body.at("enabled").get<bool>();

                NetworkTablesService::instance().setAutoPublish(enabled);

                nlohmann::json response = {
                    {"enabled", NetworkTablesService::instance().isAutoPublishing()}
                };
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(response.dump());
                callback(resp);
            } catch (const std::exception& e) {
                nlohmann::json error = {{"error", e.what()}};
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(error.dump());
                callback(resp);
            }
        },
        {Put});

    spdlog::info("NetworkTables routes registered");
}

} // namespace vision
