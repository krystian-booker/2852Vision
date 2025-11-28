#include "threads/thread_manager.hpp"
#include "services/pipeline_service.hpp"
#include "services/streamer_service.hpp"
#include <spdlog/spdlog.h>

namespace vision {

// ============== FrameQueue ==============

FrameQueue::FrameQueue(size_t maxSize) : maxSize_(maxSize) {}

bool FrameQueue::push(FramePtr frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.size() >= maxSize_) {
        // Drop oldest frame
        queue_.pop();
    }

    QueuedFrame qf;
    qf.frame = frame;
    qf.queueTime = std::chrono::steady_clock::now();
    queue_.push(qf);

    cv_.notify_one();
    return true;
}

bool FrameQueue::pop(QueuedFrame& out, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
        return false;
    }

    out = queue_.front();
    queue_.pop();
    return true;
}

void FrameQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

size_t FrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool FrameQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

// ============== CameraThread ==============

CameraThread::CameraThread(const Camera& camera, std::unique_ptr<BaseDriver> driver)
    : camera_(camera)
    , driver_(std::move(driver)) {
}

CameraThread::~CameraThread() {
    stop();
}

bool CameraThread::start() {
    if (running_.load()) return true;

    // Don't fail if connect fails here - let the run loop handle retries
    if (!driver_->connect()) {
        spdlog::warn("Initial connection to camera {} failed - will retry in run loop", camera_.id);
    } else {
        spdlog::info("Initial connection to camera {} successful", camera_.id);
    }

    running_ = true;
    thread_ = std::thread(&CameraThread::run, this);
    spdlog::info("Camera thread started for camera {}", camera_.id);
    return true;
}

void CameraThread::stop() {
    if (!running_.load()) return;

    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }

    driver_->disconnect();
    spdlog::info("Camera thread stopped for camera {}", camera_.id);
}

void CameraThread::registerQueue(int pipelineId, std::shared_ptr<FrameQueue> queue) {
    std::lock_guard<std::mutex> lock(queuesMutex_);
    queues_[pipelineId] = queue;
}

void CameraThread::unregisterQueue(int pipelineId) {
    std::lock_guard<std::mutex> lock(queuesMutex_);
    queues_.erase(pipelineId);
}

FramePtr CameraThread::getDisplayFrame() {
    std::lock_guard<std::mutex> lock(displayMutex_);
    return displayFrame_;
}

void CameraThread::run() {
    spdlog::info("Camera thread run loop started for camera {}", camera_.id);

    auto startTime = std::chrono::steady_clock::now();
    bool firstFrameReceived = false;
    int emptyFrameCount = 0;
    int totalFrameCount = 0;
    static constexpr int INITIAL_FRAME_TIMEOUT_MS = 5000;  // 5 seconds to get first frame
    static constexpr int LOG_INTERVAL = 100;  // Log every 100 frames

    bool connectionErrorLogged = false;

    while (running_.load()) {
        // Try to connect if not connected
        if (!driver_->isConnected()) {
             if (driver_->connect(connectionErrorLogged)) {
                 spdlog::info("Connected to camera {}", camera_.id);
                 connectionErrorLogged = false;
             } else {
                 if (!connectionErrorLogged) {
                     // First failure is already logged by driver (silent=false)
                     connectionErrorLogged = true;
                 }
                 
                 // Publish placeholder while connecting
                 if (totalFrameCount % 10 == 0) { // Limit frequency
                     cv::Mat placeholder = cv::Mat::zeros(480, 640, CV_8UC3);
                     cv::putText(placeholder, "Camera Connecting...", cv::Point(160, 240), 
                         cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 255), 2);
                     StreamerService::instance().publishFrame(
                         "/camera/" + std::to_string(camera_.id),
                         placeholder
                     );
                 }
                 std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                 totalFrameCount++; // Increment to trigger placeholder logic
                 continue;
             }
        }

        auto frameResult = driver_->getFrame();

        if (frameResult.empty()) {
            emptyFrameCount++;

            // Check for initial frame timeout
            if (!firstFrameReceived) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                
                // Instead of stopping, just log and publish a placeholder every second
                if (elapsed > INITIAL_FRAME_TIMEOUT_MS && emptyFrameCount % 100 == 0) {
                    spdlog::warn("Camera {} waiting for first frame... ({} empty frames, {}ms elapsed)",
                        camera_.id, emptyFrameCount, elapsed);
                    
                    // Publish placeholder to keep stream alive
                    cv::Mat placeholder = cv::Mat::zeros(480, 640, CV_8UC3);
                    cv::putText(placeholder, "Waiting for frames...", cv::Point(160, 240), 
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 255), 2);
                    StreamerService::instance().publishFrame(
                        "/camera/" + std::to_string(camera_.id),
                        placeholder
                    );
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // First frame received
        if (!firstFrameReceived) {
            firstFrameReceived = true;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            spdlog::info("Camera {} received first frame after {}ms", camera_.id, elapsed);
        }

        totalFrameCount++;

        // Periodic logging
        if (totalFrameCount % LOG_INTERVAL == 0) {
            spdlog::debug("Camera {} frame count: {}, empty frames: {}",
                camera_.id, totalFrameCount, emptyFrameCount);
        }

        // Apply orientation
        applyOrientation(frameResult.color);

        // Create frame with timestamp
        auto frame = std::make_shared<RefCountedFrame>(
            frameResult.color,
            frameResult.depth
        );
        frame->setSequence(++frameSequence_);

        // Update display frame
        {
            std::lock_guard<std::mutex> lock(displayMutex_);
            displayFrame_ = frame;
        }

        // Publish to MJPEG streamer
        StreamerService::instance().publishFrame(
            "/camera/" + std::to_string(camera_.id),
            frame->color()
        );

        // Distribute to vision threads
        {
            std::lock_guard<std::mutex> lock(queuesMutex_);
            for (auto& [pipelineId, queue] : queues_) {
                queue->push(frame);
            }
        }
    }

    spdlog::info("Camera thread run loop ended for camera {} (total frames: {}, empty: {})",
        camera_.id, totalFrameCount, emptyFrameCount);
}

