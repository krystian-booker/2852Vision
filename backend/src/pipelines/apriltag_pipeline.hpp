#pragma once

#include "pipelines/base_pipeline.hpp"
#include "models/pipeline.hpp"
#include "vision/field_layout.hpp"
#include "utils/geometry.hpp"

#include <memory>

extern "C" {
#include <apriltag.h>
#include <tag36h11.h>
#include <tag16h5.h>
#include <tag25h9.h>
#include <tagCircle21h7.h>
#include <tagStandard41h12.h>
#include <apriltag_pose.h>
}

namespace vision {

// RAII deleter for apriltag_detector_t
struct AprilTagDetectorDeleter {
    void operator()(apriltag_detector_t* detector) const {
        if (detector) {
            apriltag_detector_destroy(detector);
        }
    }
};

// RAII deleter for apriltag_family_t
struct AprilTagFamilyDeleter {
    std::string familyName;

    void operator()(apriltag_family_t* family) const;
};

using AprilTagDetectorPtr = std::unique_ptr<apriltag_detector_t, AprilTagDetectorDeleter>;
using AprilTagFamilyPtr = std::unique_ptr<apriltag_family_t, AprilTagFamilyDeleter>;

// RAII wrapper for apriltag_pose_t matrices
class PoseMatrixGuard {
public:
    explicit PoseMatrixGuard(apriltag_pose_t& pose) : pose_(pose) {}
    ~PoseMatrixGuard() noexcept {
        if (pose_.R) matd_destroy(pose_.R);
        if (pose_.t) matd_destroy(pose_.t);
    }

    PoseMatrixGuard(const PoseMatrixGuard&) = delete;
    PoseMatrixGuard& operator=(const PoseMatrixGuard&) = delete;

private:
    apriltag_pose_t& pose_;
};

// Structure to hold a single tag detection with pose info
struct TagDetection {
    int id;
    double decisionMargin;
    std::vector<cv::Point2f> corners;  // Image corners
    cv::Point2d center;
    std::optional<Pose3d> cameraPose;  // Pose of tag in camera frame
    std::optional<Pose3d> fieldPose;   // Robot pose in field frame (if field layout available)
};

// Result of multi-tag pose estimation
struct MultiTagResult {
    bool valid = false;
    Pose3d robotPose;           // Robot pose in field coordinates
    double reprojectionError = 0.0;
    int tagsUsed = 0;
    std::vector<int> tagIds;    // IDs of tags used
};

class AprilTagPipeline : public BasePipeline {
public:
    AprilTagPipeline();
    explicit AprilTagPipeline(const AprilTagConfig& config);
    ~AprilTagPipeline() override;

    PipelineResult process(const cv::Mat& frame,
                          const std::optional<cv::Mat>& depth = std::nullopt) override;

    void updateConfig(const nlohmann::json& config) override;

    PipelineType type() const override { return PipelineType::AprilTag; }

    // Set camera calibration for pose estimation
    void setCalibration(const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs) override;
    void setCalibration(double fx, double fy, double cx, double cy) override;

    // Set field layout for multi-tag pose estimation
    void setFieldLayout(const FieldLayout& layout);

private:
    AprilTagConfig config_;

    // AprilTag detector and family (RAII managed)
    AprilTagDetectorPtr detector_;
    AprilTagFamilyPtr family_;


    // Field layout for multi-tag pose
    std::optional<FieldLayout> fieldLayout_;

    // Previous pose estimate for iterative refinement
    cv::Vec3d prevRvec_, prevTvec_;
    bool hasPrevPose_ = false;

    void initializeDetector();
    AprilTagFamilyPtr createFamily(const std::string& familyName);

    // Multi-tag pose estimation
    MultiTagResult solveMultiTagPose(const std::vector<TagDetection>& detections,
                                     const cv::Size& imageSize);

    // Get 3D corners of a tag in field coordinates
    std::vector<cv::Point3f> getTagCornersInField(int tagId) const;
};

} // namespace vision
