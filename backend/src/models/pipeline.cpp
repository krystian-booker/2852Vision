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

// ColouredShapeConfig
nlohmann::json ColouredShapeConfig::toJson() const {
    return {
        {"hue_min", hue_min},
        {"hue_max", hue_max},
        {"saturation_min", saturation_min},
        {"saturation_max", saturation_max},
        {"value_min", value_min},
        {"value_max", value_max},
        {"area_min", area_min},
        {"area_max", area_max},
        {"aspect_ratio_min", aspect_ratio_min},
        {"aspect_ratio_max", aspect_ratio_max},
        {"fullness_min", fullness_min},
        {"fullness_max", fullness_max}
    };
}

ColouredShapeConfig ColouredShapeConfig::fromJson(const nlohmann::json& j) {
    ColouredShapeConfig cfg;
    cfg.hue_min = j.value("hue_min", 0);
    cfg.hue_max = j.value("hue_max", 180);
    cfg.saturation_min = j.value("saturation_min", 100);
    cfg.saturation_max = j.value("saturation_max", 255);
    cfg.value_min = j.value("value_min", 100);
    cfg.value_max = j.value("value_max", 255);
    cfg.area_min = j.value("area_min", 100);
    cfg.area_max = j.value("area_max", 100000);
    cfg.aspect_ratio_min = j.value("aspect_ratio_min", 0.0);
    cfg.aspect_ratio_max = j.value("aspect_ratio_max", 10.0);
    cfg.fullness_min = j.value("fullness_min", 0.0);
    cfg.fullness_max = j.value("fullness_max", 1.0);
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
    else if (typeStr == "Coloured Shape") p.pipeline_type = PipelineType::ColouredShape;
    else if (typeStr == "Object Detection (ML)") p.pipeline_type = PipelineType::ObjectDetectionML;

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

ColouredShapeConfig Pipeline::getColouredShapeConfig() const {
    return ColouredShapeConfig::fromJson(getConfigJson());
}

ObjectDetectionMLConfig Pipeline::getObjectDetectionMLConfig() const {
    return ObjectDetectionMLConfig::fromJson(getConfigJson());
}

} // namespace vision
