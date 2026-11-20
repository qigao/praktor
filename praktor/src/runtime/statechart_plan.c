#include "statechart_internal.h"
#include "actions/variable_reference.h"
#include "yml/orch_yaml.h"
#include <errno.h>
#include <inttypes.h>
#include <json_parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct chart_parser {
    praktor_statechart_plan *plan;
    const salts_bt_tree_config *limits;
    char *error;
    size_t capacity;
    bool has_event_source;
} chart_parser;

static int fail(chart_parser *parser, praktor_yaml_node node, int status,
                const char *message) {
    if (parser->error && parser->capacity) {
        tstr location = NULL;
        int rc = praktor_yaml_location(node, &location);
        (void)snprintf(parser->error, parser->capacity, "statechart%s: %s",
                       rc == SALTS_OK && location ? location : "", message);
        tstr_free(location);
    }
    return status;
}

/* Collect decoded exact keys once, preserving the value's source location. */
static int read_map(chart_parser *parser, praktor_yaml_node map,
                    const char *const *keys, praktor_yaml_node *values, size_t count) {
    if (!praktor_yaml_is_map(map))
        return fail(parser, map, SALTS_EINVAL, "expected a mapping");
    for (size_t i = 0; i < praktor_yaml_count(map); ++i) {
        praktor_yaml_node value = praktor_yaml_at(map, i);
        tstr name = NULL;
        int rc = praktor_yaml_text(value, true, &name);
        if (rc != SALTS_OK) return rc;
        size_t j = 0;
        while (j < count && strcmp(keys[j], name)) ++j;
        if (j == count || praktor_yaml_valid(values[j])) {
            char message[192];
            (void)snprintf(message, sizeof message, "%s key '%s'",
                           j == count ? "unknown" : "duplicate", name);
            tstr_free(name);
            return fail(parser, value, SALTS_EINVAL, message);
        }
        tstr_free(name);
        values[j] = value;
    }
    return SALTS_OK;
}

static bool identifier(const char *name) {
    const size_t length = strlen(name);
    static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";
    static const char characters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_0123456789-";
    return length && length <= CHART_MAX_NAME && strchr(letters, name[0]) &&
           strspn(name, characters) == length;
}

static bool event_name(const char *name) {
    if (strlen(name) > CHART_MAX_NAME) return false;
    char segment[CHART_MAX_NAME + 1];
    do {
        size_t length = strcspn(name, ".");
        memcpy(segment, name, length);
        segment[length] = '\0';
        if (!identifier(segment)) return false;
        name += length;
    } while (*name++ == '.');
    return true;
}

size_t praktor_statechart_find_event(const praktor_statechart_plan *plan, const char *name) {
    for (size_t i = 0; i < plan->event_count; ++i)
        if (!strcmp(name, plan->events[i].name)) return i;
    return SIZE_MAX;
}

bool praktor_statechart_field_accepts(chart_field_kind kind, const char *text, size_t size) {
    if (!text || memchr(text, '\0', size)) return false;
    if (kind == CHART_TEXT) {
        /* JSON's string constructor preserves bytes; check UTF-8 at event admission. */
        for (size_t i = 0; i < size;) {
            const unsigned char b = (unsigned char)text[i++];
            if (b < 0x80u) continue;
            size_t tail = b >= 0xC2u && b <= 0xDFu ? 1u : b <= 0xEFu && b >= 0xE0u ? 2u :
                          b >= 0xF0u && b <= 0xF4u ? 3u : 0u;
            if (!tail || tail > size - i) return false;
            const unsigned char next = (unsigned char)text[i];
            if ((b == 0xE0u && next < 0xA0u) || (b == 0xEDu && next >= 0xA0u) ||
                (b == 0xF0u && next < 0x90u) || (b == 0xF4u && next >= 0x90u)) return false;
            while (tail--) if (((unsigned char)text[i++] & 0xC0u) != 0x80u) return false;
        }
        return true;
    }
    if (kind == CHART_BOOL) return !strcmp(text, "true") || !strcmp(text, "false");
    size_t first = size && text[0] == '-' ? 1u : 0u;
    if (first == size || strspn(text + first, "0123456789") != size - first) return false;
    errno = 0;
    const intmax_t integer = strtoimax(text, NULL, 10);
    return errno != ERANGE && integer >= INT64_MIN && integer <= INT64_MAX;
}

