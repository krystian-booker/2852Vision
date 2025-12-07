#include "pipelines/optical_flow_pipeline.hpp"
#include <spdlog/spdlog.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vision {

OpticalFlowPipeline::OpticalFlowPipeline() : config_() {
    spdlog::info("OpticalFlowPipeline created with default config");
}

OpticalFlowPipeline::OpticalFlowPipeline(const OpticalFlowConfig& config)
    : config_(config) {
    spdlog::info("OpticalFlowPipeline created with algorithm: {}",
                 config_.algorithm == OpticalFlowAlgorithm::LucasKanade ? "Lucas-Kanade" : "Farneback");
}

void OpticalFlowPipeline::updateConfig(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = OpticalFlowConfig::fromJson(config);
    spdlog::info("OpticalFlowPipeline config updated");
}

OpticalFlowResult OpticalFlowPipeline::getFlowResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastResult_;
}

void OpticalFlowPipeline::detectFeatures(const cv::Mat& gray) {
    prevPoints_.clear();
    cv::goodFeaturesToTrack(
        gray,
        prevPoints_,
        config_.lk_max_corners,
        config_.lk_quality_level,
        config_.lk_min_distance
    );
}

std::tuple<double, double, int> OpticalFlowPipeline::processLucasKanade(const cv::Mat& gray) {
    if (prevPoints_.empty()) {
        return {0.0, 0.0, 0};
    }

    std::vector<cv::Point2f> currPoints;
    std::vector<uchar> status;
    std::vector<float> err;

    cv::Size winSize(config_.lk_win_size, config_.lk_win_size);

    cv::calcOpticalFlowPyrLK(
        prevGray_,
        gray,
        prevPoints_,
        currPoints,
        status,
        err,
        winSize,
        config_.lk_max_level,
        cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.01)
    );

    // Calculate average displacement from tracked points
    std::vector<cv::Point2f> displacements;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            cv::Point2f disp = currPoints[i] - prevPoints_[i];
            displacements.push_back(disp);
        }
    }

    if (displacements.empty()) {
        return {0.0, 0.0, 0};
    }

    // Filter outliers using median-based rejection
    if (displacements.size() >= 3) {
        std::vector<double> magnitudes;
        for (const auto& d : displacements) {
            magnitudes.push_back(cv::norm(d));
        }

        std::vector<double> sorted_mags = magnitudes;
        std::sort(sorted_mags.begin(), sorted_mags.end());
        double median = sorted_mags[sorted_mags.size() / 2];

        // Compute MAD (Median Absolute Deviation)
        std::vector<double> deviations;
        for (double m : magnitudes) {
            deviations.push_back(std::abs(m - median));
        }
        std::sort(deviations.begin(), deviations.end());
        double mad = deviations[deviations.size() / 2] * 1.4826; // Scale to sigma

        // Filter based on 2.5 sigma
        double threshold = 2.5 * std::max(mad, 1.0);
        std::vector<cv::Point2f> filtered;
        for (size_t i = 0; i < displacements.size(); i++) {
            if (std::abs(magnitudes[i] - median) < threshold) {
                filtered.push_back(displacements[i]);
            }
        }
        displacements = filtered;
    }

    if (displacements.empty()) {
        return {0.0, 0.0, 0};
    }

    // Calculate mean displacement
    double dx = 0.0, dy = 0.0;
    for (const auto& d : displacements) {
        dx += d.x;
        dy += d.y;
    }
    dx /= displacements.size();
    dy /= displacements.size();

    // Update points for next frame - keep successfully tracked points
    std::vector<cv::Point2f> goodPoints;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            goodPoints.push_back(currPoints[i]);
        }
    }
    prevPoints_ = goodPoints;

    // Re-detect features if we've lost too many
    if (prevPoints_.size() < static_cast<size_t>(config_.min_features)) {
        detectFeatures(gray);
    }

    return {dx, dy, static_cast<int>(displacements.size())};
}

