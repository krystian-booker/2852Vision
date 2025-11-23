#include "services/networktables_service.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace vision {

NetworkTablesService& NetworkTablesService::instance() {
    static NetworkTablesService instance;
    return instance;
}

bool NetworkTablesService::connect(int teamNumber) {
    if (connected_.load(std::memory_order_acquire)) {
        disconnect();
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        teamNumber_.store(teamNumber, std::memory_order_release);

        // Get the default instance
        ntInst_ = nt::NetworkTableInstance::GetDefault();

        // Set identity
        ntInst_.SetServerTeam(teamNumber);
        ntInst_.StartClient4("2852Vision");

        // Build server address from team number (10.TE.AM.2)
        int te = teamNumber / 100;
        int am = teamNumber % 100;
        std::ostringstream ss;
        ss << "10." << te << "." << am << ".2";
        serverAddress_ = ss.str();

        mode_ = "client";
        connected_.store(true, std::memory_order_release);

        ensureTable();

        spdlog::info("NetworkTables connecting to team {} at {}", teamNumber, serverAddress_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to connect to NetworkTables: {}", e.what());
        return false;
    }
}

bool NetworkTablesService::startServer(int port) {
    if (connected_.load(std::memory_order_acquire)) {
        disconnect();
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        ntInst_ = nt::NetworkTableInstance::GetDefault();
        ntInst_.SetServer("", port);
        ntInst_.StartServer();

        mode_ = "server";
        connected_.store(true, std::memory_order_release);
        serverAddress_ = "localhost:" + std::to_string(port);

        ensureTable();

        spdlog::info("NetworkTables server started on port {}", port);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to start NetworkTables server: {}", e.what());
        return false;
    }
}

void NetworkTablesService::disconnect() {
    if (connected_.load(std::memory_order_acquire)) {
        try {
            ntInst_.StopClient();
            ntInst_.StopServer();
        } catch (...) {
            // Ignore errors during disconnect
            spdlog::debug("Exception during NetworkTables disconnect (ignored)");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            mode_ = "disconnected";
            visionTable_.reset();
        }

        {
            std::lock_guard<std::mutex> lock(publisherMutex_);
            detectionPublishers_.clear();
            tagPosePublishers_.clear();
        }

        connected_.store(false, std::memory_order_release);
        spdlog::info("NetworkTables disconnected");
    }
}

NTStatus NetworkTablesService::getStatus() const {
    NTStatus status;
    status.connected = connected_.load(std::memory_order_acquire) && ntInst_.IsConnected();
    status.teamNumber = teamNumber_.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status.serverAddress = serverAddress_;
        status.mode = mode_;
    }

    return status;
}

void NetworkTablesService::ensureTable() {
    if (!visionTable_) {
        visionTable_ = ntInst_.GetTable("Vision");

        // Create publishers
        posePublisher_ = visionTable_->GetDoubleArrayTopic("robotPose").Publish();
        poseTimestampPublisher_ = visionTable_->GetDoubleTopic("poseTimestamp").Publish();
        tagsUsedPublisher_ = visionTable_->GetIntegerTopic("tagsUsed").Publish();
    }
}

void NetworkTablesService::publishDetections(int cameraId, const nlohmann::json& detections) {
    if (!connected_.load(std::memory_order_acquire) || !autoPublish_.load(std::memory_order_acquire)) return;

    try {
        ensureTable();

        std::lock_guard<std::mutex> lock(publisherMutex_);
        // Use cached publisher or create new one
        auto it = detectionPublishers_.find(cameraId);
        if (it == detectionPublishers_.end()) {
            std::string key = "camera" + std::to_string(cameraId) + "/detections";
            auto publisher = visionTable_->GetStringTopic(key).Publish();
            it = detectionPublishers_.emplace(cameraId, std::move(publisher)).first;
        }

        it->second.Set(detections.dump());
    } catch (const std::exception& e) {
        spdlog::warn("Failed to publish detections: {}", e.what());
    }
}

void NetworkTablesService::publishRobotPose(const Pose3d& pose, double timestamp, int tagsUsed) {
    if (!connected_.load(std::memory_order_acquire) || !autoPublish_.load(std::memory_order_acquire)) return;

    try {
        ensureTable();

        // Publish pose as array [x, y, z, qw, qx, qy, qz]
        auto q = pose.rotation.toQuaternion();
        std::vector<double> poseArray = {
            pose.translation.x,
            pose.translation.y,
            pose.translation.z,
            q.w, q.x, q.y, q.z
        };

        posePublisher_.Set(poseArray);
        poseTimestampPublisher_.Set(timestamp);
        tagsUsedPublisher_.Set(tagsUsed);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to publish robot pose: {}", e.what());
    }
}

void NetworkTablesService::publishTagPose(int tagId, const Pose3d& pose, double timestamp) {
    if (!connected_.load(std::memory_order_acquire) || !autoPublish_.load(std::memory_order_acquire)) return;

    try {
        ensureTable();

        std::lock_guard<std::mutex> lock(publisherMutex_);
        // Use cached publisher or create new one
        auto it = tagPosePublishers_.find(tagId);
        if (it == tagPosePublishers_.end()) {
            std::string key = "tag" + std::to_string(tagId) + "/pose";
            auto publisher = visionTable_->GetDoubleArrayTopic(key).Publish();
            it = tagPosePublishers_.emplace(tagId, std::move(publisher)).first;
        }

        auto q = pose.rotation.toQuaternion();
        std::vector<double> poseArray = {
            pose.translation.x,
            pose.translation.y,
            pose.translation.z,
            q.w, q.x, q.y, q.z
        };

        it->second.Set(poseArray);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to publish tag pose: {}", e.what());
    }
}

} // namespace vision
