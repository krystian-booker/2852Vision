#define _USE_MATH_DEFINES
#include "pipelines/apriltag_pipeline.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <cmath>
#include "utils/coordinate_system.hpp"
#include "services/settings_service.hpp"
#include "vision/field_layout.hpp"

namespace vision {

// Implementation of AprilTagFamilyDeleter
void AprilTagFamilyDeleter::operator()(apriltag_family_t* family) const {
    if (!family) return;

    if (familyName == "tag36h11") {
        tag36h11_destroy(family);
    } else if (familyName == "tag16h5") {
        tag16h5_destroy(family);
    } else if (familyName == "tag25h9") {
        tag25h9_destroy(family);
    } else if (familyName == "tagCircle21h7") {
        tagCircle21h7_destroy(family);
    } else if (familyName == "tagStandard41h12") {
        tagStandard41h12_destroy(family);
    } else {
        // Default case
        tag36h11_destroy(family);
    }
}

AprilTagPipeline::AprilTagPipeline() {
    initializeDetector();
}

AprilTagPipeline::AprilTagPipeline(const AprilTagConfig& config)
    : config_(config) {
    initializeDetector();
}

AprilTagPipeline::~AprilTagPipeline() {
    // Remove family from detector before destruction (order matters)
    if (detector_ && family_) {
        apriltag_detector_remove_family(detector_.get(), family_.get());
    }
    // Smart pointers handle destruction automatically
}

AprilTagFamilyPtr AprilTagPipeline::createFamily(const std::string& familyName) {
    apriltag_family_t* rawFamily = nullptr;

    if (familyName == "tag36h11") {
        rawFamily = tag36h11_create();
    } else if (familyName == "tag16h5") {
        rawFamily = tag16h5_create();
    } else if (familyName == "tag25h9") {
        rawFamily = tag25h9_create();
    } else if (familyName == "tagCircle21h7") {
        rawFamily = tagCircle21h7_create();
    } else if (familyName == "tagStandard41h12") {
        rawFamily = tagStandard41h12_create();
    } else {
        // Default to tag36h11
        spdlog::warn("Unknown tag family '{}', defaulting to tag36h11", familyName);
        rawFamily = tag36h11_create();
    }

    return AprilTagFamilyPtr(rawFamily, AprilTagFamilyDeleter{familyName});
}

void AprilTagPipeline::initializeDetector() {
    // Create tag family
    family_ = createFamily(config_.family);
    if (!family_) {
        spdlog::error("Failed to create AprilTag family: {}", config_.family);
        throw std::runtime_error("Failed to create AprilTag family: " + config_.family);
    }

    // Create detector
    detector_.reset(apriltag_detector_create());
    if (!detector_) {
        spdlog::error("Failed to create AprilTag detector");
        family_.reset();  // Clean up family on failure
        throw std::runtime_error("Failed to create AprilTag detector");
    }

    // Add family to detector
    apriltag_detector_add_family(detector_.get(), family_.get());

    // Configure detector parameters
    detector_->nthreads = config_.threads;

    detector_->quad_decimate = config_.decimate;
    detector_->quad_sigma = config_.blur;
    detector_->refine_edges = config_.refine_edges ? 1 : 0;
    detector_->decode_sharpening = 0.25;

    spdlog::info("AprilTag detector initialized - family: {}, threads: {}, decimate: {:.1f}",
                 config_.family, detector_->nthreads, config_.decimate);

    // Set initial field layout from global settings
    std::string selectedField = SettingsService::instance().getSelectedField();
    if (!selectedField.empty()) {
        auto layout = FieldLayoutService::instance().getFieldLayout(selectedField);
        if (layout) {
            setFieldLayout(*layout);
        }
    }
}

void AprilTagPipeline::setCalibration(const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs) {
    BasePipeline::setCalibration(cameraMatrix, distCoeffs);
    spdlog::debug("AprilTag calibration set with distortion coefficients");
}

void AprilTagPipeline::setCalibration(double fx, double fy, double cx, double cy) {
    BasePipeline::setCalibration(fx, fy, cx, cy);
    spdlog::debug("AprilTag calibration set (simplified) - fx: {:.1f}, fy: {:.1f}, cx: {:.1f}, cy: {:.1f}",
                  fx, fy, cx, cy);
}

PipelineResult AprilTagPipeline::process(const cv::Mat& frame,
                                         const std::optional<cv::Mat>& depth) {
    auto startTime = std::chrono::high_resolution_clock::now();

    PipelineResult result;
    result.detections = nlohmann::json::array();

    // Lock for thread safety
    std::lock_guard<std::mutex> lock(mutex_);

    if (!detector_ || !family_) {
        spdlog::warn("AprilTag detector not initialized");
        result.annotatedFrame = frame.clone();
        auto endTime = std::chrono::high_resolution_clock::now();
        result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return result;
    }

    // Convert to grayscale for detection
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame;
    }