static int read_events(chart_parser *parser, praktor_yaml_node map) {
    if (!praktor_yaml_valid(map)) return SALTS_OK;
    if (!praktor_yaml_is_map(map) || praktor_yaml_count(map) > CHART_MAX_EVENTS)
        return fail(parser, map, SALTS_EINVAL, "events must be a bounded mapping");
    praktor_statechart_plan *plan = parser->plan;
    for (size_t i = 0; i < praktor_yaml_count(map); ++i) {
        const praktor_yaml_node node = praktor_yaml_at(map, i);
        static const char *const keys[] = {"source", "fields"};
        praktor_yaml_node values[2] = {{0}};
        int rc = read_map(parser, node, keys, values, 2u);
        if (rc != SALTS_OK) return rc;
        tstr name = NULL, source = NULL;
        rc = praktor_yaml_text(node, true, &name);
        if (rc == SALTS_OK) rc = praktor_yaml_text(values[0], false, &source);
        if (rc == SALTS_OK && (!event_name(name) || praktor_statechart_find_event(plan, name) != SIZE_MAX))
            rc = fail(parser, node, SALTS_EINVAL, "invalid or duplicate event name");
        const bool external=rc==SALTS_OK&&!strcmp(source,"external");
        if (external&&!parser->has_event_source)
            rc = fail(parser, values[0], SALTS_ENOTSUP, "external event source has no host binding");
        if (rc == SALTS_OK && !external && strcmp(source, "internal"))
            rc = fail(parser, values[0], SALTS_EINVAL, "event source must be internal or external");
        chart_event_type *event = &plan->events[plan->event_count];
        if (rc == SALTS_OK) memcpy(event->name, name, strlen(name) + 1u);
        tstr_free(name);
        tstr_free(source);
        if (rc != SALTS_OK) return rc;
        event->external=external;
        plan->accepts_external|=external;
        ++plan->event_count;
        if (!praktor_yaml_valid(values[1])) continue;
        event->field_count = praktor_yaml_count(values[1]);
        if (!praktor_yaml_is_map(values[1]) || event->field_count > CHART_MAX_EVENT_FIELDS)
            return fail(parser, values[1], SALTS_EINVAL, "event fields must be a bounded mapping");
        if (!event->field_count) continue;
        event->fields = calloc(event->field_count, sizeof(*event->fields));
        if (!event->fields) return SALTS_ENOMEM;
        for (size_t j = 0; j < event->field_count; ++j) {
            const praktor_yaml_node item = praktor_yaml_at(values[1], j);
            chart_event_field *field = &event->fields[j];
            tstr key = NULL, type = NULL;
            rc = praktor_yaml_text(item, true, &key);
            if (rc == SALTS_OK) rc = praktor_yaml_text(item, false, &type);
            if (rc == SALTS_OK && (!identifier(key) || strchr(key, '-') || strlen(key) >= sizeof field->name))
                rc = SALTS_EINVAL;
            for (size_t k = 0; rc == SALTS_OK && k < j; ++k)
                if (!strcmp(key, event->fields[k].name)) rc = SALTS_EINVAL;
            if (rc == SALTS_OK) {
                if (!strcmp(type, "int64")) field->kind = CHART_INT64;
                else if (!strcmp(type, "bool")) field->kind = CHART_BOOL;
                else if (strcmp(type, "text")) rc = SALTS_EINVAL;
                if (rc == SALTS_OK) memcpy(field->name, key, strlen(key) + 1u);
            }
            tstr_free(key);
            tstr_free(type);
            if (rc != SALTS_OK) return fail(parser, item, rc, "invalid event field name or type");
        }
    }
    return SALTS_OK;
}

