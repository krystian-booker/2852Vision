#include "pipelines/object_detection_ml_pipeline.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <numeric>

namespace vision {

nlohmann::json Detection::toJson() const {
    return nlohmann::json{
        {"label", label},
        {"confidence", confidence},
        {"box", {x1, y1, x2, y2}}
    };
}

// ================== OnnxYoloBackend ==================

OnnxYoloBackend::OnnxYoloBackend(
    const std::string& modelPath,
    const std::string& provider,
    int imgSize,
    float confThreshold,
    float nmsIouThreshold,
    int maxDetections,
    const std::vector<std::string>& classNames,
    const std::vector<std::string>& targetClasses)
    : env_(ORT_LOGGING_LEVEL_WARNING, "ObjectDetection")
    , imgSize_(imgSize)
    , confThreshold_(confThreshold)
    , nmsIouThreshold_(nmsIouThreshold)
    , maxDetections_(maxDetections)
    , classNames_(classNames)
    , targetClasses_(targetClasses.begin(), targetClasses.end())
{
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Configure execution provider
    std::vector<std::string> providers;
    providers.push_back(provider);
    if (provider != "CPUExecutionProvider") {
        providers.push_back("CPUExecutionProvider");
    }

    // Set up providers
    if (provider == "CUDAExecutionProvider") {
        OrtCUDAProviderOptions cudaOptions;
        sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
    } else if (provider == "TensorrtExecutionProvider") {
        OrtTensorRTProviderOptions trtOptions;
        sessionOptions.AppendExecutionProvider_TensorRT(trtOptions);
    }
    // CPUExecutionProvider is always available as fallback

    // Create session
    // On Windows, Ort::Session requires wide string path
#ifdef _WIN32
    std::wstring wideModelPath(modelPath.begin(), modelPath.end());
    session_ = std::make_unique<Ort::Session>(env_, wideModelPath.c_str(), sessionOptions);
#else
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), sessionOptions);
#endif

    // Get input name and shape
    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNamePtr = session_->GetInputNameAllocated(0, allocator);
    inputName_ = inputNamePtr.get();

    auto inputTypeInfo = session_->GetInputTypeInfo(0);
    auto tensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
    inputShape_ = tensorInfo.GetShape();

    spdlog::info("ONNX model loaded: {} with provider {}", modelPath, provider);
    spdlog::debug("Input shape: [{}, {}, {}, {}]",
                  inputShape_[0], inputShape_[1], inputShape_[2], inputShape_[3]);
}

std::tuple<cv::Mat, float, float, float> OnnxYoloBackend::letterboxImage(const cv::Mat& image) {
    int origHeight = image.rows;
    int origWidth = image.cols;

    float scale = std::min(static_cast<float>(imgSize_) / origHeight,
                          static_cast<float>(imgSize_) / origWidth);

    int newWidth = static_cast<int>(origWidth * scale);
    int newHeight = static_cast<int>(origHeight * scale);

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);

    int padW = imgSize_ - newWidth;
    int padH = imgSize_ - newHeight;
    float padLeft = padW / 2.0f;
    float padTop = padH / 2.0f;

    cv::Mat padded;
    cv::copyMakeBorder(resized, padded,
                       static_cast<int>(std::floor(padTop)),
                       static_cast<int>(std::ceil(padTop)),
                       static_cast<int>(std::floor(padLeft)),
                       static_cast<int>(std::ceil(padLeft)),
                       cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

    return {padded, scale, padLeft, padTop};
}

std::vector<int> OnnxYoloBackend::nonMaxSuppression(
    const std::vector<cv::Rect>& boxes,
    const std::vector<float>& scores,
    float iouThreshold)
{
    if (boxes.empty()) return {};

    // Sort indices by score (descending)
    std::vector<int> indices(scores.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&scores](int a, int b) {
        return scores[a] > scores[b];
    });

    std::vector<int> keep;
    std::vector<bool> suppressed(boxes.size(), false);

    for (int idx : indices) {
        if (suppressed[idx]) continue;
        if (keep.size() >= static_cast<size_t>(maxDetections_)) break;

        keep.push_back(idx);
        const cv::Rect& boxA = boxes[idx];

        for (size_t j = 0; j < indices.size(); ++j) {
            int jdx = indices[j];
            if (suppressed[jdx] || jdx == idx) continue;

            const cv::Rect& boxB = boxes[jdx];
            cv::Rect intersection = boxA & boxB;
            float interArea = static_cast<float>(intersection.area());
            float unionArea = static_cast<float>(boxA.area() + boxB.area() - interArea);

            if (unionArea > 0 && (interArea / unionArea) > iouThreshold) {
                suppressed[jdx] = true;
            }
        }
    }

    return keep;
}

