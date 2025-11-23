#pragma once

#include "utils/geometry.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace vision {

// A single tag's position in the field
struct FieldTag {
    int id;
    Pose3d pose;  // Pose in field coordinates

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["ID"] = id;
        j["pose"] = pose.toJson();
        return j;
    }

    static FieldTag fromJson(const nlohmann::json& j) {
        FieldTag tag;
        tag.id = j.at("ID").get<int>();
        tag.pose = Pose3d::fromJson(j.at("pose"));
        return tag;
    }
};

// Complete field layout containing all tag positions
class FieldLayout {
public:
    FieldLayout() = default;

    // Load from JSON file
    static std::optional<FieldLayout> loadFromFile(const std::string& filepath);

    // Load from JSON object
    static std::optional<FieldLayout> fromJson(const nlohmann::json& j);

    // Get tag pose by ID
    std::optional<Pose3d> getTagPose(int tagId) const;

    // Check if tag exists
    bool hasTag(int tagId) const;

    // Get all tag IDs
    std::vector<int> getTagIds() const;

    // Get number of tags
    size_t size() const { return tags_.size(); }

    // Get layout name
    const std::string& name() const { return name_; }

    // Set layout name
    void setName(const std::string& name) { name_ = name; }

    // Serialize to JSON
    nlohmann::json toJson() const;

private:
    std::string name_;
    std::map<int, FieldTag> tags_;
};

// Service for managing available field layouts
class FieldLayoutService {
public:
    static FieldLayoutService& instance();

    // Initialize with fields directory
    void initialize(const std::string& fieldsDir);

    // Get list of available field names
    std::vector<std::string> getAvailableFields() const;

    // Load a specific field layout
    std::optional<FieldLayout> getFieldLayout(const std::string& fieldName) const;

    // Get currently selected field
    const std::string& getSelectedField() const { return selectedField_; }

    // Set selected field
    void setSelectedField(const std::string& fieldName);

    // Get the currently loaded field layout
    std::optional<FieldLayout> getCurrentLayout() const;

private:
    FieldLayoutService() = default;
    static FieldLayoutService instance_;

    std::string fieldsDir_;
    std::string selectedField_;
    mutable std::optional<FieldLayout> cachedLayout_;
    mutable std::string cachedFieldName_;
};

} // namespace vision