static int add_lifecycle_event(praktor_statechart_plan *plan, const char *name) {
    chart_event_type *event = &plan->events[plan->event_count++];
    memcpy(event->name, name, strlen(name) + 1u);
    event->lifecycle = true;
    event->fields = calloc(2u, sizeof(*event->fields));
    if (!event->fields) return SALTS_ENOMEM;
    event->field_count = 2u;
    event->fields[0] = (chart_event_field){.name = "task", .kind = CHART_TEXT};
    event->fields[1] = (chart_event_field){.name = "status", .kind = CHART_TEXT};
    return SALTS_OK;
}

static int read_identifier(chart_parser *parser, praktor_yaml_node node, bool key, char *out) {
    tstr name = NULL;
    int rc = praktor_yaml_text(node, key, &name);
    if (rc == SALTS_OK && identifier(name)) {
        memcpy(out, name, strlen(name) + 1u);
    } else if (rc != SALTS_ENOMEM) {
        rc = fail(parser, node, SALTS_EINVAL, "expected a state identifier");
    }
    tstr_free(name);
    return rc;
}

static size_t find_state(const praktor_statechart_plan *plan, const char *name) {
    for (size_t i = 1; i < plan->state_count; ++i)
        if (!strcmp(name, plan->states[i].name)) return i;
    return 0u;
}

static bool history(chart_state_kind kind) {
    return kind == CHART_SHALLOW_HISTORY || kind == CHART_DEEP_HISTORY;
}

bool praktor_statechart_event_reference(const char *value) {
    return value && praktor_variable_reference(vstr_from_cstr(value)).data;
}

static int validate_raise(const praktor_statechart_plan *task,const praktor_statechart_plan *workflow,
    const praktor_orch_node *node,char *error,size_t capacity) {
    const praktor_statechart_plan *plan=task;
    const char *missing="raise requires a statechart on the same task";
    const char *name = NULL;
    vstr payload = {0};
    for (size_t i = 0; i < praktor_orch_param_count(node); ++i) {
        praktor_orch_param param;
        int rc = praktor_orch_param_at(node, i, &param);
        if (rc != SALTS_OK) return rc;
        if (!strcmp(param.key.data, "event")) name = param.value.data;
        else if (!strcmp(param.key.data, "with")) payload = param.value;
        else if (!strcmp(param.key.data, "scope")) {
            if (param.value.len==8u&&!memcmp(param.value.data,"workflow",8u)) {
                plan=workflow;
                missing="raise with scope workflow requires a document statechart";
            } else if (param.value.len!=4u||memcmp(param.value.data,"task",4u)) {
                if(error&&capacity)(void)snprintf(error,capacity,"raise scope must be task or workflow");
                return SALTS_EINVAL;
            }
        }
    }
    const char *invalid="raise requires a declared internal event and matching payload";
    const size_t event = plan&&name ? praktor_statechart_find_event(plan, name) : SIZE_MAX;
    if (event == SIZE_MAX || plan->events[event].lifecycle || plan->events[event].external) {
        if(error&&capacity)(void)snprintf(error,capacity,"%s",plan?invalid:missing);
        return SALTS_EINVAL;
    }
    const chart_event_type *type = &plan->events[event];
    json_value_t *fields = payload.data ? json_parse(payload.data, payload.len) : NULL;
    int rc = payload.data && !fields ? SALTS_ENOMEM : SALTS_OK;
    if (rc == SALTS_OK && json_object_size(fields) != type->field_count) rc = SALTS_EINVAL;
    size_t used = 0;
    for (size_t i = 0; rc == SALTS_OK && i < type->field_count; ++i) {
        const json_value_t *input = json_object_get(fields, type->fields[i].name);
        const char *value = input ? json_string(input) : NULL;
        const size_t length = value ? json_string_len(input) : 0u;
        const bool reference = praktor_statechart_event_reference(value);
        if (!value || memchr(value, '\0', length) ||
            praktor_retired_variable_reference(vstr_from_buf(value,length)) ||
            (!reference && !praktor_statechart_field_accepts(type->fields[i].kind, value, length)))
            rc = SALTS_EINVAL;
        if (!reference && length > CHART_MAX_EVENT_BYTES - used) rc = SALTS_ENOBUFS;
        if (rc == SALTS_OK && !reference) used += length;
    }
    json_free(fields);
    if(rc!=SALTS_OK&&error&&capacity)(void)snprintf(error,capacity,"%s",invalid);
    return rc;
}

