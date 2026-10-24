#ifndef PRAKTOR_WORKFLOW_PROFILE_H
#define PRAKTOR_WORKFLOW_PROFILE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum praktor_profile_effect {
    PRAKTOR_PROFILE_EFFECT_OUTSIDE_WORKSPACE = 1u << 0,
    PRAKTOR_PROFILE_EFFECT_NATIVE_EXTENSION = 1u << 1,
    PRAKTOR_PROFILE_EFFECT_MODEL_API = 1u << 2
};
enum praktor_profile_reason {
    PRAKTOR_PROFILE_REASON_STRICT_INPUTS = 1u << 0,
    PRAKTOR_PROFILE_REASON_OUTPUTS_REQUIRED = 1u << 1,
    PRAKTOR_PROFILE_REASON_UNKNOWN_EFFECTS = 1u << 2,
    PRAKTOR_PROFILE_REASON_OUTSIDE_WORKSPACE = 1u << 3,
    PRAKTOR_PROFILE_REASON_NATIVE_EXTENSION = 1u << 4,
    PRAKTOR_PROFILE_REASON_MODEL_API = 1u << 5
};
typedef struct praktor_profile_input {
    int strict_inputs;
    size_t output_count;
    int unknown_effects;
    uint32_t effects;
} praktor_profile_input;

/* Pure C policy: no Workflow/EffectManifest objects, allocation, or C++
 * ownership crosses this boundary. Unknown effect bits fail closed. */
uint32_t praktor_profile_evaluate_harness_safe(const praktor_profile_input *input);
size_t praktor_profile_reason_count(uint32_t reasons);
const char *praktor_profile_reason_at(uint32_t reasons, size_t index);
#ifdef __cplusplus
}
#endif
#endif
