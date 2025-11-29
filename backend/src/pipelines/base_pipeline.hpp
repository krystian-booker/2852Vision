#pragma once

#include "utils/geometry.hpp"
#include "models/pipeline.hpp"
#include "utils/frame_buffer.hpp"
#include "vision/field_layout.hpp"
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <memory>

namespace vision {

// Result from pipeline processing
struct PipelineResult {
    nlohmann::json detections;  // Pipeline-specific detection data
    cv::Mat annotatedFrame;     // Frame with overlays drawn
    double processingTimeMs = 0;
    std::optional<Pose3d> robotPose; // Global robot pose (if available)
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
    virtual void setCalibration(const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs) {
        cameraMatrix_ = cameraMatrix.clone();
        distCoeffs_ = distCoeffs.clone();
        hasCalibration_ = true;
    }

    // Set camera calibration (simplified)
    virtual void setCalibration(double fx, double fy, double cx, double cy) {
        cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
        cameraMatrix.at<double>(0, 0) = fx;
        cameraMatrix.at<double>(1, 1) = fy;
        cameraMatrix.at<double>(0, 2) = cx;
        cameraMatrix.at<double>(1, 2) = cy;
        
        cv::Mat distCoeffs = cv::Mat::zeros(5, 1, CV_64F);
        
        setCalibration(cameraMatrix, distCoeffs);
    }

    // Set field layout (for global pose estimation)
    virtual void setFieldLayout(const FieldLayout& layout) {}

    bool hasCalibration() const { return hasCalibration_; }

    // Factory method
    static std::unique_ptr<BasePipeline> create(const Pipeline& pipeline);

protected:
    cv::Mat cameraMatrix_;
    cv::Mat distCoeffs_;
    bool hasCalibration_ = false;
};

} // namespace vision
