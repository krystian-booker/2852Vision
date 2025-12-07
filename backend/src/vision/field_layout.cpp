#include "vision/field_layout.hpp"
#include <spdlog/spdlog.h>
#include <fstream>

namespace vision {

// FieldLayout implementation

std::optional<FieldLayout> FieldLayout::loadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            spdlog::error("Failed to open field layout file: {}", filepath);
            return std::nullopt;
        }

        nlohmann::json j = nlohmann::json::parse(file);
        auto layout = fromJson(j);

        if (layout) {
            // Extract name from filename
            std::filesystem::path path(filepath);
            layout->setName(path.stem().string());
            spdlog::info("Loaded field layout '{}' with {} tags",
                         layout->name(), layout->size());
        }

        return layout;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse field layout file '{}': {}", filepath, e.what());
        return std::nullopt;
    }
}

std::optional<FieldLayout> FieldLayout::fromJson(const nlohmann::json& j) {
    try {
        FieldLayout layout;

        if (!j.contains("tags") || !j["tags"].is_array()) {
            spdlog::error("Field layout JSON missing 'tags' array");
            return std::nullopt;
        }

        for (const auto& tagJson : j["tags"]) {
            FieldTag tag = FieldTag::fromJson(tagJson);
            layout.tags_[tag.id] = tag;
        }

        return layout;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse field layout JSON: {}", e.what());
        return std::nullopt;
    }
}

std::optional<Pose3d> FieldLayout::getTagPose(int tagId) const {
    auto it = tags_.find(tagId);
    if (it != tags_.end()) {
        return it->second.pose;
    }
    return std::nullopt;
}

bool FieldLayout::hasTag(int tagId) const {
    return tags_.find(tagId) != tags_.end();
}

std::vector<int> FieldLayout::getTagIds() const {
    std::vector<int> ids;
    ids.reserve(tags_.size());
    for (const auto& [id, tag] : tags_) {
        ids.push_back(id);
    }
    return ids;
}

nlohmann::json FieldLayout::toJson() const {
    nlohmann::json j;
    j["tags"] = nlohmann::json::array();

    for (const auto& [id, tag] : tags_) {
        j["tags"].push_back(tag.toJson());
    }

    return j;
}

// FieldLayoutService implementation

FieldLayoutService FieldLayoutService::instance_;

FieldLayoutService& FieldLayoutService::instance() {
    return instance_;
}

void FieldLayoutService::initialize(const std::string& fieldsDir) {
    fieldsDir_ = fieldsDir;

    // Create directory if it doesn't exist
    if (!std::filesystem::exists(fieldsDir_)) {
        std::filesystem::create_directories(fieldsDir_);
        spdlog::info("Created fields directory: {}", fieldsDir_);
    }

    // List available fields
    auto fields = getAvailableFields();
    spdlog::info("Found {} field layouts in {}", fields.size(), fieldsDir_);

    for (const auto& field : fields) {
        spdlog::debug("  - {}", field);
    }
}

std::vector<std::string> FieldLayoutService::getAvailableFields() const {
    std::vector<std::string> fields;

    if (!std::filesystem::exists(fieldsDir_)) {
        return fields;
    }

    for (const auto& entry : std::filesystem::directory_iterator(fieldsDir_)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".json") {
                fields.push_back(entry.path().stem().string());
            }
        }
    }

    // Sort alphabetically
    std::sort(fields.begin(), fields.end());

    return fields;
}

std::optional<FieldLayout> FieldLayoutService::getFieldLayout(const std::string& fieldName) const {
    std::string filepath = (std::filesystem::path(fieldsDir_) / fieldName).string();

    if (!std::filesystem::exists(filepath)) {
        spdlog::warn("Field layout not found: {}", filepath);
        return std::nullopt;
    }

    return FieldLayout::loadFromFile(filepath);
}

void FieldLayoutService::setSelectedField(const std::string& fieldName) {
    selectedField_ = fieldName;

    // Clear cache to force reload
    cachedLayout_.reset();
    cachedFieldName_.clear();

    spdlog::info("Selected field layout: {}", fieldName.empty() ? "(none)" : fieldName);
}

std::optional<FieldLayout> FieldLayoutService::getCurrentLayout() const {
    if (selectedField_.empty()) {
        return std::nullopt;
    }

    // Return cached layout if available
    if (cachedLayout_ && cachedFieldName_ == selectedField_) {
        return cachedLayout_;
    }

    // Load and cache
    auto layout = getFieldLayout(selectedField_);
    if (layout) {
        cachedLayout_ = layout;
        cachedFieldName_ = selectedField_;
    }

    return layout;
}

} // namespace vision