int praktor_statechart_validate_raises(const praktor_statechart_plan *task,
    const praktor_statechart_plan *workflow,const praktor_orch_node *node,
    char *error,size_t capacity) {
    if (praktor_orch_node_type(node) == PRAKTOR_ORCH_RAISE)
        return validate_raise(task,workflow,node,error,capacity);
    for (size_t i = 0; i < praktor_orch_child_count(node); ++i) {
        const praktor_orch_node *child = NULL;
        int rc = praktor_orch_child_at(node, i, &child);
        if (rc == SALTS_OK) rc = praktor_statechart_validate_raises(task,workflow,child,error,capacity);
        if (rc != SALTS_OK) return rc;
    }
    return SALTS_OK;
}

static int compile_guard(chart_parser *parser, praktor_yaml_node node,
                         praktor_orch_session_plan **out) {
    if (!praktor_yaml_valid(node)) return SALTS_OK;
    praktor_orch_tree *tree = NULL;
    int rc = praktor_orch_from_guard_yaml(node, &tree, parser->error, parser->capacity);
    if (rc != SALTS_OK) return rc;
    rc = praktor_orch_session_plan_create(tree, parser->limits, out,
                                         parser->error, parser->capacity);
    praktor_orch_tree_destroy(tree);
    if (rc != SALTS_OK) return rc;
    const size_t nodes = praktor_orch_session_plan_node_count(*out);
    const size_t bytes = praktor_orch_session_plan_retained_bytes(*out);
    if (nodes > PRAKTOR_ORCH_MAX_NODES - parser->plan->guard_nodes ||
        bytes > PRAKTOR_ORCH_MAX_BYTES - parser->plan->guard_bytes)
        return fail(parser, node, SALTS_ENOBUFS, "aggregate guard plan limit exceeded");
    parser->plan->guard_nodes += nodes;
    parser->plan->guard_bytes += bytes;
    return SALTS_OK;
}

static int validate_event_fields(chart_parser *parser, praktor_yaml_node guard,
                                 const chart_transition *row) {
    if (!praktor_yaml_valid(guard)) return SALTS_OK;
    praktor_yaml_node fields = {0};
    int rc = praktor_yaml_get(guard, "fields", &fields, NULL);
    if (rc != SALTS_OK) return rc;
    for (size_t i = 0; i < praktor_yaml_count(fields); ++i) {
        praktor_yaml_node node = praktor_yaml_at(fields, i);
        tstr name = NULL;
        rc = praktor_yaml_scalar(node, &name, NULL);
        if (rc != SALTS_OK) return rc;
        bool valid = strncmp(name, "event.", 6u) != 0;
        if (!valid && row->trigger == CHART_NAMED) {
            const chart_event_type *event = &parser->plan->events[row->event];
            valid = !strcmp(name, "event.name");
            for (size_t j = 0; !valid && j < event->field_count; ++j) {
                valid = !strncmp(name, "event.data.", 11u) &&
                        !strcmp(name + 11u, event->fields[j].name);
            }
        }
        tstr_free(name);
        if (!valid) return fail(parser, node, SALTS_EINVAL, "guard references an unavailable event field");
    }
    return SALTS_OK;
}

