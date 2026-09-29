#pragma once

#include "data/workflow_value.hpp"
#include "workflow_effects.hpp"
#include "yml/task.hpp"

#include <string>
#include <vector>

namespace Praktor::Profile {

struct ProfileReport {
    bool qualified = true;
    std::vector<std::string> reasons;

    void reject(std::string reason);
    WorkflowValue toValue() const;
};

ProfileReport evaluateHarnessSafe(
    const Workflow& workflow,
    const Praktor::Effects::EffectManifest& effects);

} // namespace Praktor::Profile
