#pragma once

#include "models/pipeline.hpp"
#include "utils/frame_buffer.hpp"
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <memory>

namespace vision {

// Result from pipeline processing
struct PipelineResult {
    nlohmann::json detections;  // Pipeline-specific detection data
    cv::Mat annotatedFrame;     // Frame with overlays drawn
    double processingTimeMs = 0;
};

class BasePipeline {
public:
    virtual ~BasePipeline() = default;

    // Process a frame and return results
    virtual PipelineResult process(const cv::Mat& frame,
                                   const std::optional<cv::Mat>& depth = std::nullopt) = 0;

    // Update pipeline configuration
    virtual void updateConfig(const nlohmann::json& config) = 0;

    // Get pipeline type
    virtual PipelineType type() const = 0;

    // Set camera calibration (for pose estimation)
    void setCalibration(const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs) {
        cameraMatrix_ = cameraMatrix.clone();
        distCoeffs_ = distCoeffs.clone();
        hasCalibration_ = true;
    }

    bool hasCalibration() const { return hasCalibration_; }

    // Factory method
    static std::unique_ptr<BasePipeline> create(const Pipeline& pipeline);

protected:
    cv::Mat cameraMatrix_;
    cv::Mat distCoeffs_;
    bool hasCalibration_ = false;
};

} // namespace vision
