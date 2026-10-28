#include "workflow_profile.h"

typedef struct profile_reason_entry {
    uint32_t bit;
    const char *text;
} profile_reason_entry;

static const profile_reason_entry PROFILE_REASONS[] = {
    {PRAKTOR_PROFILE_REASON_STRICT_INPUTS,
     "harness_safe requires input_policy: strict"},
    {PRAKTOR_PROFILE_REASON_OUTPUTS_REQUIRED,
     "harness_safe requires at least one declared public output"},
    {PRAKTOR_PROFILE_REASON_UNKNOWN_EFFECTS,
     "harness_safe rejects workflows with unknown effects"},
    {PRAKTOR_PROFILE_REASON_OUTSIDE_WORKSPACE,
     "harness_safe rejects proven outside-workspace access"},
    {PRAKTOR_PROFILE_REASON_NATIVE_EXTENSION,
     "harness_safe rejects native extension execution"},
    {PRAKTOR_PROFILE_REASON_MODEL_API,
     "harness_safe rejects direct model-provider calls"}
};

#define PROFILE_KNOWN_EFFECTS (PRAKTOR_PROFILE_EFFECT_OUTSIDE_WORKSPACE | \
                               PRAKTOR_PROFILE_EFFECT_NATIVE_EXTENSION | \
                               PRAKTOR_PROFILE_EFFECT_MODEL_API)

uint32_t praktor_profile_evaluate_harness_safe(const praktor_profile_input *input) {
    uint32_t reasons = 0u;
    if (input == NULL || input->strict_inputs != 1)
        reasons |= PRAKTOR_PROFILE_REASON_STRICT_INPUTS;
    if (input == NULL || input->output_count == 0u)
        reasons |= PRAKTOR_PROFILE_REASON_OUTPUTS_REQUIRED;
    if (input == NULL || input->unknown_effects != 0 ||
        (input->effects & ~PROFILE_KNOWN_EFFECTS) != 0u)
        reasons |= PRAKTOR_PROFILE_REASON_UNKNOWN_EFFECTS;
    if (input != NULL && (input->effects & PRAKTOR_PROFILE_EFFECT_OUTSIDE_WORKSPACE) != 0u)
        reasons |= PRAKTOR_PROFILE_REASON_OUTSIDE_WORKSPACE;
    if (input != NULL && (input->effects & PRAKTOR_PROFILE_EFFECT_NATIVE_EXTENSION) != 0u)
        reasons |= PRAKTOR_PROFILE_REASON_NATIVE_EXTENSION;
    if (input != NULL && (input->effects & PRAKTOR_PROFILE_EFFECT_MODEL_API) != 0u)
        reasons |= PRAKTOR_PROFILE_REASON_MODEL_API;
    return reasons;
}

size_t praktor_profile_reason_count(uint32_t reasons) {
    size_t i, count = 0u;
    for (i = 0u; i < sizeof(PROFILE_REASONS) / sizeof(PROFILE_REASONS[0]); ++i)
        if ((reasons & PROFILE_REASONS[i].bit) != 0u) ++count;
    return count;
}

const char *praktor_profile_reason_at(uint32_t reasons, size_t index) {
    size_t i, current = 0u;
    for (i = 0u; i < sizeof(PROFILE_REASONS) / sizeof(PROFILE_REASONS[0]); ++i) {
        if ((reasons & PROFILE_REASONS[i].bit) == 0u) continue;
        if (current++ == index) return PROFILE_REASONS[i].text;
    }
    return NULL;
}