std::tuple<double, double, int> OpticalFlowPipeline::processFarneback(const cv::Mat& gray) {
    cv::Mat flow;

    cv::calcOpticalFlowFarneback(
        prevGray_,
        gray,
        flow,
        config_.fb_pyr_scale,
        config_.fb_levels,
        config_.fb_win_size,
        config_.fb_iterations,
        config_.fb_poly_n,
        config_.fb_poly_sigma,
        0
    );

    // Sample flow vectors from grid points
    const int gridStep = 20;
    std::vector<cv::Point2f> flowVectors;

    for (int y = gridStep; y < flow.rows - gridStep; y += gridStep) {
        for (int x = gridStep; x < flow.cols - gridStep; x += gridStep) {
            cv::Point2f fxy = flow.at<cv::Point2f>(y, x);
            if (cv::norm(fxy) < config_.max_velocity_mps * 100) { // Rough magnitude check
                flowVectors.push_back(fxy);
            }
        }
    }

    if (flowVectors.empty()) {
        return {0.0, 0.0, 0};
    }

    // Filter outliers
    if (flowVectors.size() >= 3) {
        std::vector<double> magnitudes;
        for (const auto& f : flowVectors) {
            magnitudes.push_back(cv::norm(f));
        }

        std::vector<double> sorted_mags = magnitudes;
        std::sort(sorted_mags.begin(), sorted_mags.end());
        double median = sorted_mags[sorted_mags.size() / 2];

        std::vector<double> deviations;
        for (double m : magnitudes) {
            deviations.push_back(std::abs(m - median));
        }
        std::sort(deviations.begin(), deviations.end());
        double mad = deviations[deviations.size() / 2] * 1.4826;

        double threshold = 2.5 * std::max(mad, 1.0);
        std::vector<cv::Point2f> filtered;
        for (size_t i = 0; i < flowVectors.size(); i++) {
            if (std::abs(magnitudes[i] - median) < threshold) {
                filtered.push_back(flowVectors[i]);
            }
        }
        flowVectors = filtered;
    }

    if (flowVectors.empty()) {
        return {0.0, 0.0, 0};
    }

    // Calculate mean flow
    double dx = 0.0, dy = 0.0;
    for (const auto& f : flowVectors) {
        dx += f.x;
        dy += f.y;
    }
    dx /= flowVectors.size();
    dy /= flowVectors.size();

    return {dx, dy, static_cast<int>(flowVectors.size())};
}

void OpticalFlowPipeline::pixelToRobotVelocity(double dx_px, double dy_px, double dt,
                                                double& vx_mps, double& vy_mps) const {
    // If no calibration, use a default approximation
    double fx = 500.0; // Default focal length estimate
    double fy = 500.0;

    if (hasCalibration_) {
        fx = cameraMatrix_.at<double>(0, 0);
        fy = cameraMatrix_.at<double>(1, 1);
    }

    // Convert pixel displacement to meters on carpet plane
    // Each pixel represents (height / focal_length) meters
    double dx_m = dx_px * config_.camera_height_m / fx;
    double dy_m = dy_px * config_.camera_height_m / fy;

    // Convert to velocity (negate because carpet moves opposite to robot)
    double vx_cam = -dx_m / dt;
    double vy_cam = -dy_m / dt;

    // Transform from camera frame to robot frame
    // Camera: +X right in image, +Y down in image
    // Robot:  +X forward, +Y left (FRC convention)
    // Assuming camera is mounted with image "up" pointing to robot forward:
    double vx_base = -vy_cam;  // Forward = -image Y direction
    double vy_base = -vx_cam;  // Left = -image X direction

    // Apply camera yaw rotation for non-standard mounting
    double yaw_rad = config_.camera_yaw_deg * M_PI / 180.0;
    double cos_yaw = std::cos(yaw_rad);
    double sin_yaw = std::sin(yaw_rad);

    vx_mps = vx_base * cos_yaw - vy_base * sin_yaw;
    vy_mps = vx_base * sin_yaw + vy_base * cos_yaw;
}

void OpticalFlowPipeline::drawVisualization(cv::Mat& frame,
                                             const std::vector<cv::Point2f>& currPoints,
                                             double vx, double vy, int features, bool valid) {
    // Draw tracked feature points
    for (const auto& pt : currPoints) {
        cv::circle(frame, pt, 3, cv::Scalar(0, 255, 0), -1);
    }

    // Draw velocity vector at center of frame
    cv::Point2f center(frame.cols / 2.0f, frame.rows / 2.0f);

    // Scale velocity for visualization (50 pixels per m/s)
    const float scale = 50.0f;
    // Convert robot velocity back to image direction for display
    // Robot +X forward = -image Y, Robot +Y left = -image X
    float arrow_dx = static_cast<float>(-vy * scale);  // Left/right in image
    float arrow_dy = static_cast<float>(-vx * scale);  // Up/down in image

    cv::Point2f arrowEnd(center.x + arrow_dx, center.y + arrow_dy);

    cv::Scalar arrowColor = valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
    cv::arrowedLine(frame, center, arrowEnd, arrowColor, 2, cv::LINE_AA, 0, 0.3);

    // Draw velocity text
    std::string velText = cv::format("Vx: %.2f m/s  Vy: %.2f m/s", vx, vy);
    cv::putText(frame, velText, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(255, 255, 255), 2);

    // Draw feature count and validity
    std::string statusText = cv::format("Features: %d  Valid: %s", features, valid ? "YES" : "NO");
    cv::putText(frame, statusText, cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);

    // Draw algorithm name
    std::string algoText = config_.algorithm == OpticalFlowAlgorithm::LucasKanade ?
                           "Algorithm: Lucas-Kanade" : "Algorithm: Farneback";
    cv::putText(frame, algoText, cv::Point(10, 90), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(200, 200, 200), 1);
}

