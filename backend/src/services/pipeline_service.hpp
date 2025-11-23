#pragma once

#include "models/pipeline.hpp"
#include <vector>
#include <optional>

namespace vision {

class PipelineService {
public:
    // Singleton access
    static PipelineService& instance();

    // CRUD operations
    std::vector<Pipeline> getAllPipelines();
    std::vector<Pipeline> getPipelinesForCamera(int cameraId);
    std::optional<Pipeline> getPipelineById(int id);
    Pipeline createPipeline(Pipeline& pipeline);
    bool updatePipeline(const Pipeline& pipeline);
    bool updatePipelineConfig(int id, const nlohmann::json& config);
    bool deletePipeline(int id);

    // Get default config for pipeline type
    static nlohmann::json getDefaultConfig(PipelineType type);

private:
    PipelineService() = default;
};

} // namespace vision
