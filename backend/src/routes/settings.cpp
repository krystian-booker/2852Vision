// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/settings.hpp"
#include "services/settings_service.hpp"
#include "services/networktables_service.hpp"
#include "drivers/spinnaker_driver.hpp"
#include "utils/network_utils.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vision {

void SettingsController::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;
    using json = nlohmann::json;

    // GET /api/settings - Get all settings
    app.registerHandler(
        "/api/settings",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto& settingsService = SettingsService::instance();

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
                result["spinnaker_available"] = SpinnakerDriver::isAvailable();

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
    app.registerHandler(
        "/api/settings/global",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                auto settings = GlobalSettings::fromJson(body);
                auto& settingsService = SettingsService::instance();
                auto currentSettings = settingsService.getGlobalSettings();

                std::string platform = network::getPlatform();
                bool isLinux = (platform == "linux");

                // Validate hostname if it changed and we're on Linux
                if (isLinux && settings.hostname != currentSettings.hostname) {
                    std::string hostnameError = network::validateHostname(settings.hostname);
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

                // Reconnect NetworkTables if team number changed
                if (settings.team_number != currentSettings.team_number && settings.team_number > 0) {
                    spdlog::info("Team number changed from {} to {}, reconnecting NetworkTables",
                        currentSettings.team_number, settings.team_number);
                    NetworkTablesService::instance().connect(settings.team_number);
                }

                // Apply network configuration on Linux only
                if (isLinux) {
                    std::string error;

                    // Set hostname if changed
                    if (settings.hostname != currentSettings.hostname) {
                        if (!network::setHostname(settings.hostname, error)) {
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
                                staticIp = network::calculateStaticIP(settings.team_number);
                            }
                            if (gateway.empty()) {
                                gateway = network::calculateDefaultGateway(settings.team_number);
                            }
                            if (subnetMask.empty()) {
                                subnetMask = "255.255.255.0";
                            }

                            if (!network::setStaticIP(settings.network_interface, staticIp, gateway, subnetMask, error)) {
                                spdlog::warn("Failed to set static IP: {}", error);
                            }
                        } else if (settings.ip_mode == "dhcp") {
                            if (!network::setDHCP(settings.network_interface, error)) {
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
    app.registerHandler(
        "/api/settings/apriltag/fields",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto fields = SettingsService::instance().getAvailableFields();
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
    app.registerHandler(
        "/api/settings/apriltag/select",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                std::string fieldName = body.at("field").get<std::string>();
                SettingsService::instance().setSelectedField(fieldName);
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
    app.registerHandler(
        "/api/settings/control/factory-reset",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            SettingsService::instance().factoryReset();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(R"({"success": true})");
            callback(resp);
        },
        {Post});

    // POST /api/apriltag/upload - Upload custom field layout
    app.registerHandler(
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

                if (SettingsService::instance().addCustomField(name, content)) {
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
    app.registerHandler(
        "/api/apriltag/delete",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = json::parse(req->getBody());
                std::string name = body.at("name").get<std::string>();

                if (SettingsService::instance().deleteField(name)) {
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

    spdlog::info("Settings routes registered");
}

} // namespace vision