std::vector<Detection> OnnxYoloBackend::postprocessYolo(
    const float* output,
    const std::vector<int64_t>& outputShape,
    float scale,
    float padX,
    float padY,
    int origWidth,
    int origHeight)
{
    // YOLOv5 output shape: [1, num_detections, 5 + num_classes]
    // Each detection: [x, y, w, h, objectness, class_scores...]
    int numDetections = static_cast<int>(outputShape[1]);
    int numClasses = static_cast<int>(outputShape[2]) - 5;

    if (numClasses <= 0) {
        spdlog::warn("Invalid output shape for YOLO postprocessing");
        return {};
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;

    for (int i = 0; i < numDetections; ++i) {
        const float* det = output + i * outputShape[2];

        float objectness = det[4];
        if (objectness < confThreshold_) continue;

        // Find best class
        int bestClass = 0;
        float bestScore = det[5];
        for (int c = 1; c < numClasses; ++c) {
            if (det[5 + c] > bestScore) {
                bestScore = det[5 + c];
                bestClass = c;
            }
        }

        float confidence = objectness * bestScore;
        if (confidence < confThreshold_) continue;

        // Convert xywh to xyxy
        float cx = det[0];
        float cy = det[1];
        float w = det[2];
        float h = det[3];

        float x1 = cx - w / 2;
        float y1 = cy - h / 2;
        float x2 = cx + w / 2;
        float y2 = cy + h / 2;

        // Undo letterbox
        x1 = (x1 - padX) / scale;
        y1 = (y1 - padY) / scale;
        x2 = (x2 - padX) / scale;
        y2 = (y2 - padY) / scale;

        // Clip to image bounds
        x1 = std::clamp(x1, 0.0f, static_cast<float>(origWidth - 1));
        y1 = std::clamp(y1, 0.0f, static_cast<float>(origHeight - 1));
        x2 = std::clamp(x2, 0.0f, static_cast<float>(origWidth - 1));
        y2 = std::clamp(y2, 0.0f, static_cast<float>(origHeight - 1));

        boxes.push_back(cv::Rect(
            static_cast<int>(x1),
            static_cast<int>(y1),
            static_cast<int>(x2 - x1),
            static_cast<int>(y2 - y1)
        ));
        scores.push_back(confidence);
        classIds.push_back(bestClass);
    }

    // Apply NMS
    std::vector<int> keepIndices = nonMaxSuppression(boxes, scores, nmsIouThreshold_);

    // Build result
    std::vector<Detection> detections;
    for (int idx : keepIndices) {
        int classId = classIds[idx];
        std::string label = (classId >= 0 && classId < static_cast<int>(classNames_.size()))
            ? classNames_[classId]
            : "class_" + std::to_string(classId);

        // Filter by target classes if specified
        if (!targetClasses_.empty() && targetClasses_.find(label) == targetClasses_.end()) {
            continue;
        }

        const cv::Rect& box = boxes[idx];
        detections.push_back({
            label,
            scores[idx],
            box.x,
            box.y,
            box.x + box.width,
            box.y + box.height
        });
    }

    return detections;
}

std::vector<Detection> OnnxYoloBackend::predict(const cv::Mat& frame) {
    // Convert to RGB
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

    // Letterbox
    auto [padded, scale, padX, padY] = letterboxImage(rgb);

    // Normalize to [0, 1] and convert to float
    cv::Mat blob;
    padded.convertTo(blob, CV_32F, 1.0 / 255.0);

    // Convert HWC to CHW
    std::vector<cv::Mat> channels(3);
    cv::split(blob, channels);

    // Create input tensor
    std::vector<float> inputTensor;
    inputTensor.reserve(3 * imgSize_ * imgSize_);
    for (int c = 0; c < 3; ++c) {
        inputTensor.insert(inputTensor.end(),
                          channels[c].begin<float>(),
                          channels[c].end<float>());
    }

    // Create ONNX tensor
    std::vector<int64_t> inputShape = {1, 3, imgSize_, imgSize_};
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputOrt = Ort::Value::CreateTensor<float>(
        memInfo,
        inputTensor.data(),
        inputTensor.size(),
        inputShape.data(),
        inputShape.size()
    );

    // Run inference
    const char* inputNames[] = {inputName_.c_str()};
    Ort::AllocatorWithDefaultOptions allocator;
    auto outputNamePtr = session_->GetOutputNameAllocated(0, allocator);
    const char* outputNames[] = {outputNamePtr.get()};

    auto outputs = session_->Run(
        Ort::RunOptions{nullptr},
        inputNames,
        &inputOrt,
        1,
        outputNames,
        1
    );

    // Get output data
    float* outputData = outputs[0].GetTensorMutableData<float>();
    auto outputShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();

    // Postprocess
    return postprocessYolo(
        outputData,
        outputShape,
        scale,
        padX,
        padY,
        frame.cols,
        frame.rows
    );
}

// ================== ObjectDetectionMLPipeline ==================

ObjectDetectionMLPipeline::ObjectDetectionMLPipeline(const ObjectDetectionMLConfig& config)
    : config_(config)
{
    loadLabels();
    createBackend();
}

void ObjectDetectionMLPipeline::loadLabels() {
    std::string labelsPath = resolveLabelsPath();
    if (labelsPath.empty() || !std::filesystem::exists(labelsPath)) {
        spdlog::warn("Labels file not found");
        return;
    }

    std::ifstream file(labelsPath);
    if (!file.is_open()) {
        spdlog::error("Failed to open labels file: {}", labelsPath);
        return;
    }

    classNames_.clear();
    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            classNames_.push_back(line);
        }
    }

    spdlog::info("Loaded {} class labels", classNames_.size());
}

