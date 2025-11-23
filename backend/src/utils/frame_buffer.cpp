#include "utils/frame_buffer.hpp"

namespace vision {

RefCountedFrame::RefCountedFrame(cv::Mat color, std::optional<cv::Mat> depth)
    : colorFrame_(std::move(color))
    , depthFrame_(std::move(depth))
    , timestamp_(std::chrono::steady_clock::now()) {
}

void RefCountedFrame::acquire() {
    refCount_.fetch_add(1, std::memory_order_acq_rel);
}

void RefCountedFrame::release() {
    refCount_.fetch_sub(1, std::memory_order_acq_rel);
}

const std::vector<uchar>& RefCountedFrame::getJpeg(int quality) {
    std::lock_guard<std::mutex> lock(jpegMutex_);

    if (jpegCacheValid_ && jpegQuality_ == quality) {
        return jpegCache_;
    }

    if (colorFrame_.empty()) {
        jpegCache_.clear();
        return jpegCache_;
    }

    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
    cv::imencode(".jpg", colorFrame_, jpegCache_, params);

    jpegQuality_ = quality;
    jpegCacheValid_ = true;

    return jpegCache_;
}

void RefCountedFrame::clearJpegCache() {
    std::lock_guard<std::mutex> lock(jpegMutex_);
    jpegCacheValid_ = false;
    jpegCache_.clear();
}

} // namespace vision
