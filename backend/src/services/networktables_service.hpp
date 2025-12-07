#pragma once

#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <functional>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "utils/geometry.hpp"
#include <networktables/NetworkTableInstance.h>
#include <networktables/NetworkTable.h>
#include <networktables/DoubleTopic.h>
#include <networktables/StringTopic.h>
#include <networktables/BooleanTopic.h>
#include <networktables/DoubleArrayTopic.h>
#include <networktables/IntegerTopic.h>

namespace vision {

// Forward declaration
struct NTStatus;

// Callback type for status change notifications
using StatusCallback = std::function<void(const NTStatus&)>;

// Connection status for UI
struct NTStatus {
    bool connected = false;
    std::string serverAddress;
    int teamNumber = 0;
    std::string mode;  // "client", "server", or "disconnected"

    nlohmann::json toJson() const {
        return {
            {"connected", connected},
            {"serverAddress", serverAddress},
            {"teamNumber", teamNumber},
            {"mode", mode}
        };
    }
};

class NetworkTablesService {
public:
    // Singleton access
    static NetworkTablesService& instance();

    // Initialize as client connecting to roboRIO
    bool connect(int teamNumber);

    // Initialize as server (for testing without roboRIO)
    bool startServer(int port = 1735);

    // Disconnect from NetworkTables
    void disconnect();

    // Get connection status
    NTStatus getStatus() const;

    // Publish detection results for a camera
    void publishDetections(int cameraId, const nlohmann::json& detections);

    // Publish robot pose from multi-tag estimation
    void publishRobotPose(const Pose3d& pose, double timestamp, int tagsUsed);

    // Publish single tag pose
    void publishTagPose(int tagId, const Pose3d& pose, double timestamp);

    // Publish optical flow velocity for carpet odometry
    void publishOpticalFlowVelocity(double vx_mps, double vy_mps,
                                     int64_t timestamp_us, int features, bool valid);

    // Set whether to auto-publish (called by pipeline manager)
    void setAutoPublish(bool enabled) { autoPublish_.store(enabled, std::memory_order_release); }
    bool isAutoPublishing() const { return autoPublish_.load(std::memory_order_acquire); }

    // Status change notification system
    void registerStatusCallback(StatusCallback callback);
    void startStatusMonitor();
    void stopStatusMonitor();

private:
    NetworkTablesService() = default;

    // Thread-safe state variables
    std::atomic<bool> connected_{false};
    std::atomic<bool> autoPublish_{true};
    std::atomic<int> teamNumber_{0};

    // Protected by mutex_
    std::string serverAddress_;
    std::string mode_ = "disconnected";
    mutable std::mutex mutex_;

    nt::NetworkTableInstance ntInst_;
    std::shared_ptr<nt::NetworkTable> visionTable_;

    // Publishers for robot pose
    nt::DoubleArrayPublisher posePublisher_;
    nt::DoublePublisher poseTimestampPublisher_;
    nt::IntegerPublisher tagsUsedPublisher_;

    // Publishers for optical flow
    nt::DoubleArrayPublisher opticalFlowVelocityPublisher_;
    nt::IntegerPublisher opticalFlowTimestampPublisher_;
    nt::IntegerPublisher opticalFlowFeaturesPublisher_;
    nt::BooleanPublisher opticalFlowValidPublisher_;
    bool opticalFlowPublishersInitialized_ = false;

    // Cached publishers for detections and tag poses (protected by publisherMutex_)
    std::unordered_map<int, nt::StringPublisher> detectionPublishers_;
    std::unordered_map<int, nt::DoubleArrayPublisher> tagPosePublishers_;
    std::mutex publisherMutex_;

    // Helper to ensure table exists
    void ensureTable();

    // Status monitor members
    std::vector<StatusCallback> statusCallbacks_;
    std::mutex callbackMutex_;
    std::thread monitorThread_;
    std::atomic<bool> monitorRunning_{false};
    NTStatus lastStatus_;
};

} // namespace vision