void CameraThread::applyOrientation(cv::Mat& frame) {
    switch (camera_.orientation) {
        case 90:
            cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);
            break;
        case 180:
            cv::rotate(frame, frame, cv::ROTATE_180);
            break;
        case 270:
            cv::rotate(frame, frame, cv::ROTATE_90_COUNTERCLOCKWISE);
            break;
        default:
            break;
    }
}

// ============== VisionThread ==============

VisionThread::VisionThread(const Pipeline& pipeline, std::unique_ptr<BasePipeline> processor)
    : pipeline_(pipeline)
    , processor_(std::move(processor)) {
}

VisionThread::~VisionThread() {
    stop();
}

void VisionThread::start(std::shared_ptr<FrameQueue> inputQueue) {
    if (running_.load()) return;

    inputQueue_ = inputQueue;
    running_ = true;
    thread_ = std::thread(&VisionThread::run, this);
    spdlog::info("Vision thread started for pipeline {}", pipeline_.id);
}

void VisionThread::stop() {
    if (!running_.load()) return;

    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    spdlog::info("Vision thread stopped for pipeline {}", pipeline_.id);
}

FramePtr VisionThread::getProcessedFrame() {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return processedFrame_;
}

nlohmann::json VisionThread::getLatestResults() {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    return latestResults_;
}



void VisionThread::updateConfig(const nlohmann::json& config) {
    if (processor_) {
        processor_->updateConfig(config);
    }
}

void VisionThread::run() {
    while (running_.load()) {
        QueuedFrame qf;
        if (!inputQueue_->pop(qf, std::chrono::milliseconds(100))) {
            // Timeout - publish placeholder to keep stream alive
            static auto lastPlaceholderTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlaceholderTime).count() > 1000) {
                cv::Mat placeholder = cv::Mat::zeros(480, 640, CV_8UC3);
                cv::putText(placeholder, "Waiting for input...", cv::Point(160, 240), 
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
                StreamerService::instance().publishFrame(
                    "/pipeline/" + std::to_string(pipeline_.id),
                    placeholder
                );
                lastPlaceholderTime = now;
            }
            continue;
        }

        if (!qf.frame || qf.frame->empty()) {
            continue;
        }

        // Process frame
        auto result = processor_->process(qf.frame->color(), qf.frame->depth());

        // Create output frame
        auto outputFrame = std::make_shared<RefCountedFrame>(result.annotatedFrame);
        outputFrame->setSequence(qf.frame->sequence());

        // Update processed frame
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            processedFrame_ = outputFrame;
        }

        // Publish to MJPEG streamer
        StreamerService::instance().publishFrame(
            "/pipeline/" + std::to_string(pipeline_.id),
            outputFrame->color()
        );

        // Update results
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            latestResults_ = {
                {"pipeline_id", pipeline_.id},
                {"pipeline_name", pipeline_.name},
                {"detections", result.detections},
                {"processing_time_ms", result.processingTimeMs}
            };
        }
    }
}

// ============== ThreadManager ==============

ThreadManager& ThreadManager::instance() {
    static ThreadManager instance;
    return instance;
}

