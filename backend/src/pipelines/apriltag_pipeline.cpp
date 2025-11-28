#define _USE_MATH_DEFINES
#include "pipelines/apriltag_pipeline.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <cmath>

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
    if (config_.auto_threads) {
        detector_->nthreads = static_cast<int>(std::thread::hardware_concurrency());
    } else {
        detector_->nthreads = config_.threads;
    }

    detector_->quad_decimate = config_.decimate;
    detector_->quad_sigma = config_.blur;
    detector_->refine_edges = config_.refine_edges ? 1 : 0;
    detector_->decode_sharpening = 0.25;

    spdlog::info("AprilTag detector initialized - family: {}, threads: {}, decimate: {:.1f}",
                 config_.family, detector_->nthreads, config_.decimate);
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

    // Collect valid detections first (before cloning frame)
    struct DetectionData {
        apriltag_detection_t* det;
        std::vector<cv::Point> corners;
    };
    std::vector<DetectionData> validDetections;

    for (int i = 0; i < zarray_size(detections); i++) {
        apriltag_detection_t* det;
        zarray_get(detections, i, &det);

        // Check decision margin threshold
        if (det->decision_margin < config_.decision_margin) {
            continue;
        }

        DetectionData data;
        data.det = det;
        for (int j = 0; j < 4; j++) {
            data.corners.push_back(cv::Point(
                static_cast<int>(det->p[j][0]),
                static_cast<int>(det->p[j][1])
            ));
        }
        validDetections.push_back(std::move(data));
    }

    // Clone frame for annotation, ensuring it's BGR for colored drawing
    if (frame.channels() == 1) {
        cv::cvtColor(frame, result.annotatedFrame, cv::COLOR_GRAY2BGR);
    } else {
        result.annotatedFrame = frame.clone();
    }

    // Process and draw each valid detection
    for (const auto& data : validDetections) {
        apriltag_detection_t* det = data.det;
        const auto& corners = data.corners;

        // Draw polygon
        cv::polylines(result.annotatedFrame, corners, true, cv::Scalar(0, 255, 0), 2);

        // Draw center point
        cv::circle(result.annotatedFrame,
                   cv::Point(static_cast<int>(det->c[0]), static_cast<int>(det->c[1])),
                   5, cv::Scalar(0, 0, 255), -1);

        // Draw ID
        cv::putText(result.annotatedFrame,
                    std::to_string(det->id),
                    cv::Point(static_cast<int>(det->c[0] - 10), static_cast<int>(det->c[1] - 10)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2);

        // Build detection JSON
        nlohmann::json detection;
        detection["id"] = det->id;
        detection["decision_margin"] = det->decision_margin;
        detection["hamming"] = det->hamming;
        detection["center"] = {det->c[0], det->c[1]};

        // Corners
        nlohmann::json cornersJson = nlohmann::json::array();
        for (int j = 0; j < 4; j++) {
            cornersJson.push_back({det->p[j][0], det->p[j][1]});
        }
        detection["corners"] = cornersJson;

        // Pose estimation if calibrated
        if (hasCalibration_) {
            // 3D Object Points for the tag (centered at origin, z=0)
            // Order: bottom-left, bottom-right, top-right, top-left (CCW)
            // Note: In camera coordinates (Y-down), Bottom-Left is (-half, +half)
            double halfSize = config_.tag_size_m / 2.0;
            std::vector<cv::Point3f> objectPoints = {
                cv::Point3f(-halfSize,  halfSize, 0), // Bottom-Left
                cv::Point3f( halfSize,  halfSize, 0), // Bottom-Right
                cv::Point3f( halfSize, -halfSize, 0), // Top-Right
                cv::Point3f(-halfSize, -halfSize, 0)  // Top-Left
            };

            // Image Points from detection
            std::vector<cv::Point2f> imagePoints;
            for (const auto& corner : data.corners) {
                imagePoints.push_back(corner);
            }

            cv::Vec3d rvec, tvec;
            // Use SQPNP as it is robust and accurate, avoiding the strict point ordering checks of IPPE_SQUARE
            bool success = cv::solvePnP(objectPoints, imagePoints, cameraMatrix_, distCoeffs_, rvec, tvec, false, cv::SOLVEPNP_SQPNP);

            if (success) {
                // Convert rotation vector to rotation matrix
                cv::Mat R;
                cv::Rodrigues(rvec, R);

                // Extract translation
                detection["pose"] = {
                    {"x", tvec[0]},
                    {"y", tvec[1]},
                    {"z", tvec[2]},
                    {"error", 0.0} // solvePnP doesn't return error directly
                };

                // Calculate Euler angles (Roll, Pitch, Yaw)
                // R = [ r00 r01 r02 ]
                //     [ r10 r11 r12 ]
                //     [ r20 r21 r22 ]
                double r00 = R.at<double>(0, 0);
                double r10 = R.at<double>(1, 0);
                double r20 = R.at<double>(2, 0);
                double r21 = R.at<double>(2, 1);
                double r22 = R.at<double>(2, 2);

                double pitch = std::atan2(r21, r22);
                double yaw = std::atan2(-r20, std::sqrt(r21*r21 + r22*r22));
                double roll = std::atan2(r10, r00);

                // Convert to degrees
                double pitchDeg = pitch * 180.0 / M_PI;
                double yawDeg = yaw * 180.0 / M_PI;
                double rollDeg = roll * 180.0 / M_PI;

                // Add 3D pose object
                detection["pose_3d"] = {
                    {"translation", {
                        {"x", tvec[0]},
                        {"y", tvec[1]},
                        {"z", tvec[2]}
                    }},
                    {"rotation", {
                        {"roll", rollDeg},
                        {"pitch", pitchDeg},
                        {"yaw", yawDeg}
                    }}
                };

                // Extract rotation matrix for JSON
                nlohmann::json rotation = nlohmann::json::array();
                for (int r = 0; r < 3; r++) {
                    nlohmann::json row = nlohmann::json::array();
                    for (int c = 0; c < 3; c++) {
                        row.push_back(R.at<double>(r, c));
                    }
                    rotation.push_back(row);
                }
                detection["rotation"] = rotation;

                // Draw 3D Cube
                // Define 3D points for the cube
                // Base points (Z=0) - same as objectPoints
                // Top points (Z=-tag_size) - extruding "into" the tag (or out depending on convention)
                double size = config_.tag_size_m;
                std::vector<cv::Point3f> cubePoints = {
                    // Base (Z=0)
                    cv::Point3f(-halfSize, -halfSize, 0),
                    cv::Point3f( halfSize, -halfSize, 0),
                    cv::Point3f( halfSize,  halfSize, 0),
                    cv::Point3f(-halfSize,  halfSize, 0),
                    // Top (Z=-size)
                    cv::Point3f(-halfSize, -halfSize, -size),
                    cv::Point3f( halfSize, -halfSize, -size),
                    cv::Point3f( halfSize,  halfSize, -size),
                    cv::Point3f(-halfSize,  halfSize, -size)
                };

                std::vector<cv::Point2f> imagePointsCube;
                cv::projectPoints(cubePoints, rvec, tvec, cameraMatrix_, distCoeffs_, imagePointsCube);

                // Draw lines
                cv::Scalar cubeColor(0, 255, 0);
                int thickness = 2;

                // Pillars
                for (int i = 0; i < 4; i++) {
                    cv::line(result.annotatedFrame, imagePointsCube[i], imagePointsCube[i+4], cubeColor, thickness);
                }

                // Top face
                for (int i = 0; i < 4; i++) {
                    cv::line(result.annotatedFrame, imagePointsCube[i+4], imagePointsCube[((i+1)%4)+4], cubeColor, thickness);
                }
            }
        }

        result.detections.push_back(detection);
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

    if (newConfig.auto_threads) {
        newDetector->nthreads = static_cast<int>(std::thread::hardware_concurrency());
    } else {
        newDetector->nthreads = newConfig.threads;
    }

    newDetector->quad_decimate = newConfig.decimate;
    newDetector->quad_sigma = newConfig.blur;
    newDetector->refine_edges = newConfig.refine_edges ? 1 : 0;
    newDetector->decode_sharpening = 0.25;

    // Now swap - this won't throw
    // Remove family from old detector before destruction
    if (detector_ && family_) {
        apriltag_detector_remove_family(detector_.get(), family_.get());
    }

    config_ = std::move(newConfig);
    family_ = std::move(newFamily);
    detector_ = std::move(newDetector);

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
    bool useExtrinsicGuess = hasPrevPose_ && objectPoints.size() >= 8;  // Use guess if multiple tags

    if (useExtrinsicGuess) {
        rvec = prevRvec_;
        tvec = prevTvec_;
    }

    cv::Mat inliers;
    bool success = false;

    if (config_.multi_tag_enabled && objectPoints.size() >= 8) {
        // Use RANSAC for multiple tags
        success = cv::solvePnPRansac(
            objectPoints, imagePoints,
            cameraMatrix_, distCoeffs_,
            rvec, tvec,
            useExtrinsicGuess,
            100,  // iterations
            static_cast<float>(config_.ransac_reproj_threshold),
            0.99,  // confidence
            inliers,
            cv::SOLVEPNP_SQPNP
        );
    } else {
        // Use regular solvePnP for single tag
        success = cv::solvePnP(
            objectPoints, imagePoints,
            cameraMatrix_, distCoeffs_,
            rvec, tvec,
            useExtrinsicGuess,
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

    // Convert to Pose3d
    // Note: rvec/tvec give the transformation from field to camera
    // We want camera pose in field, so we need to invert
    Pose3d cameraInField = Pose3d::fromOpenCV(rvec, tvec).inverse();

    // Convert from OpenCV coordinates to FRC field coordinates
    result.robotPose = CoordinateSystem::cameraToField(cameraInField);
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