    // Create image structure for AprilTag
    image_u8_t im = {
        .width = static_cast<int32_t>(gray.cols),
        .height = static_cast<int32_t>(gray.rows),
        .stride = static_cast<int32_t>(gray.cols),
        .buf = gray.data
    };

    // Run detection
    zarray_t* detections = apriltag_detector_detect(detector_.get(), &im);

    // Clone frame for annotation
    if (frame.channels() == 1) {
        cv::cvtColor(frame, result.annotatedFrame, cv::COLOR_GRAY2BGR);
    } else {
        result.annotatedFrame = frame.clone();
    }

    // Collect valid detections for global solver
    std::vector<TagDetection> validDetectionsForSolver;

    for (int i = 0; i < zarray_size(detections); i++) {
        apriltag_detection_t* det;
        zarray_get(detections, i, &det);

        // Check decision margin threshold
        if (det->decision_margin < config_.decision_margin) {
            continue;
        }

        // Draw visuals
        std::vector<cv::Point> drawCorners;
        for (int j = 0; j < 4; j++) {
            drawCorners.push_back(cv::Point(static_cast<int>(det->p[j][0]), static_cast<int>(det->p[j][1])));
        }
        cv::polylines(result.annotatedFrame, drawCorners, true, cv::Scalar(0, 255, 0), 2);
        cv::circle(result.annotatedFrame, cv::Point(static_cast<int>(det->c[0]), static_cast<int>(det->c[1])), 5, cv::Scalar(0, 0, 255), -1);
        cv::putText(result.annotatedFrame, std::to_string(det->id), cv::Point(static_cast<int>(det->c[0] - 10), static_cast<int>(det->c[1] - 10)), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2);

        // Build basic detection JSON
        nlohmann::json detection;
        detection["id"] = det->id;
        detection["decision_margin"] = det->decision_margin;
        detection["hamming"] = det->hamming;
        detection["center"] = {det->c[0], det->c[1]};
        
        nlohmann::json cornersJson = nlohmann::json::array();
        for (int j = 0; j < 4; j++) {
            cornersJson.push_back({det->p[j][0], det->p[j][1]});
        }
        detection["corners"] = cornersJson;

        // --- PART A: Individual Tag Solve (Tag-Relative) ---
        if (hasCalibration_) {
            // 3D Object Points for the tag (centered at origin, z=0)
            double halfSize = config_.tag_size_m / 2.0;
            std::vector<cv::Point3f> objectPoints = {
                cv::Point3f(-halfSize,  halfSize, 0), // Bottom-Left
                cv::Point3f( halfSize,  halfSize, 0), // Bottom-Right
                cv::Point3f( halfSize, -halfSize, 0), // Top-Right
                cv::Point3f(-halfSize, -halfSize, 0)  // Top-Left
            };

            std::vector<cv::Point2f> imagePoints;
            for (int j = 0; j < 4; j++) {
                imagePoints.push_back(cv::Point2f(static_cast<float>(det->p[j][0]), static_cast<float>(det->p[j][1])));
            }

            cv::Vec3d rvec, tvec;
            bool success = cv::solvePnP(objectPoints, imagePoints, cameraMatrix_, distCoeffs_, rvec, tvec, false, cv::SOLVEPNP_SQPNP);

            if (success) {
                // Convert to Pose3d (Camera-Relative)
                // Note: This is the pose of the TAG in CAMERA coordinates
                Pose3d tagPose = Pose3d::fromOpenCV(rvec, tvec);
                detection["pose_relative"] = tagPose.toJson();

                // Draw 3D Cube
                double size = config_.tag_size_m;
                std::vector<cv::Point3f> cubePoints = {
                    cv::Point3f(-halfSize, -halfSize, 0), cv::Point3f( halfSize, -halfSize, 0),
                    cv::Point3f( halfSize,  halfSize, 0), cv::Point3f(-halfSize,  halfSize, 0),
                    cv::Point3f(-halfSize, -halfSize, -size), cv::Point3f( halfSize, -halfSize, -size),
                    cv::Point3f( halfSize,  halfSize, -size), cv::Point3f(-halfSize,  halfSize, -size)
                };
                std::vector<cv::Point2f> imagePointsCube;
                cv::projectPoints(cubePoints, rvec, tvec, cameraMatrix_, distCoeffs_, imagePointsCube);
                cv::Scalar cubeColor(0, 255, 0);
                for (int k = 0; k < 4; k++) {
                    cv::line(result.annotatedFrame, imagePointsCube[k], imagePointsCube[k+4], cubeColor, 2);
                    cv::line(result.annotatedFrame, imagePointsCube[k+4], imagePointsCube[((k+1)%4)+4], cubeColor, 2);
                }
            }
        }

        result.detections.push_back(detection);

        // --- PART B: Prep for Global Solve (Field-Relative) ---
        if (fieldLayout_ && fieldLayout_->hasTag(det->id)) {
            TagDetection tagData;
            tagData.id = det->id;
            for (int j = 0; j < 4; j++) {
                tagData.corners.push_back(cv::Point2f(static_cast<float>(det->p[j][0]), static_cast<float>(det->p[j][1])));
            }
            validDetectionsForSolver.push_back(tagData);
        }
    }

