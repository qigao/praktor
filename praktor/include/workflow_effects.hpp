#pragma once

#include "data/workflow_value.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace Praktor::Effects {

struct EffectManifest {
    std::set<std::string> effects;
    bool unknown_effects = false;
    std::vector<std::string> unknown_reasons;

    void add(std::string effect);
    void merge(const EffectManifest& other);
    WorkflowValue toValue() const;
};

EffectManifest analyzeWorkflow(const std::filesystem::path& workflow_path);

} // namespace Praktor::Effects
