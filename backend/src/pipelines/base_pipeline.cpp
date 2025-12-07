#include "pipelines/base_pipeline.hpp"
#include "pipelines/apriltag_pipeline.hpp"
#include "pipelines/object_detection_ml_pipeline.hpp"
#include "pipelines/optical_flow_pipeline.hpp"
#include <spdlog/spdlog.h>

namespace vision {

std::unique_ptr<BasePipeline> BasePipeline::create(const Pipeline& pipeline) {
    // Use default FOV values
    return create(pipeline, 60.0, 45.0);
}

std::unique_ptr<BasePipeline> BasePipeline::create(const Pipeline& pipeline,
                                                     double horizontalFov,
                                                     double verticalFov) {
    switch (pipeline.pipeline_type) {
        case PipelineType::AprilTag: {
            auto config = pipeline.getAprilTagConfig();
            return std::make_unique<AprilTagPipeline>(config);
        }

        case PipelineType::ObjectDetectionML: {
            auto config = pipeline.getObjectDetectionMLConfig();
            return std::make_unique<ObjectDetectionMLPipeline>(config, horizontalFov, verticalFov);
        }

        case PipelineType::OpticalFlow: {
            auto config = pipeline.getOpticalFlowConfig();
            return std::make_unique<OpticalFlowPipeline>(config);
        }

        default:
            spdlog::error("Unknown pipeline type");
            return nullptr;
    }
}

} // namespace vision