std::string ObjectDetectionMLPipeline::resolveModelPath() {
    // Check if model_filename contains a path
    if (!config_.model_filename.empty()) {
        // Try as absolute path first
        if (std::filesystem::exists(config_.model_filename)) {
            return config_.model_filename;
        }

        // Try in data directory
        std::filesystem::path dataDir = std::filesystem::current_path() / "data" / "models";
        std::filesystem::path fullPath = dataDir / config_.model_filename;
        if (std::filesystem::exists(fullPath)) {
            return fullPath.string();
        }
    }

    return "";
}

std::string ObjectDetectionMLPipeline::resolveLabelsPath() {
    if (!config_.labels_filename.empty()) {
        // Try as absolute path first
        if (std::filesystem::exists(config_.labels_filename)) {
            return config_.labels_filename;
        }

        // Try in data directory
        std::filesystem::path dataDir = std::filesystem::current_path() / "data" / "models";
        std::filesystem::path fullPath = dataDir / config_.labels_filename;
        if (std::filesystem::exists(fullPath)) {
            return fullPath.string();
        }
    }

    return "";
}

void ObjectDetectionMLPipeline::createBackend() {
    backend_.reset();
    initError_.clear();

    if (config_.model_type != "yolo") {
        initError_ = "Only YOLO model type is currently supported";
        spdlog::error("{}", initError_);
        return;
    }

    std::string modelPath = resolveModelPath();
    if (modelPath.empty()) {
        initError_ = "Model file not configured or not found";
        spdlog::warn("{}", initError_);
        return;
    }

    try {
        // Determine provider
        std::string provider = "CPUExecutionProvider";
        if (config_.accelerator == "cuda") {
            provider = "CUDAExecutionProvider";
        } else if (config_.accelerator == "tensorrt") {
            provider = "TensorrtExecutionProvider";
        }

        backend_ = std::make_unique<OnnxYoloBackend>(
            modelPath,
            provider,
            config_.img_size,
            static_cast<float>(config_.confidence_threshold),
            static_cast<float>(config_.nms_iou_threshold),
            config_.max_detections,
            classNames_,
            config_.target_classes
        );

        spdlog::info("Object Detection ML pipeline initialized successfully");

    } catch (const std::exception& e) {
        initError_ = e.what();
        spdlog::error("Failed to create ML backend: {}", initError_);
    }
}

void ObjectDetectionMLPipeline::drawDetections(cv::Mat& frame, const std::vector<Detection>& detections) {
    for (const auto& det : detections) {
        // Draw bounding box
        cv::rectangle(frame,
                     cv::Point(det.x1, det.y1),
                     cv::Point(det.x2, det.y2),
                     cv::Scalar(0, 255, 0), 2);

        // Draw label with background
        std::string text = det.label + " " + std::to_string(static_cast<int>(det.confidence * 100)) + "%";
        int baseline;
        cv::Size textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);

        cv::rectangle(frame,
                     cv::Point(det.x1, det.y1 - textSize.height - 5),
                     cv::Point(det.x1 + textSize.width, det.y1),
                     cv::Scalar(0, 255, 0), cv::FILLED);

        cv::putText(frame, text,
                   cv::Point(det.x1, det.y1 - 3),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5,
                   cv::Scalar(0, 0, 0), 1);
    }
}

PipelineResult ObjectDetectionMLPipeline::process(const cv::Mat& frame,
                                                   const std::optional<cv::Mat>& depth) {
    PipelineResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    // Clone frame for annotation
    result.annotatedFrame = frame.clone();

    if (!backend_) {
        result.detections = nlohmann::json::array();
        if (!initError_.empty()) {
            spdlog::debug("ML pipeline not initialized: {}", initError_);
        }
        return result;
    }

    try {
        // Run detection
        std::vector<Detection> detections = backend_->predict(frame);

        // Convert to JSON
        nlohmann::json detectionsJson = nlohmann::json::array();
        for (const auto& det : detections) {
            detectionsJson.push_back(det.toJson());
        }
        result.detections = detectionsJson;

        // Draw on annotated frame
        drawDetections(result.annotatedFrame, detections);

    } catch (const std::exception& e) {
        spdlog::error("Error during ML inference: {}", e.what());
        result.detections = nlohmann::json::array();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

void ObjectDetectionMLPipeline::updateConfig(const nlohmann::json& configJson) {
    config_ = ObjectDetectionMLConfig::fromJson(configJson);
    loadLabels();
    createBackend();
}

} // namespace vision
