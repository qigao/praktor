#include "workflow_profile.h"
#include <string.h>
#include <tinytest.h>

spec("C workflow profile policy") {
    it("qualifies strict workflows with declared outputs and no forbidden effects") {
        const praktor_profile_input input = {1, 2u, 0, 0u};
        const uint32_t reasons = praktor_profile_evaluate_harness_safe(&input);
        check_equal(reasons, 0u);
        check_equal(praktor_profile_reason_count(reasons), (size_t)0u);
        check_null(praktor_profile_reason_at(reasons, 0u));
    }
    it("returns all rejection reasons in deterministic order") {
        const praktor_profile_input input = {
            0, 0u, 1,
            PRAKTOR_PROFILE_EFFECT_OUTSIDE_WORKSPACE |
            PRAKTOR_PROFILE_EFFECT_NATIVE_EXTENSION |
            PRAKTOR_PROFILE_EFFECT_MODEL_API
        };
        const uint32_t reasons = praktor_profile_evaluate_harness_safe(&input);
        const char *expected[] = {
            "harness_safe requires input_policy: strict",
            "harness_safe requires at least one declared public output",
            "harness_safe rejects workflows with unknown effects",
            "harness_safe rejects proven outside-workspace access",
            "harness_safe rejects native extension execution",
            "harness_safe rejects direct model-provider calls"
        };
        size_t i;
        check_equal(praktor_profile_reason_count(reasons), sizeof(expected) / sizeof(expected[0]));
        for (i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i)
            check(strcmp(praktor_profile_reason_at(reasons, i), expected[i]) == 0);
        check_null(praktor_profile_reason_at(reasons, sizeof(expected) / sizeof(expected[0])));
    }
    it("fails closed for unknown effect bits and missing input") {
        const praktor_profile_input unknown = {1, 1u, 0, UINT32_C(0x80000000)};
        const uint32_t unknown_reasons = praktor_profile_evaluate_harness_safe(&unknown);
        const uint32_t null_reasons = praktor_profile_evaluate_harness_safe(NULL);
        check_equal(unknown_reasons, PRAKTOR_PROFILE_REASON_UNKNOWN_EFFECTS);
        check_equal(null_reasons,
            PRAKTOR_PROFILE_REASON_STRICT_INPUTS |
            PRAKTOR_PROFILE_REASON_OUTPUTS_REQUIRED |
            PRAKTOR_PROFILE_REASON_UNKNOWN_EFFECTS);
    }
}
