#pragma once

#include <nadjieb/mjpeg_streamer.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace vision {

class StreamerService {
public:
    static StreamerService& instance();

    // Initialize and start the streamer on the specified port
    void initialize(int port = 5805);

    // Stop the streamer
    void shutdown();

    // Publish a frame to the specified path (e.g., "/camera/1")
    void publishFrame(const std::string& path, const cv::Mat& frame);

    // Explicitly register a path with a placeholder frame to ensure it exists
    void registerPath(const std::string& path);

    bool isRunning() const;

private:
    StreamerService() = default;
    ~StreamerService();

    StreamerService(const StreamerService&) = delete;
    StreamerService& operator=(const StreamerService&) = delete;

    std::unique_ptr<nadjieb::MJPEGStreamer> streamer_;
    std::mutex mutex_;
    bool initialized_ = false;
    
    // Buffer for JPEG encoding to avoid reallocation
    std::vector<uchar> buffer_;
    std::vector<int> compression_params_;
    
    // Track registered paths to ensure they are created before checking hasClient
    std::unordered_set<std::string> registeredPaths_;

    // Worker thread for asynchronous encoding
    struct StreamerFrame {
        std::string path;
        cv::Mat frame;
    };

    std::queue<StreamerFrame> queue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    
    struct FpsTracker {
        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
        int frameCount = 0;
        double currentFps = 0.0;
    };

    std::unordered_map<std::string, FpsTracker> fpsTrackers_;

    void workerLoop();
};

} // namespace vision
