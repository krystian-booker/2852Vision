#pragma once

#include "pipelines/base_pipeline.hpp"
#include "models/pipeline.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>
#include <chrono>
#include <mutex>
#include <vector>

namespace vision {

// Result structure for optical flow measurements
struct OpticalFlowResult {
    double vx_mps = 0.0;           // Robot-frame velocity X (forward) m/s
    double vy_mps = 0.0;           // Robot-frame velocity Y (left) m/s
    int64_t timestamp_us = 0;      // Timestamp in microseconds
    int valid_vectors = 0;         // Number of flow vectors used
    bool valid = false;            // Measurement validity flag
};

class OpticalFlowPipeline : public BasePipeline {
public:
    OpticalFlowPipeline();
    explicit OpticalFlowPipeline(const OpticalFlowConfig& config);
    ~OpticalFlowPipeline() override = default;

    PipelineResult process(const cv::Mat& frame,
                          const std::optional<cv::Mat>& depth = std::nullopt) override;

    void updateConfig(const nlohmann::json& config) override;

    PipelineType type() const override { return PipelineType::OpticalFlow; }

    // Get latest flow result (thread-safe)
    OpticalFlowResult getFlowResult() const;

private:
    OpticalFlowConfig config_;
    mutable std::mutex mutex_;

    // Frame state
    cv::Mat prevGray_;
    std::vector<cv::Point2f> prevPoints_;
    std::chrono::steady_clock::time_point prevTimestamp_;
    bool initialized_ = false;
    uint64_t frameCount_ = 0;

    // Smoothed velocity state
    double smoothedVx_ = 0.0;
    double smoothedVy_ = 0.0;
    OpticalFlowResult lastResult_;

    // Processing methods
    void detectFeatures(const cv::Mat& gray);

    // Returns (dx_pixels, dy_pixels, valid_count) - average pixel displacement
    std::tuple<double, double, int> processLucasKanade(const cv::Mat& gray);
    std::tuple<double, double, int> processFarneback(const cv::Mat& gray);

    // Convert pixel displacement to robot-frame velocity
    void pixelToRobotVelocity(double dx_px, double dy_px, double dt,
                              double& vx_mps, double& vy_mps) const;

    // Draw visualization on frame
    void drawVisualization(cv::Mat& frame,
                           const std::vector<cv::Point2f>& currPoints,
                           double vx, double vy, int features, bool valid);
};

} // namespace vision
