#include "pipelines/coloured_shape_pipeline.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vision {

ColouredShapePipeline::ColouredShapePipeline() {
    spdlog::debug("Coloured Shape pipeline initialized with default config");
}

ColouredShapePipeline::ColouredShapePipeline(const ColouredShapeConfig& config)
    : config_(config) {
    spdlog::debug("Coloured Shape pipeline initialized - H:[{}-{}] S:[{}-{}] V:[{}-{}]",
                  config_.hue_min, config_.hue_max,
                  config_.saturation_min, config_.saturation_max,
                  config_.value_min, config_.value_max);
}

cv::Mat ColouredShapePipeline::createMask(const cv::Mat& hsv) {
    cv::Mat mask;

    // Handle hue wrap-around (e.g., red spans 170-10)
    if (config_.hue_min > config_.hue_max) {
        // Split into two ranges
        cv::Mat mask1, mask2;
        cv::inRange(hsv,
                    cv::Scalar(config_.hue_min, config_.saturation_min, config_.value_min),
                    cv::Scalar(180, config_.saturation_max, config_.value_max),
                    mask1);
        cv::inRange(hsv,
                    cv::Scalar(0, config_.saturation_min, config_.value_min),
                    cv::Scalar(config_.hue_max, config_.saturation_max, config_.value_max),
                    mask2);
        cv::bitwise_or(mask1, mask2, mask);
    } else {
        cv::inRange(hsv,
                    cv::Scalar(config_.hue_min, config_.saturation_min, config_.value_min),
                    cv::Scalar(config_.hue_max, config_.saturation_max, config_.value_max),
                    mask);
    }

    // Morphological operations to clean up mask
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    return mask;
}

std::vector<std::vector<cv::Point>> ColouredShapePipeline::findContours(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    return contours;
}

nlohmann::json ColouredShapePipeline::analyzeContour(const std::vector<cv::Point>& contour,
                                                      const cv::Mat& frame) {
    nlohmann::json detection;

    // Calculate area
    double area = cv::contourArea(contour);
    detection["area"] = area;

    // Calculate bounding rect
    cv::Rect boundingRect = cv::boundingRect(contour);
    detection["bounding_rect"] = {
        {"x", boundingRect.x},
        {"y", boundingRect.y},
        {"width", boundingRect.width},
        {"height", boundingRect.height}
    };

    // Calculate center
    cv::Moments moments = cv::moments(contour);
    double cx = 0.0, cy = 0.0;
    if (moments.m00 > 0) {
        cx = moments.m10 / moments.m00;
        cy = moments.m01 / moments.m00;
    } else {
        // Fall back to bounding rect center for zero-area contours
        cx = boundingRect.x + boundingRect.width / 2.0;
        cy = boundingRect.y + boundingRect.height / 2.0;
    }
    detection["center"] = {cx, cy};

    // Calculate aspect ratio
    double aspectRatio = static_cast<double>(boundingRect.width) / boundingRect.height;
    detection["aspect_ratio"] = aspectRatio;

    // Calculate fullness (area / bounding rect area)
    double fullness = area / (boundingRect.width * boundingRect.height);
    detection["fullness"] = fullness;

    // Calculate rotated rectangle for angle
    cv::RotatedRect rotatedRect = cv::minAreaRect(contour);
    double angle = rotatedRect.angle;
    if (rotatedRect.size.width < rotatedRect.size.height) {
        angle += 90.0;
    }
    detection["angle"] = angle;

    // Calculate perimeter
    double perimeter = cv::arcLength(contour, true);
    detection["perimeter"] = perimeter;

    // Circularity = 4 * pi * area / perimeter^2
    double circularity = (4.0 * M_PI * area) / (perimeter * perimeter);
    detection["circularity"] = circularity;

    // Classify shape based on properties
    std::string shape = "unknown";
    if (circularity > 0.85) {
        shape = "circle";
    } else if (fullness > 0.9 && aspectRatio > 0.9 && aspectRatio < 1.1) {
        shape = "square";
    } else if (fullness > 0.9) {
        shape = "rectangle";
    } else if (fullness > 0.4 && fullness < 0.6) {
        shape = "triangle";
    }
    detection["shape"] = shape;

    // Calculate position relative to frame center
    double frameWidth = frame.cols;
    double frameHeight = frame.rows;
    double relativeX = (cx - frameWidth / 2.0) / (frameWidth / 2.0);
    double relativeY = (cy - frameHeight / 2.0) / (frameHeight / 2.0);
    detection["relative_position"] = {relativeX, relativeY};

    return detection;
}

PipelineResult ColouredShapePipeline::process(const cv::Mat& frame,
                                               const std::optional<cv::Mat>& depth) {
    auto startTime = std::chrono::high_resolution_clock::now();

    PipelineResult result;
    result.detections = nlohmann::json::array();

    // Convert to HSV
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

    // Create color mask
    cv::Mat mask = createMask(hsv);

    // Find contours
    auto contours = findContours(mask);

    // Collect valid detections first before cloning frame
    std::vector<std::pair<std::vector<cv::Point>, nlohmann::json>> validDetections;

    // Process each contour
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);

        // Filter by area
        if (area < config_.area_min || area > config_.area_max) {
            continue;
        }

        // Analyze contour
        auto detection = analyzeContour(contour, frame);

        // Filter by aspect ratio
        double aspectRatio = detection["aspect_ratio"].get<double>();
        if (aspectRatio < config_.aspect_ratio_min || aspectRatio > config_.aspect_ratio_max) {
            continue;
        }

        // Filter by fullness
        double fullness = detection["fullness"].get<double>();
        if (fullness < config_.fullness_min || fullness > config_.fullness_max) {
            continue;
        }

        validDetections.emplace_back(contour, std::move(detection));
    }

    // Only clone frame if we have detections or need to draw on it
    result.annotatedFrame = frame.clone();

    // Draw all valid detections
    for (auto& [contour, detection] : validDetections) {
        // Draw contour
        cv::drawContours(result.annotatedFrame, std::vector<std::vector<cv::Point>>{contour},
                         0, cv::Scalar(0, 255, 0), 2);

        // Draw bounding rect
        auto rect = detection["bounding_rect"];
        cv::rectangle(result.annotatedFrame,
                      cv::Point(rect["x"].get<int>(), rect["y"].get<int>()),
                      cv::Point(rect["x"].get<int>() + rect["width"].get<int>(),
                                rect["y"].get<int>() + rect["height"].get<int>()),
                      cv::Scalar(255, 0, 0), 1);

        // Draw center
        auto center = detection["center"];
        cv::circle(result.annotatedFrame,
                   cv::Point(static_cast<int>(center[0].get<double>()),
                             static_cast<int>(center[1].get<double>())),
                   5, cv::Scalar(0, 0, 255), -1);

        // Draw shape label
        std::string label = detection["shape"].get<std::string>();
        cv::putText(result.annotatedFrame, label,
                    cv::Point(rect["x"].get<int>(), rect["y"].get<int>() - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);

        result.detections.push_back(std::move(detection));
    }

    // Draw detection count
    cv::putText(result.annotatedFrame,
                "Shapes: " + std::to_string(result.detections.size()),
                cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(0, 255, 0), 2);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

void ColouredShapePipeline::updateConfig(const nlohmann::json& config) {
    config_ = ColouredShapeConfig::fromJson(config);
    spdlog::debug("Coloured Shape config updated - H:[{}-{}] S:[{}-{}] V:[{}-{}]",
                  config_.hue_min, config_.hue_max,
                  config_.saturation_min, config_.saturation_max,
                  config_.value_min, config_.value_max);
}

} // namespace vision