bool ThreadManager::startCamera(const Camera& camera) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cameraThreads_.find(camera.id);
    if (it != cameraThreads_.end()) {
        spdlog::warn("Camera {} already running", camera.id);
        return true;
    }

    auto driver = BaseDriver::create(camera);
    if (!driver) {
        spdlog::error("Failed to create driver for camera {}", camera.id);
        return false;
    }

    auto thread = std::make_unique<CameraThread>(camera, std::move(driver));
    if (!thread->start()) {
        spdlog::error("Failed to start camera thread for camera {}", camera.id);
        return false;
    }

    cameraThreads_.emplace(camera.id, std::move(thread));
    
    // Register stream path immediately so it doesn't 404 even if camera is slow/broken
    StreamerService::instance().registerPath("/camera/" + std::to_string(camera.id));
    
    return true;
}

void ThreadManager::stopCamera(int cameraId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cameraThreads_.find(cameraId);
    if (it != cameraThreads_.end()) {
        it->second->stop();
        cameraThreads_.erase(it);
    }
}

bool ThreadManager::isCameraRunning(int cameraId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cameraThreads_.find(cameraId);
    return it != cameraThreads_.end() && it->second->isRunning();
}

void ThreadManager::executeWithCameraPaused(int cameraId, std::function<void()> action) {
    Camera camera;
    bool wasRunning = false;
    std::vector<std::pair<int, std::shared_ptr<FrameQueue>>> queuesToRestore;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cameraThreads_.find(cameraId);
        if (it != cameraThreads_.end() && it->second->isRunning()) {
            wasRunning = true;
            camera = it->second->getCamera();

            // Find all pipelines for this camera and their queues
            for (auto const& [pipelineId, camId] : pipelineToCameraMap_) {
                if (camId == cameraId) {
                    auto qIt = pipelineQueues_.find(pipelineId);
                    if (qIt != pipelineQueues_.end()) {
                        queuesToRestore.push_back({pipelineId, qIt->second});
                    }
                }
            }

            // Stop camera
            it->second->stop();
            cameraThreads_.erase(it);
        }
    }

    // Execute action (without lock)
    action();

    if (wasRunning) {
        // Restart camera
        if (startCamera(camera)) {
             std::lock_guard<std::mutex> lock(mutex_);
             auto it = cameraThreads_.find(cameraId);
             if (it != cameraThreads_.end()) {
                 for (const auto& [pipelineId, queue] : queuesToRestore) {
                     it->second->registerQueue(pipelineId, queue);
                 }
             }
        }
    }
}


void ThreadManager::restartCamera(const Camera& newCamera) {
    int cameraId = newCamera.id;
    bool wasRunning = false;
    std::vector<std::pair<int, std::shared_ptr<FrameQueue>>> queuesToRestore;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cameraThreads_.find(cameraId);
        if (it != cameraThreads_.end()) {
            // Only restart if it was actually running (or at least the thread object existed)
            wasRunning = true;

            // Find all pipelines for this camera and their queues
            for (auto const& [pipelineId, camId] : pipelineToCameraMap_) {
                if (camId == cameraId) {
                    auto qIt = pipelineQueues_.find(pipelineId);
                    if (qIt != pipelineQueues_.end()) {
                        queuesToRestore.push_back({pipelineId, qIt->second});
                    }
                }
            }

            // Stop camera
            it->second->stop();
            cameraThreads_.erase(it);
        }
    }

    if (wasRunning) {
        // Restart camera with NEW settings
        if (startCamera(newCamera)) {
             std::lock_guard<std::mutex> lock(mutex_);
             auto it = cameraThreads_.find(cameraId);
             if (it != cameraThreads_.end()) {
                 for (const auto& [pipelineId, queue] : queuesToRestore) {
                     it->second->registerQueue(pipelineId, queue);
                 }
             }
             spdlog::info("Restarted camera {} with new settings", cameraId);
        }
    }
}