static int read_transition(chart_parser *parser, praktor_yaml_node node, size_t source) {
    enum { ON, AFTER, DONE, GUARD, TARGET, TYPE, FIELD_COUNT };
    static const char *const keys[] = {"on", "after_ms", "on_done", "guard", "target", "type"};
    praktor_yaml_node fields[FIELD_COUNT] = {{0}};
    int rc = read_map(parser, node, keys, fields, FIELD_COUNT);
    if (rc != SALTS_OK) return rc;
    if (parser->plan->transition_count == CHART_MAX_TRANSITIONS)
        return fail(parser, node, SALTS_ENOBUFS, "transition limit exceeded");
    chart_transition *row = &parser->plan->transitions[parser->plan->transition_count++];
    row->source = source;
    unsigned triggers = (unsigned)praktor_yaml_valid(fields[ON]) +
        (unsigned)praktor_yaml_valid(fields[AFTER]) + (unsigned)praktor_yaml_valid(fields[DONE]);
    if (triggers > 1u) return fail(parser, node, SALTS_EINVAL, "multiple transition triggers");
    if (praktor_yaml_valid(fields[ON])) {
        tstr name = NULL;
        rc = praktor_yaml_text(fields[ON], false, &name);
        if (rc != SALTS_OK) return rc;
        row->event = praktor_statechart_find_event(parser->plan, name);
        tstr_free(name);
        if (row->event == SIZE_MAX) return fail(parser, fields[ON], SALTS_EINVAL, "undeclared transition event");
        row->trigger = CHART_NAMED;
    } else if (praktor_yaml_valid(fields[AFTER])) {
        int delay = 0;
        rc = praktor_yaml_integer(fields[AFTER], &delay, NULL);
        if (rc != SALTS_OK || delay <= 0)
            return fail(parser, fields[AFTER], rc == SALTS_ENOMEM ? rc : SALTS_EINVAL,
                        "after_ms must be a positive integer");
        row->trigger = CHART_TIMER;
        row->delay_ns = (uint64_t)delay * UINT64_C(1000000);
    } else if (praktor_yaml_valid(fields[DONE])) {
        row->trigger = CHART_DONE;
        rc = read_identifier(parser, fields[DONE], false, row->done_name);
        if (rc != SALTS_OK) return rc;
    }
    if (praktor_yaml_valid(fields[TARGET])) {
        rc = read_identifier(parser, fields[TARGET], false, row->target_name);
        if (rc != SALTS_OK) return rc;
    } else return fail(parser, node, SALTS_EINVAL, "transition requires a target");
    if (praktor_yaml_valid(fields[TYPE])) {
        tstr type = NULL;
        rc = praktor_yaml_text(fields[TYPE], false, &type);
        if (rc == SALTS_OK) {
            row->internal = !strcmp(type, "internal");
            if (!row->internal && strcmp(type, "external")) rc = SALTS_EINVAL;
        }
        tstr_free(type);
        if (rc != SALTS_OK) return fail(parser, fields[TYPE], rc, "expected internal or external transition type");
    }
    rc = compile_guard(parser, fields[GUARD], &row->guard);
    if (rc == SALTS_OK) rc = validate_event_fields(parser, fields[GUARD], row);
    return rc;
}

static int read_states(chart_parser *parser, praktor_yaml_node map, size_t parent, size_t depth);

static int direct_child(chart_parser *parser, praktor_yaml_node node, size_t parent, size_t *out) {
    char name[CHART_MAX_NAME + 1];
    int rc = read_identifier(parser, node, false, name);
    if (rc != SALTS_OK) return rc;
    const size_t i = find_state(parser->plan, name);
    if (!i || parser->plan->states[i].parent != parent || history(parser->plan->states[i].kind))
        return fail(parser, node, SALTS_EINVAL, "initial/default must name a direct real child");
    *out = i;
    return SALTS_OK;
}

