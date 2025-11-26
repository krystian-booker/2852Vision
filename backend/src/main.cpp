// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/database.hpp"
#include "services/camera_service.hpp"
#include "services/pipeline_service.hpp"
#include "services/streamer_service.hpp"
#include "drivers/spinnaker_driver.hpp"
#include "threads/thread_manager.hpp"

// Route controllers
#include "routes/cameras.hpp"
#include "routes/pipelines.hpp"
#include "routes/spinnaker.hpp"
#include "routes/settings.hpp"
#include "routes/system.hpp"
#include "routes/database.hpp"
#include "routes/calibration.hpp"
#include "routes/networktables.hpp"

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

    // Register all route controllers
    vision::CamerasController::registerRoutes(app());
    vision::PipelinesController::registerRoutes(app());
    vision::SpinnakerController::registerRoutes(app());
    vision::SettingsController::registerRoutes(app());
    vision::SystemController::registerRoutes(app());
    vision::DatabaseController::registerRoutes(app(), config.database_path);
    vision::CalibrationService::registerRoutes(app());
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
