// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/database.hpp"
#include "core/database.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vision {

void DatabaseController::registerRoutes(drogon::HttpAppFramework& app, const std::string& databasePath) {
    using namespace drogon;
    using json = nlohmann::json;

    // GET /api/database/export - Export SQLite database file
    app.registerHandler(
        "/api/database/export",
        [databasePath](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                std::filesystem::path dbPath = databasePath;

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
    app.registerHandler(
        "/api/database/import",
        [databasePath](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                std::filesystem::path dbPath = databasePath;
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

                Database::instance().initialize(databasePath);

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

    spdlog::info("Database routes registered");
}

} // namespace vision