    // --- PART C: Global Solve (Multi-Tag Logic) ---
    if (hasCalibration_ && !validDetectionsForSolver.empty()) {
        MultiTagResult globalPose = solveMultiTagPose(validDetectionsForSolver, frame.size());
        if (globalPose.valid) {
            result.robotPose = globalPose.robotPose;
        }
    }

    // Cleanup detections
    apriltag_detections_destroy(detections);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

void AprilTagPipeline::updateConfig(const nlohmann::json& config) {
    // Parse new config first (may throw)
    AprilTagConfig newConfig = AprilTagConfig::fromJson(config);

    // Create new detector and family before modifying state (exception safety)
    AprilTagFamilyPtr newFamily = createFamily(newConfig.family);
    if (!newFamily) {
        throw std::runtime_error("Failed to create AprilTag family: " + newConfig.family);
    }

    AprilTagDetectorPtr newDetector(apriltag_detector_create());
    if (!newDetector) {
        throw std::runtime_error("Failed to create AprilTag detector");
    }

    // Configure new detector
    apriltag_detector_add_family(newDetector.get(), newFamily.get());

    newDetector->nthreads = newConfig.threads;

    newDetector->quad_decimate = newConfig.decimate;
    newDetector->quad_sigma = newConfig.blur;
    newDetector->refine_edges = newConfig.refine_edges ? 1 : 0;
    newDetector->decode_sharpening = 0.25;

    // Now swap - this won't throw
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Remove family from old detector before destruction
        if (detector_ && family_) {
            apriltag_detector_remove_family(detector_.get(), family_.get());
        }

        config_ = std::move(newConfig);
        family_ = std::move(newFamily);
        detector_ = std::move(newDetector);
    }

    spdlog::info("AprilTag config updated - family: {}, threads: {}, decimate: {:.1f}",
                 config_.family, detector_->nthreads, config_.decimate);
}

void AprilTagPipeline::setFieldLayout(const FieldLayout& layout) {
    fieldLayout_ = layout;
    spdlog::info("AprilTag field layout set: {} tags", layout.size());
}

