// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/system.hpp"
#include "metrics/registry.hpp"
#include "utils/network_utils.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vision {

void SystemController::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;
    using json = nlohmann::json;

    // GET / - Root health check
    app.registerHandler(
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

    // GET /health - Health status
    app.registerHandler(
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

    // POST /api/control/restart-app - Restart application
    app.registerHandler(
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
    app.registerHandler(
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
#elif defined(__APPLE__)
            int result = system("sudo shutdown -r now");
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
                resp->setBody(R"({"error": "Reboot command failed (may need sudo)"})");
                callback(resp);
            }
#else
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"error": "Reboot only supported on Linux and macOS"})");
            callback(resp);
#endif
        },
        {Post});

    // GET /api/metrics/summary - Get combined metrics snapshot
    app.registerHandler(
        "/api/metrics/summary",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto summary = MetricsRegistry::instance().getSummary();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(summary.toJson().dump());
            callback(resp);
        },
        {Get});

    // GET /api/metrics/system - Get system metrics only
    app.registerHandler(
        "/api/metrics/system",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto metrics = MetricsRegistry::instance().getSystemMetrics();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(metrics.toJson().dump());
            callback(resp);
        },
        {Get});

    // GET /api/network - Get network information
    app.registerHandler(
        "/api/network",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto networkInfo = network::getNetworkInfo();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(networkInfo.toJson().dump());
            callback(resp);
        },
        {Get});

    // GET /api/system/platform - Get current platform
    app.registerHandler(
        "/api/system/platform",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            json result = {
                {"platform", network::getPlatform()}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    // GET /api/network/interfaces - Get list of network interfaces
    app.registerHandler(
        "/api/network/interfaces",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto interfaces = network::getNetworkInterfaces();
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
    app.registerHandler(
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
                {"static_ip", network::calculateStaticIP(teamNumber)},
                {"gateway", network::calculateDefaultGateway(teamNumber)},
                {"subnet_mask", "255.255.255.0"}
            };
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(result.dump());
            callback(resp);
        },
        {Get});

    spdlog::info("System routes registered");
}

} // namespace vision
