#include "workflow_profile.hpp"

#include <algorithm>
#include <utility>

namespace Praktor::Profile {

void ProfileReport::reject(std::string reason) {
    qualified = false;
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(std::move(reason));
    }
}

WorkflowValue ProfileReport::toValue() const {
    WorkflowValue value = WorkflowValue::object();
    value["qualified"] = qualified;

    WorkflowValue reason_values = WorkflowValue::array();
    for (const auto& reason : reasons) {
        reason_values.push_back(reason);
    }
    value["reasons"] = std::move(reason_values);
    return value;
}

ProfileReport evaluateHarnessSafe(
    const Workflow& workflow,
    const Praktor::Effects::EffectManifest& effects) {
    ProfileReport report;

    if (!workflow.strict_inputs) {
        report.reject(
            "harness_safe requires input_policy: strict");
    }
    if (workflow.outputs.empty()) {
        report.reject(
            "harness_safe requires at least one declared public output");
    }
    if (effects.unknown_effects) {
        report.reject(
            "harness_safe rejects workflows with unknown effects");
    }
    if (effects.effects.count("outside_workspace") != 0) {
        report.reject(
            "harness_safe rejects proven outside-workspace access");
    }
    if (effects.effects.count("native_extension") != 0) {
        report.reject(
            "harness_safe rejects native extension execution");
    }
    if (effects.effects.count("model_api") != 0) {
        report.reject(
            "harness_safe rejects direct model-provider calls");
    }

    return report;
}

} // namespace Praktor::Profile
