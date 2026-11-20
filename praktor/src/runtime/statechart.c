#include "statechart_internal.h"
#include <core/clock.h>
#include <inttypes.h>
#include <json_parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct chart_notification {
    size_t id;
    uint64_t generation;
    json_value_t *payload;
} chart_notification;
typedef struct chart_timer { uint64_t deadline, order; } chart_timer;
typedef struct chart_runtime {
    const praktor_statechart_plan *plan;
    praktor_native_host *host;
    praktor_session_config config;
    bool initialized, finished;
    bool active[CHART_MAX_STATES + 1];
    bool history[CHART_MAX_STATES + 1][CHART_MAX_STATES + 1];
    praktor_session guards[CHART_MAX_TRANSITIONS];
    chart_timer timers[CHART_MAX_TRANSITIONS];
    uint64_t order, generation;
    uint64_t completed[CHART_MAX_STATES + 1];
    chart_notification queue[CHART_MAX_QUEUE];
    size_t head, count;
    char error[256];
} chart_runtime;

typedef struct chart_trigger {
    chart_trigger_kind kind;
    size_t state;
    uint64_t delay_ns;
} chart_trigger;

static int error(chart_runtime *run, int code, const char *message) {
    if (!run->error[0]) (void)snprintf(run->error, sizeof run->error, "statechart: %s", message);
    return code;
}

static uint64_t now(const chart_runtime *run) {
    return run->config.now_ns ? run->config.now_ns(run->config.clock_user) : hrtime();
}

static int enqueue_named(chart_runtime *run, size_t id, json_value_t *payload) {
    if (run->count == CHART_MAX_QUEUE) {
        json_free(payload);
        return error(run, SALTS_ENOBUFS, "internal event queue is full");
    }
    run->queue[(run->head + run->count++) % CHART_MAX_QUEUE] =
        (chart_notification){.id = id, .payload = payload};
    return SALTS_OK;
}

static bool add_text(json_value_t *payload, const char *key, const char *value, size_t length) {
    json_value_t *item = json_create_string_n(value, length);
    if (item && json_object_add_checked(payload, key, item)) return true;
    json_free(item);
    return false;
}

static int add_event_field(json_value_t *payload,const chart_event_field *field,
                           const char *value,size_t length,size_t *used) {
    if(length>CHART_MAX_EVENT_BYTES-*used||
       !praktor_statechart_field_accepts(field->kind,value,length))return SALTS_EINVAL;
    *used+=length;
    char integer[24];
    if(field->kind==CHART_INT64) {
        (void)snprintf(integer,sizeof integer,"%" PRIdMAX,strtoimax(value,NULL,10));
        value=integer;
        length=strlen(integer);
    }
    return add_text(payload,field->name,value,length)?SALTS_OK:SALTS_ENOMEM;
}

static int dispatch_named(praktor_statechart *chart,size_t id,json_value_t *payload) {
    chart_runtime *run=chart->impl;
    if(run->finished) {
        json_free(payload);
        return SALTS_OK;
    }
    const int rc=enqueue_named(run,id,payload);
    return rc==SALTS_OK?praktor_statechart_advance(chart):rc;
}

int praktor_statechart_raise(praktor_statechart *chart,const praktor_bt_host *sender,
                            const char *name,const char *arguments) {
    chart_runtime *run = chart->impl;
    if (!run) return SALTS_EINVAL;
    const size_t id = name ? praktor_statechart_find_event(run->plan, name) : SIZE_MAX;
    if (id == SIZE_MAX || run->plan->events[id].lifecycle || run->plan->events[id].external)
        return error(run, SALTS_EINVAL, "raise requires a declared internal event");
    const chart_event_type *event = &run->plan->events[id];
    json_value_t *bindings = arguments ? json_parse(arguments, strlen(arguments)) : NULL;
    json_value_t *payload = json_create_object();
    int rc = !payload || (arguments && !bindings) ? SALTS_ENOMEM : SALTS_OK;
    size_t used = 0u;
    for (size_t i = 0; rc == SALTS_OK && i < event->field_count; ++i) {
        const chart_event_field *field = &event->fields[i];
        const json_value_t *input = json_object_get(bindings, field->name);
        const char *value = input ? json_string(input) : NULL;
        if (!value) { rc = SALTS_EINVAL; break; }
        size_t length = json_string_len(input);
        char resolved[CHART_MAX_EVENT_BYTES + 1];
        if (praktor_statechart_event_reference(value)) {
            const int read = sender->value_get(sender->value_user, value + 1u,
                                              resolved, sizeof resolved, &length);
            if (read != PRAKTOR_BT_VALUE_OK) { rc = SALTS_EINVAL; break; }
            value = resolved;
        }
        rc=add_event_field(payload,field,value,length,&used);
    }
    json_free(bindings);
    if (rc != SALTS_OK) {
        json_free(payload);
        return error(run, rc, "cannot construct typed event payload");
    }
    return dispatch_named(chart,id,payload);
}

