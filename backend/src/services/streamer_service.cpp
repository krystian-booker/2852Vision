#include "services/streamer_service.hpp"
#include <spdlog/spdlog.h>

namespace vision {

StreamerService& StreamerService::instance() {
    static StreamerService instance;
    return instance;
}

StreamerService::~StreamerService() {
    shutdown();
}

void StreamerService::initialize(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        spdlog::warn("StreamerService already initialized");
        return;
    }

    try {
        streamer_ = std::make_unique<nadjieb::MJPEGStreamer>();
        streamer_->start(port);
        
        // Pre-configure compression parameters
        compression_params_ = {cv::IMWRITE_JPEG_QUALITY, 80};
        
        // Start worker thread
        running_ = true;
        workerThread_ = std::thread(&StreamerService::workerLoop, this);

        initialized_ = true;
        spdlog::info("MJPEG Streamer started on port {}", port);
    } catch (const std::exception& e) {
        spdlog::error("Failed to start MJPEG Streamer: {}", e.what());
    }
}

void StreamerService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (streamer_ && streamer_->isRunning()) {
            streamer_->stop();
            spdlog::info("MJPEG Streamer stopped");
        }
        initialized_ = false;
    }

    // Stop worker thread
    running_ = false;
    queueCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    // Clear queue
    std::lock_guard<std::mutex> lock(queueMutex_);
    std::queue<StreamerFrame> empty;
    std::swap(queue_, empty);
}

void StreamerService::publishFrame(const std::string& path, const cv::Mat& frame) {
    if (!initialized_ || !streamer_ || !streamer_->isRunning()) {
        return;
    }

    if (frame.empty()) {
        return;
    }

    // Quick check if anyone is listening to this path to save queue overhead
    // We assume hasClient is safe enough to call without lock for this optimization
    if (streamer_->hasClient(path) == false) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.size() > 5) {
            // Drop oldest frame if queue is full
            queue_.pop();
        }
        queue_.push({path, frame.clone()}); // Clone frame to ensure it's valid when processed
    }
    queueCv_.notify_one();
}

void StreamerService::workerLoop() {
    // Reusable buffers to avoid allocation overhead
    cv::Mat resizedFrame;
    std::vector<uchar> local_buffer;
    // Reserve some memory to avoid initial reallocations (approx 1280*720*3 for mat, 500KB for jpeg)
    // This is a heuristic; actual usage will adapt.
    local_buffer.reserve(500 * 1024); 

    while (running_) {
        StreamerFrame item;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return !queue_.empty() || !running_; });

            if (!running_ && queue_.empty()) {
                return;
            }

            if (queue_.empty()) {
                continue;
            }

            item = queue_.front();
            queue_.pop();
        }

        // Double check if client is still connected before encoding
        if (!streamer_->hasClient(item.path)) {
            continue;
        }

        try {
            auto start = std::chrono::steady_clock::now();
            
            cv::Mat* frameToEncode = &item.frame;

            // Downscale if too large (e.g., > 1024 width) to improve performance
            if (frameToEncode->cols > 1024) {
                double scale = 1024.0 / frameToEncode->cols;
                // Use INTER_NEAREST for speed. It's much faster than LINEAR/CUBIC
                cv::resize(*frameToEncode, resizedFrame, cv::Size(), scale, scale, cv::INTER_NEAREST);
                frameToEncode = &resizedFrame;
            }

            // --- FPS Calculation and Overlay ---
            auto& tracker = fpsTrackers_[item.path];
            tracker.frameCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - tracker.lastFrameTime).count();

            if (elapsed >= 1000) {
                tracker.currentFps = tracker.frameCount * 1000.0 / elapsed;
                tracker.frameCount = 0;
                tracker.lastFrameTime = now;
            }

            if (tracker.currentFps > 0.0) {
                std::string fpsText = fmt::format("FPS: {:.1f}", tracker.currentFps);
                int fontFace = cv::FONT_HERSHEY_SIMPLEX;
                double fontScale = 1.0;
                int thickness = 2;
                int baseline = 0;
                cv::Size textSize = cv::getTextSize(fpsText, fontFace, fontScale, thickness, &baseline);
                
                // Position: Top Right
                cv::Point textOrg(frameToEncode->cols - textSize.width - 10, textSize.height + 10);
                
                // Draw FPS
                cv::putText(*frameToEncode, fpsText, textOrg, fontFace, fontScale, cv::Scalar(255, 255, 255), thickness);
            }
            // -----------------------------------

            // Encode to JPEG
            // Use lower quality (50) for better performance
            std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 50};
            cv::imencode(".jpg", *frameToEncode, local_buffer, params);

            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (duration > 20) {
                spdlog::warn("Slow encoding for {}: {}ms (Queue size: {})", item.path, duration, queue_.size());
            }

            // Publish
            streamer_->publish(item.path, std::string(local_buffer.begin(), local_buffer.end()));

            // Mark path as registered if not already
            // Note: We might want to optimize this to avoid locking every time
            // but for now let's just do a quick check
            bool needsRegistration = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (registeredPaths_.find(item.path) == registeredPaths_.end()) {
                    needsRegistration = true;
                }
            }
            
            if (needsRegistration) {
                std::lock_guard<std::mutex> lock(mutex_);
                registeredPaths_.insert(item.path);
            }

        } catch (const std::exception& e) {
            spdlog::error("Error publishing frame to {}: {}", item.path, e.what());
        }
    }
}

bool StreamerService::isRunning() const {
    return initialized_ && streamer_ && streamer_->isRunning();
}

void StreamerService::registerPath(const std::string& path) {
    if (!initialized_ || !streamer_ || !streamer_->isRunning()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (registeredPaths_.find(path) == registeredPaths_.end()) {
        try {
            // Publish a 640x480 black frame as placeholder
            cv::Mat placeholder = cv::Mat::zeros(480, 640, CV_8UC3);
            cv::putText(placeholder, "Waiting for camera...", cv::Point(160, 240), 
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
            
            std::vector<uchar> local_buffer;
            cv::imencode(".jpg", placeholder, local_buffer, compression_params_);
            
            streamer_->publish(path, std::string(local_buffer.begin(), local_buffer.end()));
            registeredPaths_.insert(path);
            spdlog::info("Registered stream path: {}", path);
        } catch (const std::exception& e) {
            spdlog::error("Failed to register path {}: {}", path, e.what());
        }
    }
}

} // namespace vision
