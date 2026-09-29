#pragma once

#include "data/workflow_value.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
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
                 std::vector<WorkflowDependency> dependencies);

    static WorkflowPlan compile(const std::filesystem::path& workflow_path);

    const std::string& rootPath() const noexcept { return root_path_; }
    const std::string& rootDirectory() const noexcept { return root_directory_; }
    const std::string& digest() const noexcept { return digest_; }
    const std::vector<WorkflowDependency>& dependencies() const noexcept {
        return dependencies_;
    }

    bool validate(std::string* error_message = nullptr) const;
    WorkflowValue toValue() const;

private:
    std::string root_path_;
    std::string root_directory_;
    std::string digest_;
    std::vector<WorkflowDependency> dependencies_;
};

} // namespace Praktor::Plan
