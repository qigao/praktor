#ifndef PRAKTOR_STATECHART_INTERNAL_H
#define PRAKTOR_STATECHART_INTERNAL_H

#include "actions/native_host.h"
#include "actions/orch_session_plan.h"
#include "praktor.h"
#include "yml/yaml_document.h"

enum {
    CHART_MAX_STATES = 128,
    CHART_MAX_TRANSITIONS = 512,
    CHART_MAX_DEPTH = 16,
    CHART_MAX_NAME = 128,
    CHART_MAX_STEPS = 256,
    CHART_MAX_QUEUE = 64,
    CHART_MAX_EVENTS = 128,
    CHART_MAX_EVENT_FIELDS = 32,
    CHART_MAX_EVENT_BYTES = 8192
};

typedef enum chart_state_kind {
    CHART_ATOMIC, CHART_COMPOUND, CHART_PARALLEL, CHART_FINAL,
    CHART_SHALLOW_HISTORY, CHART_DEEP_HISTORY
} chart_state_kind;

typedef enum chart_trigger_kind {
    CHART_ALWAYS, CHART_TIMER, CHART_DONE, CHART_NAMED
} chart_trigger_kind;

typedef enum chart_field_kind { CHART_TEXT, CHART_INT64, CHART_BOOL } chart_field_kind;
typedef struct chart_event_field {
    /* The full event.data.* value key fits the native host's key limit. */
    char name[CHART_MAX_NAME - sizeof("event.data.") + 2];
    chart_field_kind kind;
} chart_event_field;
typedef struct chart_event_type {
    char name[CHART_MAX_NAME + 1];
    chart_event_field *fields;
    size_t field_count;
    bool lifecycle,external;
} chart_event_type;

typedef struct chart_state {
    char name[CHART_MAX_NAME + 1];
    size_t parent, end, initial;
    size_t first_transition, transition_count;
    chart_state_kind kind;
} chart_state;

typedef struct chart_transition {
    char target_name[CHART_MAX_NAME + 1], done_name[CHART_MAX_NAME + 1];
    size_t source, target, done, domain, event;
    chart_trigger_kind trigger;
    uint64_t delay_ns;
    bool internal;
    praktor_orch_session_plan *guard;
} chart_transition;

typedef struct praktor_statechart_plan {
    size_t state_count, transition_count, guard_nodes, guard_bytes, event_count;
    bool accepts_external;
    /* Index zero is the implicit root and is never a transition target. */
    chart_state states[CHART_MAX_STATES + 1];
    chart_transition transitions[CHART_MAX_TRANSITIONS];
    chart_event_type events[CHART_MAX_EVENTS + 2];
} praktor_statechart_plan;

size_t praktor_statechart_find_event(const praktor_statechart_plan *plan, const char *name);
bool praktor_statechart_field_accepts(chart_field_kind kind, const char *text, size_t size);
bool praktor_statechart_event_reference(const char *value);
int praktor_statechart_validate_raises(const praktor_statechart_plan *task,
    const praktor_statechart_plan *workflow,const praktor_orch_node *node,
    char *error,size_t capacity);

typedef struct praktor_statechart { void *impl; } praktor_statechart;

int praktor_statechart_plan_create(praktor_yaml_node chart,
    const salts_bt_tree_config *limits,bool has_event_source,praktor_statechart_plan **out,
    char *error, size_t capacity);
void praktor_statechart_plan_destroy(praktor_statechart_plan *plan);

/* The owner drives state changes on the same thread as its BT. The chart
 * borrows the plan and the host's read-only guard context until destruction. */
int praktor_statechart_init(praktor_statechart *chart,
    const praktor_statechart_plan *plan, praktor_native_host *host,
    const praktor_session_config *config);
int praktor_statechart_advance(praktor_statechart *chart);
int praktor_statechart_lifecycle(praktor_statechart *chart, const char *event,
    const char *task, const char *status);
/* Sender hooks are borrowed only for resolving references in this call. The
 * receiver owns the typed copy and applies it to its own guard context. */
int praktor_statechart_raise(praktor_statechart *chart,const praktor_bt_host *sender,
    const char *event,const char *bindings);
bool praktor_statechart_polls_external(const praktor_statechart *chart);
int praktor_statechart_receive(praktor_statechart *chart,const praktor_host_event *event);
/* Returns 1 for an active state, 0 for an inactive state, -1 for an unknown ID. */
int praktor_statechart_active(const praktor_statechart *chart, const char *state);
int praktor_statechart_destroy(praktor_statechart *chart);
const char *praktor_statechart_error(const praktor_statechart *chart);

#endif