bool praktor_statechart_polls_external(const praktor_statechart *chart) {
    const chart_runtime *run=chart->impl;
    return run&&!run->finished&&run->plan->accepts_external;
}

int praktor_statechart_receive(praktor_statechart *chart,const praktor_host_event *notification) {
    chart_runtime *run=chart->impl;
    if(!run||!notification)return SALTS_EINVAL;
    const size_t id=notification->name?praktor_statechart_find_event(run->plan,notification->name):SIZE_MAX;
    if(id==SIZE_MAX||!run->plan->events[id].external)
        return error(run,SALTS_EINVAL,"host event must name a declared external event");
    const chart_event_type *event=&run->plan->events[id];
    if(notification->field_count!=event->field_count||(event->field_count&&!notification->fields))
        return error(run,SALTS_EINVAL,"host event fields do not match the declaration");
    json_value_t *payload=json_create_object();
    if(!payload)return error(run,SALTS_ENOMEM,"cannot allocate host event payload");
    size_t used=0u;
    for(size_t i=0u;i<event->field_count;++i) {
        const chart_event_field *field=&event->fields[i];
        const char *value=NULL;
        for(size_t j=0u;j<notification->field_count;++j) {
            const praktor_event_field *supplied=&notification->fields[j];
            if(supplied->name&&strcmp(field->name,supplied->name)==0) {
                value=supplied->value;
                break;
            }
        }
        size_t length=0u;
        if(value)while(length<=CHART_MAX_EVENT_BYTES-used&&value[length])++length;
        const int rc=add_event_field(payload,field,value,length,&used);
        if(rc!=SALTS_OK) {
            json_free(payload);
            return error(run,rc,"invalid typed host event payload");
        }
    }
    return dispatch_named(chart,id,payload);
}

int praktor_statechart_lifecycle(praktor_statechart *chart, const char *name,
                                 const char *task, const char *status) {
    chart_runtime *run = chart->impl;
    if (!run || run->finished) return SALTS_OK;
    const size_t id = praktor_statechart_find_event(run->plan, name);
    if (id == SIZE_MAX || !run->plan->events[id].lifecycle)
        return error(run, SALTS_EINVAL, "unknown lifecycle event");
    bool observed = false;
    for (size_t i = 0; i < run->plan->transition_count; ++i) {
        const chart_transition *row = &run->plan->transitions[i];
        if (row->trigger == CHART_NAMED && row->event == id) { observed = true; break; }
    }
    if (!observed) return praktor_statechart_advance(chart);
    const size_t task_length = strlen(task), status_length = strlen(status);
    if (status_length > CHART_MAX_EVENT_BYTES || task_length > CHART_MAX_EVENT_BYTES - status_length)
        return error(run, SALTS_ENOBUFS, "lifecycle event payload exceeds the chart limit");
    json_value_t *payload = json_create_object();
    if (!payload || !add_text(payload, "task", task, task_length) ||
        !add_text(payload, "status", status, status_length)) {
        json_free(payload);
        return error(run, SALTS_ENOMEM, "cannot construct lifecycle event");
    }
    return dispatch_named(chart,id,payload);
}

