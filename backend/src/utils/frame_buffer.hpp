#pragma once

#include <opencv2/opencv.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <chrono>
#include <memory>

namespace vision {

class RefCountedFrame {
public:
    RefCountedFrame() = default;
    RefCountedFrame(cv::Mat color, std::optional<cv::Mat> depth = std::nullopt);

    // Acquire a reference
    void acquire();

    // Release a reference
    void release();

    // Get reference count
    int refCount() const { return refCount_.load(); }

    // Get color frame
    const cv::Mat& color() const { return colorFrame_; }
    cv::Mat& color() { return colorFrame_; }

    // Get depth frame
    const std::optional<cv::Mat>& depth() const { return depthFrame_; }

    // Get JPEG encoded frame (cached)
    const std::vector<uchar>& getJpeg(int quality = 85);

    // Get timestamp
    std::chrono::steady_clock::time_point timestamp() const { return timestamp_; }

    // Set timestamp
    void setTimestamp(std::chrono::steady_clock::time_point ts) { timestamp_ = ts; }

    // Check if frame is empty
    bool empty() const { return colorFrame_.empty(); }

    // Frame sequence number
    uint64_t sequence() const { return sequence_; }
    void setSequence(uint64_t seq) { sequence_ = seq; }

    // Clear cached JPEG
    void clearJpegCache();

private:
    cv::Mat colorFrame_;
    std::optional<cv::Mat> depthFrame_;
    std::atomic<int> refCount_{0};
    std::chrono::steady_clock::time_point timestamp_;
    uint64_t sequence_ = 0;

    // JPEG cache
    std::vector<uchar> jpegCache_;
    int jpegQuality_ = 0;
    bool jpegCacheValid_ = false;
    std::mutex jpegMutex_;
};

using FramePtr = std::shared_ptr<RefCountedFrame>;

} // namespace vision