PipelineResult OpticalFlowPipeline::process(const cv::Mat& frame,
                                             const std::optional<cv::Mat>& /*depth*/) {
    PipelineResult result;
    result.annotatedFrame = frame.clone();

    auto now = std::chrono::steady_clock::now();
    frameCount_++;

    // Convert to grayscale
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame.clone();
    }

    // First frame initialization
    if (!initialized_) {
        prevGray_ = gray.clone();
        prevTimestamp_ = now;
        initialized_ = true;

        if (config_.algorithm == OpticalFlowAlgorithm::LucasKanade) {
            detectFeatures(gray);
        }

        // Return invalid result for first frame
        result.detections = {
            {"valid", false},
            {"reason", "initializing"},
            {"vx_mps", 0.0},
            {"vy_mps", 0.0},
            {"features", 0}
        };
        result.processingTimeMs = 0;

        drawVisualization(result.annotatedFrame, prevPoints_, 0, 0, 0, false);

        std::lock_guard<std::mutex> lock(mutex_);
        lastResult_ = OpticalFlowResult{};
        return result;
    }

    // Calculate time delta
    double dt = std::chrono::duration<double>(now - prevTimestamp_).count();

    // Validate dt - reject if too small or too large
    if (dt < 0.001 || dt > 0.5) {
        spdlog::warn("OpticalFlow: invalid dt={:.3f}s, skipping frame", dt);
        prevTimestamp_ = now;
        prevGray_ = gray.clone();

        result.detections = {
            {"valid", false},
            {"reason", "invalid_dt"},
            {"vx_mps", 0.0},
            {"vy_mps", 0.0},
            {"features", 0}
        };
        result.processingTimeMs = 0;

        std::lock_guard<std::mutex> lock(mutex_);
        lastResult_.valid = false;
        return result;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Process optical flow based on algorithm selection
    double dx_px, dy_px;
    int validVectors;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (config_.algorithm == OpticalFlowAlgorithm::LucasKanade) {
            std::tie(dx_px, dy_px, validVectors) = processLucasKanade(gray);
        } else {
            std::tie(dx_px, dy_px, validVectors) = processFarneback(gray);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double processingMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Check if we have enough valid vectors
    bool valid = validVectors >= config_.min_features;
    double vx_mps = 0.0, vy_mps = 0.0;

    if (valid) {
        // Convert pixel displacement to robot-frame velocity
        pixelToRobotVelocity(dx_px, dy_px, dt, vx_mps, vy_mps);

        // Check velocity magnitude
        double speed = std::sqrt(vx_mps * vx_mps + vy_mps * vy_mps);
        if (speed > config_.max_velocity_mps) {
            spdlog::warn("OpticalFlow: excessive velocity {:.2f} m/s, rejecting", speed);
            valid = false;
            vx_mps = 0.0;
            vy_mps = 0.0;
        } else {
            // Apply exponential smoothing
            smoothedVx_ = config_.smoothing_alpha * vx_mps + (1.0 - config_.smoothing_alpha) * smoothedVx_;
            smoothedVy_ = config_.smoothing_alpha * vy_mps + (1.0 - config_.smoothing_alpha) * smoothedVy_;
            vx_mps = smoothedVx_;
            vy_mps = smoothedVy_;
        }
    }

    // Update previous frame state
    prevGray_ = gray.clone();
    prevTimestamp_ = now;

    // Draw visualization
    drawVisualization(result.annotatedFrame, prevPoints_, vx_mps, vy_mps, validVectors, valid);

    // Build result JSON
    result.detections = {
        {"valid", valid},
        {"vx_mps", vx_mps},
        {"vy_mps", vy_mps},
        {"features", validVectors},
        {"algorithm", config_.algorithm == OpticalFlowAlgorithm::LucasKanade ? "LucasKanade" : "Farneback"},
        {"dt_ms", dt * 1000.0}
    };
    result.processingTimeMs = processingMs;

    // Update last result (thread-safe)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastResult_.vx_mps = vx_mps;
        lastResult_.vy_mps = vy_mps;
        lastResult_.valid_vectors = validVectors;
        lastResult_.valid = valid;
        lastResult_.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
    }

    return result;
}

} // namespace vision