static int evaluate_guard(chart_runtime *run, size_t index, bool *enabled) {
    const praktor_orch_session_plan *plan = run->plan->transitions[index].guard;
    if (!plan) { *enabled = true; return SALTS_OK; }
    praktor_session *session = &run->guards[index];
    int rc = session->impl ? praktor_session_reset(session) :
        praktor_orch_session_plan_init_session(plan, &run->config, session);
    if (rc != SALTS_OK) return error(run, rc, "cannot prepare transition guard");
    salts_bt_node_status result = SALTS_BT_RUNNING;
    rc = praktor_session_tick(session, &result);
    if (rc != SALTS_OK) return error(run, rc, praktor_session_error(session));
    if (result == SALTS_BT_RUNNING) return error(run, SALTS_EINVAL, "transition guard returned RUNNING");
    *enabled = result == SALTS_BT_SUCCESS;
    return SALTS_OK;
}

static bool descendants(const praktor_statechart_plan *plan, size_t ancestor, size_t state) {
    return ancestor < state && state < plan->states[ancestor].end;
}

static bool matches(const chart_transition *row, chart_trigger trigger) {
    if (row->trigger != trigger.kind) return false;
    switch (trigger.kind) {
    case CHART_ALWAYS: return true;
    case CHART_DONE: return row->done == trigger.state;
    case CHART_TIMER: return row->source == trigger.state && row->delay_ns == trigger.delay_ns;
    case CHART_NAMED: return row->event == trigger.state;
    }
    return false;
}

static bool conflicts(const praktor_statechart_plan *plan, size_t a, size_t b) {
    /* Each targeted transition exits all active descendants of its domain.
     * Two such sets meet exactly when the domains are nested or equal. */
    const chart_transition *left = &plan->transitions[a], *right = &plan->transitions[b];
    return left->target && right->target && (left->domain == right->domain ||
           descendants(plan, left->domain, right->domain) || descendants(plan, right->domain, left->domain));
}

