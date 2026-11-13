#ifndef PRAKTOR_PROCESS_RETAINER_H
#define PRAKTOR_PROCESS_RETAINER_H
#include <process_async.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Address-stable, zero-initialized, exclusive owner. No concurrent calls.
 * Keep alive until every reservation is released or adopted and reaped.
 * Destroy preserves the serial counter so later reservations cannot reuse it. */
typedef struct praktor_process_retainer {
    void *impl;
    uint64_t serial;
} praktor_process_retainer;
typedef struct praktor_process_retainer_token {
    const praktor_process_retainer *owner;
    size_t slot;
    uint64_t serial;
} praktor_process_retainer_token;

/* Init allocates capacity slots; invalid capacity returns EINVAL, allocation
 * failure ENOMEM, and reinit of a live owner EBUSY. No later operation allocates. */
int praktor_process_retainer_init(praktor_process_retainer *retainer, size_t capacity);
/* Reserve before spawn. Full returns ENOBUFS; exhausted serials return ERANGE.
 * Rejection clears out. Release accepts only a current, unadopted token. */
int praktor_process_retainer_reserve(praktor_process_retainer *retainer,
    praktor_process_retainer_token *out);
int praktor_process_retainer_release(praktor_process_retainer *retainer,
    praktor_process_retainer_token token);
/* Transfer a closing native handle and its containing payload. The handle's
 * address stays fixed. On success caller relinquishes both; on failure it
 * retains both. cleanup runs once, after native quiescence and destroy. */
int praktor_process_retainer_adopt(praktor_process_retainer *retainer,
    praktor_process_retainer_token token, process_async *process,
    void *payload, void (*cleanup)(void *));
/* Drive at most max_steps native turns in round-robin order. Pending includes
 * unadopted reservations. EBUSY means live owners remain; native failures
 * propagate with ownership retained. Outputs are initialized on rejection. */
int praktor_process_retainer_run_ready(praktor_process_retainer *retainer,
    size_t max_steps, size_t *reaped, size_t *pending);
/* EBUSY retains every live slot; success frees slot storage and clears impl. */
int praktor_process_retainer_destroy(praktor_process_retainer *retainer);
#ifdef __cplusplus
}
#endif
#endif
