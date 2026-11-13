#include "praktor/process_retainer.h"
#include <stdlib.h>

typedef struct retained_process {
    uint64_t serial;
    process_async *process;
    void *payload;
    void (*cleanup)(void *);
} retained_process;
typedef struct retainer_state {
    const praktor_process_retainer *owner;
    size_t capacity, pending, cursor;
    bool driving;
    retained_process slots[];
} retainer_state;

static retainer_state *state_of(const praktor_process_retainer *retainer) {
    retainer_state *state = retainer ? retainer->impl : NULL;
    return state && state->owner == retainer ? state : NULL;
}
static retained_process *find_token(retainer_state *state,
    praktor_process_retainer_token token) {
    if (!state || token.owner != state->owner || !token.serial ||
        token.slot >= state->capacity || state->slots[token.slot].serial != token.serial)
        return NULL;
    return &state->slots[token.slot];
}
int praktor_process_retainer_init(praktor_process_retainer *retainer, size_t capacity) {
    retainer_state *state;
    if (!retainer || !capacity ||
        capacity > (SIZE_MAX - sizeof(*state)) / sizeof(retained_process))
        return SALTS_EINVAL;
    if (retainer->impl) return SALTS_EBUSY;
    state = calloc(1u, sizeof(*state) + capacity * sizeof(retained_process));
    if (!state) return SALTS_ENOMEM;
    state->owner = retainer;
    state->capacity = capacity;
    retainer->impl = state;
    return SALTS_OK;
}
int praktor_process_retainer_reserve(praktor_process_retainer *retainer,
    praktor_process_retainer_token *out) {
    retainer_state *state = state_of(retainer);
    if (out) *out = (praktor_process_retainer_token){0};
    if (!state || !out) return SALTS_EINVAL;
    if (state->driving) return SALTS_EBUSY;
    if (state->pending == state->capacity) return SALTS_ENOBUFS;
    if (retainer->serial == UINT64_MAX) return SALTS_ERANGE;
    for (size_t i = 0u; i < state->capacity; ++i) {
        if (state->slots[i].serial) continue;
        state->slots[i].serial = ++retainer->serial;
        ++state->pending;
        *out = (praktor_process_retainer_token){retainer, i, retainer->serial};
        return SALTS_OK;
    }
    return SALTS_EIO;
}
int praktor_process_retainer_release(praktor_process_retainer *retainer,
    praktor_process_retainer_token token) {
    retainer_state *state = state_of(retainer);
    retained_process *slot = find_token(state, token);
    if (!slot) return SALTS_EINVAL;
    if (state->driving || slot->process) return SALTS_EBUSY;
    *slot = (retained_process){0};
    --state->pending;
    return SALTS_OK;
}
int praktor_process_retainer_adopt(praktor_process_retainer *retainer,
    praktor_process_retainer_token token, process_async *process,
    void *payload, void (*cleanup)(void *)) {
    retainer_state *state = state_of(retainer);
    retained_process *slot = find_token(state, token);
    process_async_stats stats = {0};
    if (!slot || !process || !payload || !cleanup ||
        !process_async_get_stats(process, &stats) || !stats.close_requested)
        return SALTS_EINVAL;
    if (state->driving || slot->process) return SALTS_EBUSY;
    for (size_t i = 0u; i < state->capacity; ++i)
        if (state->slots[i].process == process || state->slots[i].payload == payload)
            return SALTS_EBUSY;
    slot->process = process;
    slot->payload = payload;
    slot->cleanup = cleanup;
    return SALTS_OK;
}
int praktor_process_retainer_run_ready(praktor_process_retainer *retainer,
    size_t max_steps, size_t *reaped, size_t *pending) {
    retainer_state *state = state_of(retainer);
    int result = SALTS_OK;
    if (reaped) *reaped = 0u;
    if (pending) *pending = state ? state->pending : 0u;
    if (!state || !max_steps || !reaped || !pending) return SALTS_EINVAL;
    if (state->driving) return SALTS_EBUSY;
    state->driving = true;
    /* Bound both scan work and native turns, including reserved-only slots. */
    for (size_t step = 0u; step < max_steps && state->pending; ++step) {
        retained_process *slot = &state->slots[state->cursor];
        size_t progressed = 0u;
        state->cursor = (state->cursor + 1u) % state->capacity;
        if (!slot->process) continue;
        result = process_async_close(slot->process);
        if (result != SALTS_OK && result != SALTS_EBUSY) break;
        result = process_async_run_ready(slot->process, 1u, &progressed);
        if (result != SALTS_OK && result != SALTS_EBUSY) break;
        result = SALTS_OK;
        if (!process_async_is_quiescent(slot->process)) continue;
        result = process_async_destroy(slot->process);
        if (result == SALTS_EBUSY) { result = SALTS_OK; continue; }
        if (result != SALTS_OK) break;
        slot->cleanup(slot->payload);
        *slot = (retained_process){0};
        --state->pending;
        ++*reaped;
    }
    state->driving = false;
    *pending = state->pending;
    return result != SALTS_OK ? result : state->pending ? SALTS_EBUSY : SALTS_OK;
}
int praktor_process_retainer_destroy(praktor_process_retainer *retainer) {
    retainer_state *state = state_of(retainer);
    if (!state) return SALTS_EINVAL;
    if (state->driving || state->pending) return SALTS_EBUSY;
    free(state);
    retainer->impl = NULL;
    return SALTS_OK;
}