static int select_transitions(chart_runtime *run, chart_trigger trigger, bool *selected, bool *any) {
    const praktor_statechart_plan *plan = run->plan;
    unsigned char evaluated[CHART_MAX_TRANSITIONS] = {0};
    size_t candidates[CHART_MAX_STATES], count = 0;
    for (size_t leaf = 1; leaf < plan->state_count; ++leaf) {
        if (!run->active[leaf] || plan->states[leaf].end != leaf + 1u) continue;
        size_t match = SIZE_MAX;
        for (size_t i = leaf; i && match == SIZE_MAX; i = plan->states[i].parent) {
            const chart_state *source = &plan->states[i];
            for (size_t j = source->first_transition; j < source->first_transition + source->transition_count; ++j) {
                const chart_transition *row = &plan->transitions[j];
                if (!matches(row, trigger)) continue;
                if (!evaluated[j]) {
                    bool enabled = false;
                    int rc = evaluate_guard(run, j, &enabled);
                    if (rc != SALTS_OK) return rc;
                    evaluated[j] = enabled ? 2u : 1u;
                    if (enabled) candidates[count++] = j;
                }
                if (evaluated[j] == 2u) { match = j; break; }
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        size_t candidate = candidates[i];
        bool wins = true;
        for (size_t j = 0; j < i; ++j) {
            size_t old = candidates[j];
            if (selected[old] && conflicts(plan, old, candidate) &&
                !descendants(plan, plan->transitions[old].source, plan->transitions[candidate].source)) {
                wins = false;
                break;
            }
        }
        if (!wins) continue;
        for (size_t j = 0; j < i; ++j)
            if (selected[candidates[j]] && conflicts(plan, candidates[j], candidate)) selected[candidates[j]] = false;
        selected[candidate] = true;
        *any = true;
    }
    return SALTS_OK;
}

static void enter_path(const praktor_statechart_plan *plan, size_t state, size_t target,
                       const bool *saved, bool *configuration) {
    const chart_state *row = &plan->states[state];
    configuration[state] = true;
    if (row->kind == CHART_COMPOUND) {
        size_t child = row->initial;
        for (size_t i = state + 1u; i < row->end; i = plan->states[i].end) {
            if ((i <= target && target < plan->states[i].end) || (saved && saved[i])) {
                child = i;
                break;
            }
        }
        enter_path(plan, child, target, saved, configuration);
    } else if (row->kind == CHART_PARALLEL) {
        for (size_t child = state + 1u; child < row->end; child = plan->states[child].end)
            enter_path(plan, child, target, saved, configuration);
    }
}

static void complete_states(const chart_runtime *run, const bool *active,
                           const bool *entering, bool *completed, bool *new_completion) {
    const praktor_statechart_plan *plan = run->plan;
    for (size_t i = plan->state_count; i-- > 1u;) {
        const chart_state *row = &plan->states[i];
        if (!active[i]) continue;
        bool reentered_child = false;
        if (row->kind == CHART_COMPOUND || row->kind == CHART_PARALLEL) {
            completed[i] = row->kind == CHART_PARALLEL;
            for (size_t child = i + 1u; child < row->end; child = plan->states[child].end) {
                if (row->kind == CHART_COMPOUND && active[child] && plan->states[child].kind == CHART_FINAL) {
                    completed[i] = true;
                    reentered_child = entering[child];
                }
                if (row->kind == CHART_PARALLEL) {
                    if (!completed[child]) completed[i] = false;
                    reentered_child |= new_completion[child];
                }
            }
        }
        new_completion[i] = completed[i] && (entering[i] || !run->completed[i] || reentered_child);
    }
}

static int enqueue_completion(chart_runtime *run, size_t state, uint64_t generation) {
    if (run->count == CHART_MAX_QUEUE) return error(run, SALTS_ENOBUFS, "internal event queue is full");
    run->queue[(run->head + run->count++) % CHART_MAX_QUEUE] = (chart_notification){state, generation, NULL};
    return SALTS_OK;
}

static int apply_transitions(chart_runtime *run, const bool *selected) {
    const praktor_statechart_plan *plan = run->plan;
    bool next[CHART_MAX_STATES + 1], exiting[CHART_MAX_STATES + 1] = {0};
    bool entering[CHART_MAX_STATES + 1] = {0}, completed[CHART_MAX_STATES + 1] = {0};
    bool new_completion[CHART_MAX_STATES + 1] = {0};
    const size_t bytes = plan->state_count * sizeof(*next);
    memcpy(next, run->active, bytes);
    if (!selected) {
        enter_path(plan, 0u, plan->states[0].initial, NULL, next);
    } else {
        for (size_t i = 0; i < plan->transition_count; ++i) {
            const chart_transition *row = &plan->transitions[i];
            if (!selected[i] || !row->target) continue;
            for (size_t j = row->domain + 1u; j < plan->states[row->domain].end; ++j) {
                exiting[j] |= run->active[j];
                next[j] = false;
            }
        }
        for (size_t i = 0; i < plan->transition_count; ++i) {
            const chart_transition *row = &plan->transitions[i];
            if (!selected[i] || !row->target) continue;
            size_t target = row->target;
            const chart_state *to = &plan->states[target];
            const bool *saved = NULL;
            bool latest[CHART_MAX_STATES + 1] = {0};
            if (to->kind == CHART_SHALLOW_HISTORY || to->kind == CHART_DEEP_HISTORY) {
                const size_t owner = to->parent;
                saved = run->history[owner];
                if (exiting[owner]) {
                    for (size_t j = owner + 1u; j < plan->states[owner].end; ++j)
                        latest[j] = run->active[j];
                    saved = latest;
                }
                for (target = owner + 1u; target < plan->states[owner].end && !saved[target];
                     target = plan->states[target].end) {}
                if (target == plan->states[owner].end) { target = to->initial; saved = NULL; }
                else if (to->kind == CHART_SHALLOW_HISTORY) saved = NULL;
                else target = owner;
            }
            /* Only build paths below the domain, whose other active regions stay intact. */
            enter_path(plan, row->domain, target, saved, next);
        }
    }
    for (size_t i = 1; i < plan->state_count; ++i)
        entering[i] = next[i] && (!run->active[i] || exiting[i]);

    int rc = SALTS_OK;
    complete_states(run, next, entering, completed, new_completion);
    size_t added = 0;
    for (size_t i = 1; i < plan->state_count; ++i)
        if (new_completion[i]) ++added;
    if (added > CHART_MAX_QUEUE - run->count)
        rc = error(run, SALTS_ENOBUFS, "internal event queue is full");
    if (rc == SALTS_OK && added > UINT64_MAX - run->generation)
        rc = error(run, SALTS_ENOBUFS, "completion generation overflows");
    const uint64_t started = now(run);
    for (size_t i = 0; rc == SALTS_OK && i < plan->transition_count; ++i) {
        const chart_transition *row = &plan->transitions[i];
        if (row->trigger == CHART_TIMER && entering[row->source] &&
            (row->delay_ns > UINT64_MAX - started || run->order > UINT64_MAX - CHART_MAX_TRANSITIONS))
            rc = error(run, SALTS_EINVAL, "timer deadline or order overflows");
    }
    if (rc != SALTS_OK) return rc;
    for (size_t i = 1; i < plan->state_count; ++i) {
        if (exiting[i] && plan->states[i].kind == CHART_COMPOUND) {
            const size_t length = plan->states[i].end - (i + 1u);
            memcpy(&run->history[i][i + 1u], &run->active[i + 1u], length * sizeof(*next));
        }
        if (!completed[i]) run->completed[i] = 0u;
        else if (new_completion[i]) {
            const uint64_t generation = ++run->generation;
            run->completed[i] = generation;
            (void)enqueue_completion(run, i, generation);
        }
        if (entering[i] && !plan->states[i].parent && plan->states[i].kind == CHART_FINAL)
            run->finished = true;
    }
    for (size_t i = 0; i < plan->transition_count; ++i) {
        const chart_transition *row = &plan->transitions[i];
        if (row->trigger != CHART_TIMER) continue;
        if (entering[row->source]) run->timers[i] = (chart_timer){started + row->delay_ns, ++run->order};
        else if (exiting[row->source]) run->timers[i] = (chart_timer){0};
    }
    memcpy(run->active, next, bytes);
    return SALTS_OK;
}

static int fire(chart_runtime *run, chart_trigger trigger, unsigned *steps, bool *matched) {
    bool selected[CHART_MAX_TRANSITIONS] = {0};
    *matched = false;
    int rc = select_transitions(run, trigger, selected, matched);
    if (rc != SALTS_OK || !*matched) return rc;
    if (*steps == CHART_MAX_STEPS) return error(run, SALTS_ENOBUFS, "stabilization step limit exceeded");
    ++*steps;
    return apply_transitions(run, selected);
}

static int fire_named(chart_runtime *run, chart_notification event, unsigned *steps) {
    const chart_event_type *type = &run->plan->events[event.id];
    praktor_value_store values = {0};
    int rc = praktor_value_store_init(&values, type->field_count + 1u, CHART_MAX_NAME, CHART_MAX_EVENT_BYTES);
    if (rc == PRAKTOR_VALUE_OK) rc = praktor_value_store_set(&values, "event.name", type->name);
    for (size_t i = 0; rc == PRAKTOR_VALUE_OK && i < type->field_count; ++i) {
        const char *field = type->fields[i].name;
        char key[CHART_MAX_NAME + 1];
        (void)snprintf(key, sizeof key, "event.data.%s", field);
        rc = praktor_value_store_set(&values, key, json_get_string(event.payload, field));
    }
    json_free(event.payload);
    if (rc == PRAKTOR_VALUE_OK) {
        bool matched;
        praktor_native_host_event_values(run->host, &values);
        rc = fire(run, (chart_trigger){.kind = CHART_NAMED, .state = event.id}, steps, &matched);
        praktor_native_host_event_values(run->host, NULL);
    } else {
        rc = error(run, rc == PRAKTOR_VALUE_ENOMEM ? SALTS_ENOMEM : SALTS_EINVAL, "cannot expose event fields");
    }
    praktor_value_store_destroy(&values);
    return rc;
}

static int stabilize(chart_runtime *run, unsigned *steps) {
    while (!run->finished) {
        bool matched;
        int rc = fire(run, (chart_trigger){.kind = CHART_ALWAYS}, steps, &matched);
        if (rc != SALTS_OK) return rc;
        if (matched) continue;
        if (!run->count) break;
        const chart_notification event = run->queue[run->head];
        run->head = (run->head + 1u) % CHART_MAX_QUEUE;
        --run->count;
        if (event.payload) rc = fire_named(run, event, steps);
        else if (run->completed[event.id] == event.generation)
            rc = fire(run, (chart_trigger){.kind = CHART_DONE, .state = event.id}, steps, &matched);
        if (rc != SALTS_OK) return rc;
    }
    return SALTS_OK;
}

static int dispatch_timer(chart_runtime *run, unsigned *steps, bool *dispatched) {
    size_t best = SIZE_MAX;
    const uint64_t time = now(run);
    for (size_t i = 0; i < run->plan->transition_count; ++i) {
        const chart_timer timer = run->timers[i];
        if (timer.deadline && timer.deadline <= time &&
            (best == SIZE_MAX || timer.deadline < run->timers[best].deadline ||
             (timer.deadline == run->timers[best].deadline && timer.order < run->timers[best].order)))
            best = i;
    }
    *dispatched = best != SIZE_MAX;
    if (!*dispatched) return SALTS_OK;
    const chart_transition *row = &run->plan->transitions[best];
    chart_trigger trigger = {CHART_TIMER, row->source, row->delay_ns};
    for (size_t i = 0; i < run->plan->transition_count; ++i)
        if (matches(&run->plan->transitions[i], trigger)) run->timers[i] = (chart_timer){0};
    bool matched;
    return fire(run, trigger, steps, &matched);
}

static int refuse_write(void *user, const char *key, const char *value) {
    (void)user;
    (void)key;
    (void)value;
    return PRAKTOR_BT_VALUE_ERROR;
}

int praktor_statechart_init(praktor_statechart *chart, const praktor_statechart_plan *plan,
                            praktor_native_host *host, const praktor_session_config *config) {
    chart_runtime *run = calloc(1u, sizeof(*run));
    if (!run) return SALTS_ENOMEM;
    run->plan = plan;
    run->host = host;
    run->config = *config;
    run->config.retainer = NULL;
    /* Guard plans contain a single predicate. Give them only value reads and
     * capability lookup, with no host action dispatch. */
    const praktor_bt_host *hooks = &config->host;
    run->config.host = (praktor_bt_host){
        .value_get = hooks->value_get, .value_set = refuse_write,
        .value_contains = hooks->value_contains, .value_user = hooks->value_user
    };
    chart->impl = run;
    return SALTS_OK;
}

int praktor_statechart_advance(praktor_statechart *chart) {
    chart_runtime *run = chart->impl;
    if (!run || run->finished) return SALTS_OK;
    unsigned steps = 0u;
    if (!run->initialized) {
        int rc = apply_transitions(run, NULL);
        if (rc != SALTS_OK) return rc;
        run->initialized = true;
    }
    for (;;) {
        int rc = stabilize(run, &steps);
        if (rc != SALTS_OK || run->finished) return rc;
        bool dispatched;
        rc = dispatch_timer(run, &steps, &dispatched);
        if (rc != SALTS_OK || !dispatched) return rc;
    }
}

int praktor_statechart_active(const praktor_statechart *chart, const char *state) {
    const chart_runtime *run = chart->impl;
    if (run)
        for (size_t i = 1; i < run->plan->state_count; ++i)
            if (!strcmp(run->plan->states[i].name, state)) return run->active[i];
    return -1;
}

int praktor_statechart_destroy(praktor_statechart *chart) {
    chart_runtime *run = chart->impl;
    if (!run) return SALTS_OK;
    for (size_t i = 0; i < run->plan->transition_count; ++i) {
        if (!run->guards[i].impl) continue;
        int rc = praktor_session_destroy(&run->guards[i]);
        if (rc != SALTS_OK) return rc;
    }
    for (size_t i = 0; i < run->count; ++i) json_free(run->queue[(run->head + i) % CHART_MAX_QUEUE].payload);
    free(run);
    chart->impl = NULL;
    return SALTS_OK;
}

const char *praktor_statechart_error(const praktor_statechart *chart) {
    const chart_runtime *run = chart->impl;
    return run ? run->error : "";
}
