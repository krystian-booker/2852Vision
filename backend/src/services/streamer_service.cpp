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
        
        initialized_ = true;
        spdlog::info("MJPEG Streamer started on port {}", port);
    } catch (const std::exception& e) {
        spdlog::error("Failed to start MJPEG Streamer: {}", e.what());
    }
}

void StreamerService::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (streamer_ && streamer_->isRunning()) {
        streamer_->stop();
        spdlog::info("MJPEG Streamer stopped");
    }
    initialized_ = false;
}

void StreamerService::publishFrame(const std::string& path, const cv::Mat& frame) {
    if (!initialized_ || !streamer_ || !streamer_->isRunning()) {
        return;
    }

    if (frame.empty()) {
        return;
    }

    // Check if anyone is listening to this path to save CPU
    // Note: hasClient is not thread-safe in the library if called concurrently with removeClient,
    // but for now we assume it's "safe enough" or we just encode anyway. 
    // The library's publish method is thread-safe.
    // Check if anyone is listening to this path to save CPU
    // We use hasClient to check if there are any active subscribers for this path.
    // If not, we skip encoding and publishing to save resources.
    // BUT: We must ensure the path is registered (published to at least once) 
    // otherwise clients will get 404 and never be able to connect.
    bool pathRegistered = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (registeredPaths_.find(path) != registeredPaths_.end()) {
            pathRegistered = true;
        }
    }

    if (pathRegistered && !streamer_->hasClient(path)) {
        // spdlog::trace("No clients for {}, skipping", path);
        return;
    }

    // spdlog::info("Publishing frame to {} (hasClient: {})", path, streamer_->hasClient(path));

    try {
        // Encode to JPEG
        // We use a local buffer if we want to be fully thread-safe without locking the class member buffer
        // or we lock the buffer. Given this might be called from multiple threads (different cameras),
        // let's use a local buffer for safety.
        std::vector<uchar> local_buffer;
        cv::imencode(".jpg", frame, local_buffer, compression_params_);

        // Publish
        streamer_->publish(path, std::string(local_buffer.begin(), local_buffer.end()));

        // Mark path as registered
        if (!pathRegistered) {
            std::lock_guard<std::mutex> lock(mutex_);
            registeredPaths_.insert(path);
        }
    } catch (const std::exception& e) {
        spdlog::error("Error publishing frame to {}: {}", path, e.what());
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