static int read_state(chart_parser *parser, praktor_yaml_node node, size_t parent, size_t depth) {
    enum { TYPE, STATES, INITIAL, TRANSITIONS, MODE, DEFAULT, FIELD_COUNT };
    static const char *const keys[] = {
        "type", "states", "initial", "transitions", "mode", "default"
    };
    praktor_yaml_node fields[FIELD_COUNT] = {{0}};
    int rc = read_map(parser, node, keys, fields, FIELD_COUNT);
    if (rc != SALTS_OK) return rc;
    praktor_statechart_plan *plan = parser->plan;
    if (plan->state_count > CHART_MAX_STATES)
        return fail(parser, node, SALTS_ENOBUFS, "state limit exceeded");
    const size_t index = plan->state_count;
    chart_state *state = &plan->states[index];
    rc = read_identifier(parser, node, true, state->name);
    if (rc != SALTS_OK) return rc;
    if (find_state(plan, state->name)) return fail(parser, node, SALTS_EINVAL, "duplicate state ID");
    ++plan->state_count;
    state->parent = parent;
    const bool has_states = praktor_yaml_valid(fields[STATES]);
    state->kind = has_states ? CHART_COMPOUND : CHART_ATOMIC;
    if (praktor_yaml_valid(fields[TYPE])) {
        tstr name = NULL;
        rc = praktor_yaml_text(fields[TYPE], false, &name);
        if (rc == SALTS_OK) {
            if (!strcmp(name, "parallel")) state->kind = CHART_PARALLEL;
            else if (!strcmp(name, "final")) state->kind = CHART_FINAL;
            else if (!strcmp(name, "history")) state->kind = CHART_SHALLOW_HISTORY;
            else rc = SALTS_EINVAL;
        }
        tstr_free(name);
        if (rc != SALTS_OK) return fail(parser, fields[TYPE], rc, "expected parallel, final or history state type");
    }
    const bool compound = state->kind == CHART_COMPOUND;
    const bool parallel = state->kind == CHART_PARALLEL;
    const bool final = state->kind == CHART_FINAL;
    const bool pseudo = history(state->kind);
    if (has_states != (compound || parallel) || praktor_yaml_valid(fields[INITIAL]) != compound ||
        praktor_yaml_valid(fields[MODE]) != pseudo || praktor_yaml_valid(fields[DEFAULT]) != pseudo ||
        ((final || pseudo) && praktor_yaml_valid(fields[TRANSITIONS])))
        return fail(parser, node, SALTS_EINVAL, "invalid fields for state type");
    if (plan->states[parent].kind == CHART_PARALLEL && !compound && !parallel)
        return fail(parser, node, SALTS_EINVAL, "parallel region must be compound or parallel");
    if (pseudo) {
        if (!parent || plan->states[parent].kind != CHART_COMPOUND)
            return fail(parser, node, SALTS_EINVAL, "history requires a compound parent");
        tstr mode = NULL;
        rc = praktor_yaml_text(fields[MODE], false, &mode);
        if (rc == SALTS_OK && !strcmp(mode, "deep")) state->kind = CHART_DEEP_HISTORY;
        else if (rc == SALTS_OK && strcmp(mode, "shallow")) rc = SALTS_EINVAL;
        tstr_free(mode);
        if (rc != SALTS_OK) return fail(parser, fields[MODE], rc, "expected shallow or deep history mode");
        /* Default may be declared after this pseudostate. Resolve in its parent's pass. */
    }
    state->first_transition = plan->transition_count;
    if (praktor_yaml_valid(fields[TRANSITIONS])) {
        if (!praktor_yaml_is_seq(fields[TRANSITIONS]))
            return fail(parser, fields[TRANSITIONS], SALTS_EINVAL, "transitions must be a list");
        state->transition_count = praktor_yaml_count(fields[TRANSITIONS]);
        for (size_t i = 0; i < state->transition_count; ++i) {
            rc = read_transition(parser, praktor_yaml_at(fields[TRANSITIONS], i), index);
            if (rc != SALTS_OK) return rc;
        }
    }
    if (has_states) {
        rc = read_states(parser, fields[STATES], index, depth + 1u);
        if (rc != SALTS_OK) return rc;
        if (compound) {
            rc = direct_child(parser, fields[INITIAL], index, &state->initial);
            if (rc != SALTS_OK) return rc;
        }
    }
    state->end = plan->state_count;
    return SALTS_OK;
}

