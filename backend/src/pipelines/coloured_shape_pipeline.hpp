#pragma once

#include "pipelines/base_pipeline.hpp"
#include "models/pipeline.hpp"

namespace vision {

class ColouredShapePipeline : public BasePipeline {
public:
    ColouredShapePipeline();
    explicit ColouredShapePipeline(const ColouredShapeConfig& config);
    ~ColouredShapePipeline() override = default;

    PipelineResult process(const cv::Mat& frame,
                          const std::optional<cv::Mat>& depth = std::nullopt) override;

    void updateConfig(const nlohmann::json& config) override;

    PipelineType type() const override { return PipelineType::ColouredShape; }

private:
    ColouredShapeConfig config_;

    // Helper functions
    cv::Mat createMask(const cv::Mat& hsv);
    std::vector<std::vector<cv::Point>> findContours(const cv::Mat& mask);
    nlohmann::json analyzeContour(const std::vector<cv::Point>& contour, const cv::Mat& frame);
};

} // namespace vision