std::vector<cv::Point3f> AprilTagPipeline::getTagCornersInField(int tagId) const {
    std::vector<cv::Point3f> corners;

    if (!fieldLayout_) {
        return corners;
    }

    auto tagPose = fieldLayout_->getTagPose(tagId);
    if (!tagPose) {
        return corners;
    }

    // Tag corners in tag-local coordinates (centered at origin)
    // Order: bottom-left, bottom-right, top-right, top-left (CCW from camera view)
    double halfSize = config_.tag_size_m / 2.0;
    std::vector<Eigen::Vector3d> localCorners = {
        {-halfSize,  halfSize, 0.0},  // Bottom-left
        { halfSize,  halfSize, 0.0},  // Bottom-right
        { halfSize, -halfSize, 0.0},  // Top-right
        {-halfSize, -halfSize, 0.0}   // Top-left
    };

    // Transform corners to field coordinates
    for (const auto& local : localCorners) {
        Eigen::Vector3d fieldPoint = tagPose->transformPoint(local);
        corners.push_back(cv::Point3f(
            static_cast<float>(fieldPoint.x()),
            static_cast<float>(fieldPoint.y()),
            static_cast<float>(fieldPoint.z())
        ));
    }

    return corners;
}

MultiTagResult AprilTagPipeline::solveMultiTagPose(const std::vector<TagDetection>& detections,
                                                    const cv::Size& imageSize) {
    MultiTagResult result;

    if (!hasCalibration_ || !fieldLayout_ || detections.empty()) {
        return result;
    }

    // Collect all object points (3D in field) and image points (2D in image)
    std::vector<cv::Point3f> objectPoints;
    std::vector<cv::Point2f> imagePoints;
    std::vector<int> usedTagIds;

    for (const auto& det : detections) {
        // Get tag corners in field coordinates
        auto fieldCorners = getTagCornersInField(det.id);
        if (fieldCorners.empty()) {
            continue;  // Tag not in field layout
        }

        // Add corners to the point sets
        for (size_t i = 0; i < 4; i++) {
            objectPoints.push_back(fieldCorners[i]);
            imagePoints.push_back(det.corners[i]);
        }
        usedTagIds.push_back(det.id);
    }

    if (objectPoints.size() < 4) {
        return result;  // Need at least one tag
    }

    // Solve PnP
    cv::Vec3d rvec, tvec;
    bool success = false;

    // STRATEGY 1: Multi-Tag (8+ points)
    // The geometry is rigid, so the solution is usually unique and stable.
    if (objectPoints.size() >= 8) {
        // Use RANSAC to throw out a tag if it's jittery/garbage
        cv::Mat inliers;
        success = cv::solvePnPRansac(
            objectPoints, imagePoints,
            cameraMatrix_, distCoeffs_,
            rvec, tvec,
            hasPrevPose_, // Use previous frame as guess
            100,  // iterations
            static_cast<float>(config_.ransac_reproj_threshold),
            0.99,  // confidence
            inliers,
            cv::SOLVEPNP_SQPNP
        );
    } 
    // STRATEGY 2: Single-Tag (4 points)
    else {
        // Use regular solvePnP for single tag
        success = cv::solvePnP(
            objectPoints, imagePoints,
            cameraMatrix_, distCoeffs_,
            rvec, tvec,
            hasPrevPose_,
            cv::SOLVEPNP_SQPNP
        );
    }

    if (!success) {
        return result;
    }

    // Calculate reprojection error
    std::vector<cv::Point2f> projectedPoints;
    cv::projectPoints(objectPoints, rvec, tvec, cameraMatrix_, distCoeffs_, projectedPoints);

    double totalError = 0.0;
    for (size_t i = 0; i < imagePoints.size(); i++) {
        double dx = imagePoints[i].x - projectedPoints[i].x;
        double dy = imagePoints[i].y - projectedPoints[i].y;
        totalError += std::sqrt(dx * dx + dy * dy);
    }
    result.reprojectionError = totalError / imagePoints.size();

    // Convert directly from OpenCV PnP result to FRC Field Coordinates
    // This returns the position of the CAMERA on the FIELD.
    // The RoboRIO will handle adding the robot-to-camera offset.
    result.robotPose = CoordinateUtils::solvePnPToFieldPose(rvec, tvec);

    result.valid = true;
    result.tagsUsed = static_cast<int>(usedTagIds.size());
    result.tagIds = usedTagIds;

    // Save for next iteration
    prevRvec_ = rvec;
    prevTvec_ = tvec;
    hasPrevPose_ = true;

    return result;
}

} // namespace vision