static int read_states(chart_parser *parser, praktor_yaml_node map, size_t parent, size_t depth) {
    if (depth > CHART_MAX_DEPTH || !praktor_yaml_is_map(map) || !praktor_yaml_count(map))
        return fail(parser, map, SALTS_EINVAL, "expected a nonempty states map within the depth limit");
    size_t first = parser->plan->state_count;
    for (size_t i = 0; i < praktor_yaml_count(map); ++i) {
        int rc = read_state(parser, praktor_yaml_at(map, i), parent, depth);
        if (rc != SALTS_OK) return rc;
    }
    for (size_t i = 0; i < praktor_yaml_count(map); ++i) {
        chart_state *child = &parser->plan->states[first];
        if (history(child->kind)) {
            praktor_yaml_node node = {0};
            int rc = praktor_yaml_get(praktor_yaml_at(map, i), "default", &node, NULL);
            if (rc == SALTS_OK) rc = direct_child(parser, node, parent, &child->initial);
            if (rc != SALTS_OK) return rc;
        }
        first = child->end;
    }
    return SALTS_OK;
}

static int resolve_transitions(chart_parser *parser, praktor_yaml_node node) {
    praktor_statechart_plan *plan = parser->plan;
    for (size_t i = 0; i < plan->transition_count; ++i) {
        chart_transition *row = &plan->transitions[i];
        if (row->trigger == CHART_DONE) {
            row->done = find_state(plan, row->done_name);
            const chart_state *done = &plan->states[row->done];
            if (!row->done || row->done < row->source || row->done >= plan->states[row->source].end ||
                (done->kind != CHART_COMPOUND && done->kind != CHART_PARALLEL))
                return fail(parser, node, SALTS_EINVAL, "invalid on_done state");
        }
        row->target = find_state(plan, row->target_name);
        if (!row->target) return fail(parser, node, SALTS_EINVAL, "unknown transition target");
        size_t target = row->target;
        if (history(plan->states[target].kind)) target = plan->states[target].parent;
        if (row->internal) {
            if (plan->states[row->source].kind != CHART_COMPOUND ||
                target <= row->source || target >= plan->states[row->source].end)
                return fail(parser, node, SALTS_EINVAL, "internal target requires a proper descendant of a compound state");
            row->domain = row->source;
            continue;
        }
        size_t ancestor = plan->states[row->source].parent;
        while (ancestor && (plan->states[ancestor].kind != CHART_COMPOUND ||
                            target <= ancestor || target >= plan->states[ancestor].end))
            ancestor = plan->states[ancestor].parent;
        row->domain = ancestor;
    }
    return SALTS_OK;
}

int praktor_statechart_plan_create(praktor_yaml_node chart,const salts_bt_tree_config *limits,
    bool has_event_source,
    praktor_statechart_plan **out, char *error, size_t capacity) {
    static const char *const keys[] = {"initial", "states", "events"};
    praktor_yaml_node fields[3] = {{0}};
    chart_parser parser = {.limits=limits,.error=error,.capacity=capacity,
                           .has_event_source=has_event_source};
    int rc = read_map(&parser, chart, keys, fields, 3u);
    if (rc != SALTS_OK) return rc;
    praktor_statechart_plan *plan = calloc(1u, sizeof(*plan));
    if (!plan) return SALTS_ENOMEM;
    parser.plan = plan;
    plan->state_count = 1u;
    plan->states[0].kind = CHART_COMPOUND;
    rc = add_lifecycle_event(plan, "task.started");
    if (rc == SALTS_OK) rc = add_lifecycle_event(plan, "task.finished");
    if (rc == SALTS_OK) rc = read_events(&parser, fields[2]);
    if (rc == SALTS_OK) rc = read_states(&parser, fields[1], 0u, 1u);
    plan->states[0].end = plan->state_count;
    if (rc == SALTS_OK) rc = direct_child(&parser, fields[0], 0u, &plan->states[0].initial);
    if (rc == SALTS_OK) rc = resolve_transitions(&parser, chart);
    if (rc == SALTS_OK) *out = plan;
    else praktor_statechart_plan_destroy(plan);
    return rc;
}

void praktor_statechart_plan_destroy(praktor_statechart_plan *plan) {
    if (!plan) return;
    for (size_t i = 0; i < plan->transition_count; ++i) {
        praktor_orch_session_plan_destroy(plan->transitions[i].guard);
    }
    for (size_t i = 0; i < plan->event_count; ++i) free(plan->events[i].fields);
    free(plan);
}
