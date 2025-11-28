#pragma once

#include "models/camera.hpp"
#include "models/pipeline.hpp"
#include "drivers/base_driver.hpp"
#include "pipelines/base_pipeline.hpp"
#include "utils/frame_buffer.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <memory>

namespace vision {

// Forward declarations
class CameraThread;
class VisionThread;

// Queued frame for vision processing
struct QueuedFrame {
    FramePtr frame;
    std::chrono::steady_clock::time_point queueTime;
};

// Thread-safe frame queue
class FrameQueue {
public:
    explicit FrameQueue(size_t maxSize = 2);

    bool push(FramePtr frame);  // Returns false if queue is full (frame dropped)
    bool pop(QueuedFrame& out, std::chrono::milliseconds timeout);
    void clear();
    size_t size() const;
    bool empty() const;

private:
    std::queue<QueuedFrame> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t maxSize_;
};

// Camera acquisition thread
class CameraThread {
public:
    CameraThread(const Camera& camera, std::unique_ptr<BaseDriver> driver);
    ~CameraThread();

    // Delete copy and move operations (class contains std::thread which cannot be safely moved while running)
    CameraThread(const CameraThread&) = delete;
    CameraThread& operator=(const CameraThread&) = delete;
    CameraThread(CameraThread&&) = delete;
    CameraThread& operator=(CameraThread&&) = delete;

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Register a vision thread's queue
    void registerQueue(int pipelineId, std::shared_ptr<FrameQueue> queue);
    void unregisterQueue(int pipelineId);

    // Get latest display frame (for raw video feed)
    FramePtr getDisplayFrame();

    // Get camera ID
    int cameraId() const { return camera_.id; }

    // Get camera object
    const Camera& getCamera() const { return camera_; }

private:
    void run();
    void applyOrientation(cv::Mat& frame);

    Camera camera_;
    std::unique_ptr<BaseDriver> driver_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // Subscribed vision threads
    std::unordered_map<int, std::shared_ptr<FrameQueue>> queues_;
    std::mutex queuesMutex_;

    // Latest frame for display
    FramePtr displayFrame_;
    std::mutex displayMutex_;

    // Frame counter
    uint64_t frameSequence_ = 0;
};

// Vision processing thread
class VisionThread {
public:
    VisionThread(const Pipeline& pipeline, std::unique_ptr<BasePipeline> processor);
    ~VisionThread();

    // Delete copy and move operations (class contains std::thread which cannot be safely moved while running)
    VisionThread(const VisionThread&) = delete;
    VisionThread& operator=(const VisionThread&) = delete;
    VisionThread(VisionThread&&) = delete;
    VisionThread& operator=(VisionThread&&) = delete;

    void start(std::shared_ptr<FrameQueue> inputQueue);
    void stop();
    bool isRunning() const { return running_.load(); }

    // Get latest processed frame
    FramePtr getProcessedFrame();

    // Get latest results
    nlohmann::json getLatestResults();

    // Get pipeline ID
    int pipelineId() const { return pipeline_.id; }

    // Update pipeline config
    void updateConfig(const nlohmann::json& config);

    // Get underlying processor
    BasePipeline* getProcessor() { return processor_.get(); }

private:
    void run();

    Pipeline pipeline_;
    std::unique_ptr<BasePipeline> processor_;
    std::shared_ptr<FrameQueue> inputQueue_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // Latest processed frame
    FramePtr processedFrame_;
    std::mutex frameMutex_;

    // Latest results
    nlohmann::json latestResults_;
    std::mutex resultsMutex_;
};

// Thread manager singleton
class ThreadManager {
public:
    static ThreadManager& instance();

    // Start camera thread
    bool startCamera(const Camera& camera);
    void stopCamera(int cameraId);
    bool isCameraRunning(int cameraId);

    // Restart camera with new settings (if running)
    void restartCamera(const Camera& newCamera);

    // Execute an action with the camera temporarily paused
    void executeWithCameraPaused(int cameraId, std::function<void()> action);

    // Start/stop pipeline thread
    bool startPipeline(const Pipeline& pipeline, int cameraId);
    void stopPipeline(int pipelineId);
    bool isPipelineRunning(int pipelineId);

    // Update calibration for all pipelines associated with a camera
    void updateCalibration(int cameraId, const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs);

    // Get frames for streaming
    FramePtr getCameraFrame(int cameraId);
    FramePtr getPipelineFrame(int pipelineId);

    // Get pipeline results
    nlohmann::json getPipelineResults(int pipelineId);

    // Get all results for a camera
    nlohmann::json getCameraResults(int cameraId);

    // Shutdown all threads
    void shutdown();

private:
    ThreadManager() = default;

    std::unordered_map<int, std::unique_ptr<CameraThread>> cameraThreads_;
    std::unordered_map<int, std::unique_ptr<VisionThread>> visionThreads_;
    std::unordered_map<int, std::shared_ptr<FrameQueue>> pipelineQueues_;
    std::unordered_map<int, int> pipelineToCameraMap_;  // pipeline_id -> camera_id

    std::mutex mutex_;
};

} // namespace vision
