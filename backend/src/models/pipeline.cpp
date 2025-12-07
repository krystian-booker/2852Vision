#include "models/pipeline.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

namespace vision {

// AprilTagConfig
nlohmann::json AprilTagConfig::toJson() const {
    return {
        {"family", family},
        {"tag_size_m", tag_size_m},
        {"threads", threads},
        {"decimate", decimate},
        {"blur", blur},
        {"refine_edges", refine_edges},
        {"decision_margin", decision_margin},
        {"pose_iterations", pose_iterations},
        {"ransac_reproj_threshold", ransac_reproj_threshold},
        {"selected_field", selected_field}
    };
}

AprilTagConfig AprilTagConfig::fromJson(const nlohmann::json& j) {
    AprilTagConfig cfg;
    cfg.family = j.value("family", "tag36h11");
    cfg.tag_size_m = j.value("tag_size_m", 0.1524);
    cfg.threads = j.value("threads", 4);
    cfg.decimate = j.value("decimate", 2.0);
    cfg.blur = j.value("blur", 0.0);
    cfg.refine_edges = j.value("refine_edges", true);
    cfg.decision_margin = j.value("decision_margin", 35);
    cfg.pose_iterations = j.value("pose_iterations", 50);
    cfg.ransac_reproj_threshold = j.value("ransac_reproj_threshold", 0.1);
    cfg.selected_field = j.value("selected_field", "");
    return cfg;
}

// ObjectDetectionMLConfig
nlohmann::json ObjectDetectionMLConfig::toJson() const {
    return {
        {"model_type", model_type},
        {"model_filename", model_filename},
        {"labels_filename", labels_filename},
        {"confidence_threshold", confidence_threshold},
        {"nms_iou_threshold", nms_iou_threshold},
        {"img_size", img_size},
        {"max_detections", max_detections},
        {"accelerator", accelerator},
        {"target_classes", target_classes}
    };
}

ObjectDetectionMLConfig ObjectDetectionMLConfig::fromJson(const nlohmann::json& j) {
    ObjectDetectionMLConfig cfg;
    cfg.model_type = j.value("model_type", "yolo");
    cfg.model_filename = j.value("model_filename", "");
    cfg.labels_filename = j.value("labels_filename", "");
    cfg.confidence_threshold = j.value("confidence_threshold", 0.5);
    cfg.nms_iou_threshold = j.value("nms_iou_threshold", 0.45);
    cfg.img_size = j.value("img_size", 640);
    cfg.max_detections = j.value("max_detections", 100);
    cfg.accelerator = j.value("accelerator", "none");
    if (j.contains("target_classes")) {
        cfg.target_classes = j["target_classes"].get<std::vector<std::string>>();
    }
    return cfg;
}

// OpticalFlowConfig
nlohmann::json OpticalFlowConfig::toJson() const {
    return {
        {"algorithm", algorithm},
        {"camera_height_m", camera_height_m},
        {"camera_yaw_deg", camera_yaw_deg},
        {"lk_max_corners", lk_max_corners},
        {"lk_quality_level", lk_quality_level},
        {"lk_min_distance", lk_min_distance},
        {"lk_win_size", lk_win_size},
        {"lk_max_level", lk_max_level},
        {"fb_pyr_scale", fb_pyr_scale},
        {"fb_levels", fb_levels},
        {"fb_win_size", fb_win_size},
        {"fb_iterations", fb_iterations},
        {"fb_poly_n", fb_poly_n},
        {"fb_poly_sigma", fb_poly_sigma},
        {"max_velocity_mps", max_velocity_mps},
        {"min_features", min_features},
        {"smoothing_alpha", smoothing_alpha}
    };
}

OpticalFlowConfig OpticalFlowConfig::fromJson(const nlohmann::json& j) {
    OpticalFlowConfig cfg;
    cfg.algorithm = j.value("algorithm", OpticalFlowAlgorithm::LucasKanade);
    cfg.camera_height_m = j.value("camera_height_m", 0.1);
    cfg.camera_yaw_deg = j.value("camera_yaw_deg", 0.0);
    cfg.lk_max_corners = j.value("lk_max_corners", 100);
    cfg.lk_quality_level = j.value("lk_quality_level", 0.01);
    cfg.lk_min_distance = j.value("lk_min_distance", 10.0);
    cfg.lk_win_size = j.value("lk_win_size", 21);
    cfg.lk_max_level = j.value("lk_max_level", 3);
    cfg.fb_pyr_scale = j.value("fb_pyr_scale", 0.5);
    cfg.fb_levels = j.value("fb_levels", 3);
    cfg.fb_win_size = j.value("fb_win_size", 15);
    cfg.fb_iterations = j.value("fb_iterations", 3);
    cfg.fb_poly_n = j.value("fb_poly_n", 5);
    cfg.fb_poly_sigma = j.value("fb_poly_sigma", 1.2);
    cfg.max_velocity_mps = j.value("max_velocity_mps", 5.0);
    cfg.min_features = j.value("min_features", 10);
    cfg.smoothing_alpha = j.value("smoothing_alpha", 0.3);
    return cfg;
}

// Pipeline
nlohmann::json Pipeline::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["pipeline_type"] = pipeline_type;
    j["config"] = config;
    j["camera_id"] = camera_id;
    return j;
}

Pipeline Pipeline::fromJson(const nlohmann::json& j) {
    Pipeline p;
    p.id = j.value("id", 0);
    p.name = j.at("name").get<std::string>();
    p.pipeline_type = j.at("pipeline_type").get<PipelineType>();
    p.config = j.value("config", "{}");
    p.camera_id = j.at("camera_id").get<int>();
    return p;
}

Pipeline Pipeline::fromRow(const SQLite::Statement& query) {
    Pipeline p;
    p.id = query.getColumn("id").getInt();
    p.name = query.getColumn("name").getString();

    std::string typeStr = query.getColumn("pipeline_type").getString();
    if (typeStr == "AprilTag") p.pipeline_type = PipelineType::AprilTag;
    else if (typeStr == "Object Detection (ML)") p.pipeline_type = PipelineType::ObjectDetectionML;
    else if (typeStr == "Optical Flow") p.pipeline_type = PipelineType::OpticalFlow;

    if (!query.getColumn("config").isNull()) {
        p.config = query.getColumn("config").getString();
    }
    p.camera_id = query.getColumn("camera_id").getInt();

    return p;
}

nlohmann::json Pipeline::getConfigJson() const {
    if (config.empty()) {
        return nlohmann::json::object();
    }
    try {
        return nlohmann::json::parse(config);
    } catch (...) {
        return nlohmann::json::object();
    }
}

void Pipeline::setConfigJson(const nlohmann::json& configJson) {
    config = configJson.dump();
}

AprilTagConfig Pipeline::getAprilTagConfig() const {
    return AprilTagConfig::fromJson(getConfigJson());
}

ObjectDetectionMLConfig Pipeline::getObjectDetectionMLConfig() const {
    return ObjectDetectionMLConfig::fromJson(getConfigJson());
}

OpticalFlowConfig Pipeline::getOpticalFlowConfig() const {
    return OpticalFlowConfig::fromJson(getConfigJson());
}

} // namespace vision
