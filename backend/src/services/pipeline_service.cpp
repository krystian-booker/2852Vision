#include "services/pipeline_service.hpp"
#include "threads/thread_manager.hpp"
#include "core/database.hpp"
#include <spdlog/spdlog.h>

namespace vision {

PipelineService& PipelineService::instance() {
    static PipelineService instance;
    return instance;
}

std::vector<Pipeline> PipelineService::getAllPipelines() {
    auto& db = Database::instance();
    return db.withLock([](SQLite::Database& sqlDb) {
        std::vector<Pipeline> pipelines;
        SQLite::Statement query(sqlDb, "SELECT * FROM pipelines ORDER BY id");

        while (query.executeStep()) {
            pipelines.push_back(Pipeline::fromRow(query));
        }

        return pipelines;
    });
}

std::vector<Pipeline> PipelineService::getPipelinesForCamera(int cameraId) {
    auto& db = Database::instance();
    return db.withLock([cameraId](SQLite::Database& sqlDb) {
        std::vector<Pipeline> pipelines;
        SQLite::Statement query(sqlDb,
            "SELECT * FROM pipelines WHERE camera_id = ? ORDER BY id");
        query.bind(1, cameraId);

        while (query.executeStep()) {
            pipelines.push_back(Pipeline::fromRow(query));
        }

        return pipelines;
    });
}

std::optional<Pipeline> PipelineService::getPipelineById(int id) {
    auto& db = Database::instance();
    return db.withLock([id](SQLite::Database& sqlDb) -> std::optional<Pipeline> {
        SQLite::Statement query(sqlDb, "SELECT * FROM pipelines WHERE id = ?");
        query.bind(1, id);

        if (query.executeStep()) {
            return Pipeline::fromRow(query);
        }

        return std::nullopt;
    });
}

Pipeline PipelineService::createPipeline(Pipeline& pipeline) {
    auto& db = Database::instance();

    // Set default config if not provided
    if (pipeline.config.empty()) {
        pipeline.config = getDefaultConfig(pipeline.pipeline_type).dump();
    }

    std::string typeStr;
    switch (pipeline.pipeline_type) {
        case PipelineType::AprilTag: typeStr = "AprilTag"; break;
        case PipelineType::ColouredShape: typeStr = "Coloured Shape"; break;
        case PipelineType::ObjectDetectionML: typeStr = "Object Detection (ML)"; break;
    }

    return db.withLock([&pipeline, &typeStr](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            INSERT INTO pipelines (name, pipeline_type, config, camera_id)
            VALUES (?, ?, ?, ?)
        )");

        stmt.bind(1, pipeline.name);
        stmt.bind(2, typeStr);
        stmt.bind(3, pipeline.config);
        stmt.bind(4, pipeline.camera_id);
        stmt.exec();

        pipeline.id = static_cast<int>(sqlDb.getLastInsertRowid());
        spdlog::info("Created pipeline '{}' with id {} for camera {}",
                     pipeline.name, pipeline.id, pipeline.camera_id);

        return pipeline;
    });
}

bool PipelineService::updatePipeline(const Pipeline& pipeline) {
    auto& db = Database::instance();

    std::string typeStr;
    switch (pipeline.pipeline_type) {
        case PipelineType::AprilTag: typeStr = "AprilTag"; break;
        case PipelineType::ColouredShape: typeStr = "Coloured Shape"; break;
        case PipelineType::ObjectDetectionML: typeStr = "Object Detection (ML)"; break;
    }

    return db.withLock([&pipeline, &typeStr](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, R"(
            UPDATE pipelines SET
                name = ?, pipeline_type = ?, config = ?, camera_id = ?
            WHERE id = ?
        )");

        stmt.bind(1, pipeline.name);
        stmt.bind(2, typeStr);
        stmt.bind(3, pipeline.config);
        stmt.bind(4, pipeline.camera_id);
        stmt.bind(5, pipeline.id);

        return stmt.exec() > 0;
    });
}

bool PipelineService::updatePipelineConfig(int id, const nlohmann::json& config) {
    auto& db = Database::instance();
    return db.withLock([id, &config](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, "UPDATE pipelines SET config = ? WHERE id = ?");
        stmt.bind(1, config.dump());
        stmt.bind(2, id);

        bool success = stmt.exec() > 0;
        if (success) {
            spdlog::debug("Updated config for pipeline {}", id);
            // Propagate update to running thread
            ThreadManager::instance().updatePipelineConfig(id, config);
        }
        return success;
    });
}

bool PipelineService::deletePipeline(int id) {
    auto& db = Database::instance();
    return db.withLock([id](SQLite::Database& sqlDb) {
        SQLite::Statement stmt(sqlDb, "DELETE FROM pipelines WHERE id = ?");
        stmt.bind(1, id);

        bool success = stmt.exec() > 0;
        if (success) {
            spdlog::info("Deleted pipeline with id {}", id);
        }
        return success;
    });
}

nlohmann::json PipelineService::getDefaultConfig(PipelineType type) {
    switch (type) {
        case PipelineType::AprilTag:
            return AprilTagConfig().toJson();

        case PipelineType::ColouredShape:
            return ColouredShapeConfig().toJson();

        case PipelineType::ObjectDetectionML:
            return ObjectDetectionMLConfig().toJson();

        default:
            return nlohmann::json::object();
    }
}

} // namespace vision