bool ThreadManager::startPipeline(const Pipeline& pipeline, int cameraId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = visionThreads_.find(pipeline.id);
    if (it != visionThreads_.end()) {
        spdlog::warn("Pipeline {} already running", pipeline.id);
        return true;
    }

    auto cameraIt = cameraThreads_.find(cameraId);
    if (cameraIt == cameraThreads_.end()) {
        spdlog::error("Camera {} not running, cannot start pipeline", cameraId);
        return false;
    }

    auto processor = BasePipeline::create(pipeline);
    if (!processor) {
        spdlog::error("Failed to create processor for pipeline {}", pipeline.id);
        return false;
    }

    // Create queue and register with camera
    auto queue = std::make_shared<FrameQueue>(2);
    cameraIt->second->registerQueue(pipeline.id, queue);

    // Create and start vision thread
    auto thread = std::make_unique<VisionThread>(pipeline, std::move(processor));

    // Inject calibration if available
    try {
        const auto& cam = cameraIt->second->getCamera();
        if (cam.camera_matrix_json.has_value() && !cam.camera_matrix_json->empty()) {
            auto matrixJson = nlohmann::json::parse(*cam.camera_matrix_json);
            
            cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
            if (matrixJson.is_array() && matrixJson.size() == 3) {
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 3; c++) {
                        cameraMatrix.at<double>(r, c) = matrixJson[r][c];
                    }
                }
            }

            cv::Mat distCoeffs = cv::Mat::zeros(5, 1, CV_64F);
            if (cam.dist_coeffs_json.has_value() && !cam.dist_coeffs_json->empty()) {
                auto distJson = nlohmann::json::parse(*cam.dist_coeffs_json);
                if (distJson.is_array()) {
                    distCoeffs = cv::Mat::zeros(distJson.size(), 1, CV_64F);
                    for (size_t i = 0; i < distJson.size(); i++) {
                        distCoeffs.at<double>(i) = distJson[i];
                    }
                }
            }

            thread->getProcessor()->setCalibration(cameraMatrix, distCoeffs);
            spdlog::info("Set calibration for pipeline {} (camera {}) with distortion", pipeline.id, cameraId);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse calibration for camera {}: {}", cameraId, e.what());
    }

    thread->start(queue);

    pipelineQueues_.emplace(pipeline.id, queue);
    pipelineToCameraMap_.emplace(pipeline.id, cameraId);
    visionThreads_.emplace(pipeline.id, std::move(thread));

    // Register stream path immediately
    StreamerService::instance().registerPath("/pipeline/" + std::to_string(pipeline.id));

    return true;
}

void ThreadManager::stopPipeline(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Unregister from camera
    auto mapIt = pipelineToCameraMap_.find(pipelineId);
    if (mapIt != pipelineToCameraMap_.end()) {
        auto cameraIt = cameraThreads_.find(mapIt->second);
        if (cameraIt != cameraThreads_.end()) {
            cameraIt->second->unregisterQueue(pipelineId);
        }
        pipelineToCameraMap_.erase(mapIt);
    }

    // Stop and remove vision thread
    auto it = visionThreads_.find(pipelineId);
    if (it != visionThreads_.end()) {
        it->second->stop();
        visionThreads_.erase(it);
    }

    pipelineQueues_.erase(pipelineId);
}

bool ThreadManager::isPipelineRunning(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = visionThreads_.find(pipelineId);
    return it != visionThreads_.end() && it->second->isRunning();
}

FramePtr ThreadManager::getCameraFrame(int cameraId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cameraThreads_.find(cameraId);
    if (it != cameraThreads_.end()) {
        return it->second->getDisplayFrame();
    }
    return nullptr;
}

FramePtr ThreadManager::getPipelineFrame(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = visionThreads_.find(pipelineId);
    if (it != visionThreads_.end()) {
        return it->second->getProcessedFrame();
    }
    return nullptr;
}

nlohmann::json ThreadManager::getPipelineResults(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = visionThreads_.find(pipelineId);
    if (it != visionThreads_.end()) {
        return it->second->getLatestResults();
    }
    return nlohmann::json::object();
}

nlohmann::json ThreadManager::getCameraResults(int cameraId) {
    // Collect vision thread pointers under lock, then release lock before calling methods
    // that acquire other locks (avoiding potential deadlock)
    std::vector<VisionThread*> threadsToQuery;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& [pipelineId, visionThread] : visionThreads_) {
            auto mapIt = pipelineToCameraMap_.find(pipelineId);
            if (mapIt != pipelineToCameraMap_.end() && mapIt->second == cameraId) {
                threadsToQuery.push_back(visionThread.get());
            }
        }
    }

    // Now query results without holding mutex_
    nlohmann::json results = nlohmann::json::array();
    for (auto* thread : threadsToQuery) {
        results.push_back(thread->getLatestResults());
    }

    return results;
}

void ThreadManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Stop all vision threads first
    for (auto& [id, thread] : visionThreads_) {
        thread->stop();
    }
    visionThreads_.clear();
    pipelineQueues_.clear();
    pipelineToCameraMap_.clear();

    // Stop all camera threads
    for (auto& [id, thread] : cameraThreads_) {
        thread->stop();
    }
    cameraThreads_.clear();

    spdlog::info("ThreadManager shutdown complete");
}

} // namespace vision
