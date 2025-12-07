#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

// Forward declaration
namespace SQLite {
    class Statement;
}

namespace vision {

enum class PipelineType {
    AprilTag,
    ObjectDetectionML,
    OpticalFlow
};

NLOHMANN_JSON_SERIALIZE_ENUM(PipelineType, {
    {PipelineType::AprilTag, "AprilTag"},
    {PipelineType::ObjectDetectionML, "Object Detection (ML)"},
    {PipelineType::OpticalFlow, "Optical Flow"}
})

// Optical Flow algorithm selection
enum class OpticalFlowAlgorithm {
    LucasKanade,
    Farneback
};

NLOHMANN_JSON_SERIALIZE_ENUM(OpticalFlowAlgorithm, {
    {OpticalFlowAlgorithm::LucasKanade, "LucasKanade"},
    {OpticalFlowAlgorithm::Farneback, "Farneback"}
})

// AprilTag configuration
struct AprilTagConfig {
    std::string family = "tag36h11";
    double tag_size_m = 0.1524;  // 6 inches default
    int threads = 4;
    double decimate = 2.0;
    double blur = 0.0;
    bool refine_edges = true;
    int decision_margin = 35;
    int pose_iterations = 50;
    double ransac_reproj_threshold = 0.1;
    std::string selected_field;

    nlohmann::json toJson() const;
    static AprilTagConfig fromJson(const nlohmann::json& j);
};

// ML object detection configuration
struct ObjectDetectionMLConfig {
    std::string model_type = "yolo";
    std::string model_filename;
    std::string labels_filename;
    double confidence_threshold = 0.5;
    double nms_iou_threshold = 0.45;
    int img_size = 640;
    int max_detections = 100;
    std::string accelerator = "none";
    std::vector<std::string> target_classes;

    nlohmann::json toJson() const;
    static ObjectDetectionMLConfig fromJson(const nlohmann::json& j);
};

// Optical Flow configuration for carpet odometry
struct OpticalFlowConfig {
    // Algorithm selection
    OpticalFlowAlgorithm algorithm = OpticalFlowAlgorithm::LucasKanade;

    // Camera mounting
    double camera_height_m = 0.1;        // Height above carpet (meters)
    double camera_yaw_deg = 0.0;         // Rotation about vertical axis (degrees)

    // Lucas-Kanade parameters
    int lk_max_corners = 100;            // Max feature points to track
    double lk_quality_level = 0.01;      // Feature detection quality (0-1)
    double lk_min_distance = 10.0;       // Min pixels between features
    int lk_win_size = 21;                // Optical flow window size
    int lk_max_level = 3;                // Pyramid levels

    // Farneback parameters
    double fb_pyr_scale = 0.5;           // Pyramid scale
    int fb_levels = 3;                   // Pyramid levels
    int fb_win_size = 15;                // Window size
    int fb_iterations = 3;               // Iterations per level
    int fb_poly_n = 5;                   // Polynomial expansion neighborhood
    double fb_poly_sigma = 1.2;          // Gaussian sigma for polynomial

    // Filtering
    double max_velocity_mps = 5.0;       // Reject velocities above this (m/s)
    int min_features = 10;               // Min features for valid estimate
    double smoothing_alpha = 0.3;        // Exponential smoothing (0=all old, 1=all new)

    nlohmann::json toJson() const;
    static OpticalFlowConfig fromJson(const nlohmann::json& j);
};

struct Pipeline {
    int id = 0;
    std::string name;
    PipelineType pipeline_type = PipelineType::AprilTag;
    std::string config;  // JSON string
    int camera_id = 0;

    // JSON serialization
    nlohmann::json toJson() const;
    static Pipeline fromJson(const nlohmann::json& j);

    // Database operations
    static Pipeline fromRow(const class SQLite::Statement& query);

    // Config helpers
    nlohmann::json getConfigJson() const;
    void setConfigJson(const nlohmann::json& configJson);

    // Get typed config
    AprilTagConfig getAprilTagConfig() const;
    ObjectDetectionMLConfig getObjectDetectionMLConfig() const;
    OpticalFlowConfig getOpticalFlowConfig() const;
};

} // namespace vision
