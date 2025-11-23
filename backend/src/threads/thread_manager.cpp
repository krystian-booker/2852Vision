#include "threads/thread_manager.hpp"
#include "services/pipeline_service.hpp"
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

void CameraThread::start() {
    if (running_.load()) return;

    if (!driver_->connect()) {
        spdlog::error("Failed to connect to camera {}", camera_.id);
        return;
    }

    running_ = true;
    thread_ = std::thread(&CameraThread::run, this);
    spdlog::info("Camera thread started for camera {}", camera_.id);
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
    while (running_.load()) {
        auto frameResult = driver_->getFrame();

        if (frameResult.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
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

        // Distribute to vision threads
        {
            std::lock_guard<std::mutex> lock(queuesMutex_);
            for (auto& [pipelineId, queue] : queues_) {
                queue->push(frame);
            }
        }
    }
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
    thread->start();

    cameraThreads_.emplace(camera.id, std::move(thread));
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
    thread->start(queue);

    pipelineQueues_.emplace(pipeline.id, queue);
    pipelineToCameraMap_.emplace(pipeline.id, cameraId);
    visionThreads_.emplace(pipeline.id, std::move(thread));

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
