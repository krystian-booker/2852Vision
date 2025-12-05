#include "pipelines/base_pipeline.hpp"
#include "pipelines/apriltag_pipeline.hpp"
#include "pipelines/object_detection_ml_pipeline.hpp"
#include <spdlog/spdlog.h>

namespace vision {

std::unique_ptr<BasePipeline> BasePipeline::create(const Pipeline& pipeline) {
    switch (pipeline.pipeline_type) {
        case PipelineType::AprilTag: {
            auto config = pipeline.getAprilTagConfig();
            return std::make_unique<AprilTagPipeline>(config);
        }

        case PipelineType::ObjectDetectionML: {
            auto config = pipeline.getObjectDetectionMLConfig();
            return std::make_unique<ObjectDetectionMLPipeline>(config);
        }

        default:
            spdlog::error("Unknown pipeline type");
            return nullptr;
    }
}

} // namespace vision
