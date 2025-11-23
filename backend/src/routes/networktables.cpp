#include "routes/networktables.hpp"
#include "services/networktables_service.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace vision {

void NetworkTablesRoutes::registerRoutes(crow::SimpleApp& app) {
    // GET /api/networktables/status - Get connection status
    CROW_ROUTE(app, "/api/networktables/status")
    ([]() {
        auto status = NetworkTablesService::instance().getStatus();
        nlohmann::json response = status.toJson();
        return crow::response(200, "application/json", response.dump());
    });

    // POST /api/networktables/connect - Connect to team roboRIO
    CROW_ROUTE(app, "/api/networktables/connect").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            int teamNumber = body.at("team_number").get<int>();

            if (teamNumber < 1 || teamNumber > 9999) {
                return crow::response(400, "application/json", R"({"error": "Invalid team number"})");
            }

            bool success = NetworkTablesService::instance().connect(teamNumber);

            if (success) {
                auto status = NetworkTablesService::instance().getStatus();
                return crow::response(200, "application/json", status.toJson().dump());
            } else {
                return crow::response(500, "application/json", R"({"error": "Failed to connect to NetworkTables"})");
            }
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            return crow::response(400, "application/json", error.dump());
        }
    });

    // POST /api/networktables/server - Start as NT server (for testing)
    CROW_ROUTE(app, "/api/networktables/server").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            int port = 1735;
            if (!req.body.empty()) {
                auto body = nlohmann::json::parse(req.body);
                port = body.value("port", 1735);
            }

            bool success = NetworkTablesService::instance().startServer(port);

            if (success) {
                auto status = NetworkTablesService::instance().getStatus();
                return crow::response(200, "application/json", status.toJson().dump());
            } else {
                return crow::response(500, "application/json", R"({"error": "Failed to start NetworkTables server"})");
            }
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            return crow::response(400, "application/json", error.dump());
        }
    });

    // POST /api/networktables/disconnect - Disconnect from NetworkTables
    CROW_ROUTE(app, "/api/networktables/disconnect").methods("POST"_method)
    ([]() {
        NetworkTablesService::instance().disconnect();
        return crow::response(200, "application/json", R"({"success": true})");
    });

    // PUT /api/networktables/autopublish - Set auto-publish setting
    CROW_ROUTE(app, "/api/networktables/autopublish").methods("PUT"_method)
    ([](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            bool enabled = body.at("enabled").get<bool>();

            NetworkTablesService::instance().setAutoPublish(enabled);

            nlohmann::json response = {
                {"enabled", NetworkTablesService::instance().isAutoPublishing()}
            };
            return crow::response(200, "application/json", response.dump());
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            return crow::response(400, "application/json", error.dump());
        }
    });

    spdlog::info("NetworkTables routes registered");
}

} // namespace vision
