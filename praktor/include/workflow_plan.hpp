#pragma once

#include "data/workflow_value.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Praktor::Plan {

struct WorkflowDependency {
    std::string path;
    std::string relative_path;
    std::string sha256;
    std::vector<std::string> roles;
    std::uint64_t size = 0;
};

class WorkflowPlan {
public:
    WorkflowPlan() = default;
    WorkflowPlan(std::string root_path,
                 std::string root_directory,
                 std::string digest,
                 std::vector<WorkflowDependency> dependencies,
                 WorkflowValue input_schema = WorkflowValue::object(),
                 WorkflowValue output_schema = WorkflowValue::object(),
                 WorkflowValue effect_manifest = WorkflowValue::object(),
                 WorkflowValue profiles = WorkflowValue::object())
        : root_path_(std::move(root_path)),
          root_directory_(std::move(root_directory)),
          digest_(std::move(digest)),
          dependencies_(std::move(dependencies)),
          input_schema_(std::move(input_schema)),
          output_schema_(std::move(output_schema)),
          effect_manifest_(std::move(effect_manifest)),
          profiles_(std::move(profiles)) {}

    static WorkflowPlan compile(const std::filesystem::path& workflow_path);

    const std::string& rootPath() const noexcept { return root_path_; }
    const std::string& rootDirectory() const noexcept { return root_directory_; }
    const std::string& digest() const noexcept { return digest_; }
    const std::vector<WorkflowDependency>& dependencies() const noexcept {
        return dependencies_;
    }
    const WorkflowValue& inputSchema() const noexcept { return input_schema_; }
    const WorkflowValue& outputSchema() const noexcept { return output_schema_; }
    const WorkflowValue& effectManifest() const noexcept { return effect_manifest_; }
    const WorkflowValue& profiles() const noexcept { return profiles_; }

    bool validate(std::string* error_message = nullptr) const;
    WorkflowValue toValue() const;

private:
    std::string root_path_;
    std::string root_directory_;
    std::string digest_;
    std::vector<WorkflowDependency> dependencies_;
    WorkflowValue input_schema_{WorkflowValue::object()};
    WorkflowValue output_schema_{WorkflowValue::object()};
    WorkflowValue effect_manifest_{WorkflowValue::object()};
    WorkflowValue profiles_{WorkflowValue::object()};
};

} // namespace Praktor::Plan
