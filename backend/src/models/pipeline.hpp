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
    ColouredShape,
    ObjectDetectionML
};

NLOHMANN_JSON_SERIALIZE_ENUM(PipelineType, {
    {PipelineType::AprilTag, "AprilTag"},
    {PipelineType::ColouredShape, "Coloured Shape"},
    {PipelineType::ObjectDetectionML, "Object Detection (ML)"}
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

// Coloured shape configuration
struct ColouredShapeConfig {
    int hue_min = 0;
    int hue_max = 180;
    int saturation_min = 100;
    int saturation_max = 255;
    int value_min = 100;
    int value_max = 255;
    int area_min = 100;
    int area_max = 100000;
    double aspect_ratio_min = 0.0;
    double aspect_ratio_max = 10.0;
    double fullness_min = 0.0;
    double fullness_max = 1.0;

    nlohmann::json toJson() const;
    static ColouredShapeConfig fromJson(const nlohmann::json& j);
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
    ColouredShapeConfig getColouredShapeConfig() const;
    ObjectDetectionMLConfig getObjectDetectionMLConfig() const;
};

} // namespace vision
