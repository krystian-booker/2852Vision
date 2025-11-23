#pragma once

#include "pipelines/base_pipeline.hpp"
#include "models/pipeline.hpp"
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <memory>

namespace vision {

// Detection result
struct Detection {
    std::string label;
    float confidence;
    int x1, y1, x2, y2;  // Bounding box

    nlohmann::json toJson() const;
};

// ONNX YOLO backend
class OnnxYoloBackend {
public:
    OnnxYoloBackend(const std::string& modelPath,
                    const std::string& provider,
                    int imgSize,
                    float confThreshold,
                    float nmsIouThreshold,
                    int maxDetections,
                    const std::vector<std::string>& classNames,
                    const std::vector<std::string>& targetClasses);

    std::vector<Detection> predict(const cv::Mat& frame);

private:
    std::unique_ptr<Ort::Session> session_;
    Ort::Env env_;
    std::string inputName_;
    std::vector<int64_t> inputShape_;

    int imgSize_;
    float confThreshold_;
    float nmsIouThreshold_;
    int maxDetections_;
    std::vector<std::string> classNames_;
    std::set<std::string> targetClasses_;

    // Preprocessing
    std::tuple<cv::Mat, float, float, float> letterboxImage(const cv::Mat& image);

    // Postprocessing
    std::vector<Detection> postprocessYolo(
        const float* output,
        const std::vector<int64_t>& outputShape,
        float scale,
        float padX,
        float padY,
        int origWidth,
        int origHeight);

    // Non-maximum suppression
    std::vector<int> nonMaxSuppression(
        const std::vector<cv::Rect>& boxes,
        const std::vector<float>& scores,
        float iouThreshold);
};

class ObjectDetectionMLPipeline : public BasePipeline {
public:
    explicit ObjectDetectionMLPipeline(const ObjectDetectionMLConfig& config);
    ~ObjectDetectionMLPipeline() override = default;

    PipelineResult process(const cv::Mat& frame,
                          const std::optional<cv::Mat>& depth = std::nullopt) override;

    void updateConfig(const nlohmann::json& config) override;

    PipelineType type() const override { return PipelineType::ObjectDetectionML; }

private:
    ObjectDetectionMLConfig config_;
    std::unique_ptr<OnnxYoloBackend> backend_;
    std::vector<std::string> classNames_;
    std::string initError_;

    void loadLabels();
    void createBackend();
    std::string resolveModelPath();
    std::string resolveLabelsPath();

    // Draw detections on frame
    void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections);
};

} // namespace vision
