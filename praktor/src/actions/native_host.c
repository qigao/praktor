#include "actions/native_host.h"
#include "actions/json_selection.h"
#include "actions/output_scan.h"
#include "actions/regex_pcre2.h"
#include "actions/variable_reference.h"
#include <json_parser.h>
#include <ctype.h>
#include <core/clock.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_sleep_state {
  const void *session_owner;
  size_t node_index;
  uint64_t started_ns;
  uint64_t duration_ns;
  bool active;
} native_sleep_state;

typedef struct native_regex_cache_entry {
  char *pattern;
  praktor_regex *regex;
  praktor_regex_status status;
  bool occupied;
} native_regex_cache_entry;

#define PRAKTOR_NATIVE_REGEX_CACHE_DEFAULT 8u
#define PRAKTOR_NATIVE_REGEX_CAPTURE_DEFAULT 64u

typedef struct native_host_impl {
  praktor_value_store values;
  const praktor_value_store *parent_values;
  const praktor_value_store *event_values;
  bool reserve_event_fields;
  int (*chart_active)(void *user, const char *field);
  void *chart_user;
  size_t scratch_capacity;
  size_t max_value_bytes;
  char *scratch;
  praktor_output_pair *pairs;
  size_t pair_capacity;
  native_sleep_state *sleep_states;
  size_t max_running_leaves;
  native_regex_cache_entry *regex_cache;
  char *regex_patterns;
  size_t regex_cache_capacity;
  praktor_regex_limits regex_limits;
  uint64_t (*now_ns)(void *user);
  void *clock_user;
} native_host_impl;

static int value_result(int result) {
  switch (result) {
    case PRAKTOR_VALUE_OK: return PRAKTOR_BT_VALUE_OK;
    case PRAKTOR_VALUE_ENOTFOUND: return PRAKTOR_BT_VALUE_NOT_FOUND;
    case PRAKTOR_VALUE_ENOBUFS: return PRAKTOR_BT_VALUE_BUFFER_TOO_SMALL;
    default: return PRAKTOR_BT_VALUE_ERROR;
  }
}
/* Explicit task/root values override inherited process environment. Dotenv is
 * loaded by the caller before execution; never cache or mutate env here. */
static bool event_field(native_host_impl *impl, const char *key) {
  return impl->reserve_event_fields && key && !strncmp(key, "event.", 6u);
}
static bool chart_field(native_host_impl *impl, const char *key) {
  return impl->chart_active && key && !strncmp(key, "chart.", 6u);
}
static int read_value(native_host_impl *impl, const char *key, char *value,
                      size_t capacity, size_t *size) {
  if (chart_field(impl,key)) {
    const int active=impl->chart_active(impl->chart_user,key+6u);
    if(active<0)return PRAKTOR_VALUE_ENOTFOUND;
    *size=1u;
    if(!value||capacity<=1u)return PRAKTOR_VALUE_ENOBUFS;
    value[0]=active?'1':'0';
    value[1]='\0';
    return PRAKTOR_VALUE_OK;
  }
  if (event_field(impl,key))
    return impl->event_values ? praktor_value_store_get(impl->event_values,key,value,capacity,size)
                              : PRAKTOR_VALUE_ENOTFOUND;
  int rc=praktor_value_store_get(&impl->values,key,value,capacity,size);
  if (rc == PRAKTOR_VALUE_ENOTFOUND && impl->parent_values)
    return praktor_value_store_get(impl->parent_values,key,value,capacity,size);
  return rc;
}
static int lookup_value(native_host_impl *impl,const char *key,char *value,
                        size_t capacity,size_t *size) {
  int rc=read_value(impl,key,value,capacity,size);
  if(rc!=PRAKTOR_VALUE_ENOTFOUND||event_field(impl,key)||chart_field(impl,key))return rc;
  const char *environment=getenv(key);
  if(!environment)return PRAKTOR_VALUE_ENOTFOUND;
  size_t length=strlen(environment);
  *size=length;
  if(length>impl->max_value_bytes||!value||capacity<=length)return PRAKTOR_VALUE_ENOBUFS;
  memcpy(value,environment,length+1u);return PRAKTOR_VALUE_OK;
}
static int get_value(void *user, const char *key, char *value,
                     size_t capacity, size_t *size) {
  native_host_impl *impl = (native_host_impl *)user;
  return value_result(lookup_value(impl,key,value,capacity,size));
}
static int write_value(native_host_impl *impl, const char *key, const char *value) {
  if (event_field(impl,key)||chart_field(impl,key)) return PRAKTOR_VALUE_EINVAL;
  return praktor_value_store_set(&impl->values,key,value);
}
static int set_value(void *user, const char *key, const char *value) {
  native_host_impl *impl = (native_host_impl *)user;
  return value_result(write_value(impl,key,value));
}
static int contains_value(void *user, const char *key, int *contains) {
  native_host_impl *impl = (native_host_impl *)user;
  if (chart_field(impl,key)) {
    *contains=impl->chart_active(impl->chart_user,key+6u)>=0;
    return PRAKTOR_BT_VALUE_OK;
  }
  if (event_field(impl,key)) {
    *contains=0;
    return impl->event_values ? value_result(praktor_value_store_contains(impl->event_values,key,contains))
                              : PRAKTOR_BT_VALUE_OK;
  }
  int result=praktor_value_store_contains(&impl->values,key,contains);
  if (result == PRAKTOR_VALUE_OK && !*contains && impl->parent_values)
    result=praktor_value_store_contains(impl->parent_values,key,contains);
  if(result==PRAKTOR_VALUE_OK&&!*contains&&getenv(key))*contains=1;
  return value_result(result);
}
static const praktor_leaf_parameter *parameter(
    const praktor_leaf_request *request, const char *name) {
  size_t i;
  for (i = 0u; i < request->parameter_count; ++i)
    if (strcmp(request->parameters[i].name, name) == 0) return &request->parameters[i];
  return NULL;
}
static int copy_text(char *out, size_t capacity, const char *text) {
  size_t length = 0u;
  if (text == NULL) return SALTS_EINVAL;
  while (length < capacity && text[length] != '\0') ++length;
  if (length == capacity) return SALTS_ENOBUFS;
  memcpy(out, text, length + 1u);
  return SALTS_OK;
}
static int resolve(native_host_impl *impl, const praktor_leaf_parameter *param,
                    char *out) {
  size_t required = 0u;
  int written;
  if (param == NULL) return SALTS_EINVAL;
  switch (param->kind) {
    case PRAKTOR_LEAF_BOOLEAN:
      return copy_text(out, impl->scratch_capacity, param->boolean ? "true" : "false");
    case PRAKTOR_LEAF_NUMBER:
      if (!isfinite(param->number)) return SALTS_EINVAL;
      written = snprintf(out, impl->scratch_capacity, "%.6f", param->number);
      return written >= 0 && (size_t)written < impl->scratch_capacity ? SALTS_OK : SALTS_ENOBUFS;
    case PRAKTOR_LEAF_STRING: {
      if (param->text == NULL) return SALTS_EINVAL;
      const vstr text = vstr_from_cstr(param->text);
      if (praktor_retired_variable_reference(text)) return SALTS_EINVAL;
      const vstr key = praktor_variable_reference(text);
      if (key.data) {
        return lookup_value(impl, key.data, out,
                                           impl->scratch_capacity, &required) == PRAKTOR_VALUE_OK
            ? SALTS_OK : SALTS_EINVAL;
      }
      return copy_text(out, impl->scratch_capacity, param->text);
    }
  }
  return SALTS_EINVAL;
}
static const char *source_key(const praktor_leaf_parameter *param, const char *fallback) {
  if (!param) return fallback;
  return param->kind == PRAKTOR_LEAF_STRING && param->text
      ? praktor_variable_reference(vstr_from_cstr(param->text)).data : NULL;
}
static int parse_integer(const char *text, int *out) {
  char *end = NULL;
  double value;
  errno = 0;
  value = strtod(text, &end);
  if (end == text || *end != '\0' || errno == ERANGE || !isfinite(value) ||
      value < INT_MIN || value > INT_MAX || fabs(value - trunc(value)) > 0.0) return SALTS_EINVAL;
  *out = (int)value;
  return SALTS_OK;
}

static int resolve_integer(native_host_impl *impl,
                           const praktor_leaf_parameter *param,
                           int default_value, char *scratch, int *out) {
  int result;
  if (param == NULL) {
    *out = default_value;
    return SALTS_OK;
  }
  if (param->kind == PRAKTOR_LEAF_BOOLEAN) return SALTS_EINVAL;
  if (param->kind == PRAKTOR_LEAF_NUMBER) {
    if (!isfinite(param->number) || param->number < INT_MIN ||
        param->number > INT_MAX ||
        fabs(param->number - trunc(param->number)) > 0.0)
      return SALTS_EINVAL;
    *out = (int)param->number;
    return SALTS_OK;
  }
  result = resolve(impl, param, scratch);
  if (result != SALTS_OK) return result;
  return parse_integer(scratch, out);
}

static praktor_regex_status cached_regex(native_host_impl *impl,
                                         const char *pattern,
                                         praktor_regex **out_regex) {
  native_regex_cache_entry *available = NULL;
  size_t i, pattern_size;
  *out_regex = NULL;
  for (i = 0u; i < impl->regex_cache_capacity; ++i) {
    native_regex_cache_entry *entry = &impl->regex_cache[i];
    if (!entry->occupied) {
      if (available == NULL) available = entry;
    } else if (strcmp(entry->pattern, pattern) == 0) {
      *out_regex = entry->regex;
      return entry->status;
    }
  }
  if (available == NULL) return PRAKTOR_REGEX_LIMIT;
  pattern_size = strlen(pattern);
  if (pattern_size >= impl->scratch_capacity) return PRAKTOR_REGEX_LIMIT;
  memcpy(available->pattern, pattern, pattern_size + 1u);
  available->status = praktor_regex_compile(
      pattern, pattern_size, &impl->regex_limits, &available->regex);
  available->occupied = true;
  *out_regex = available->regex;
  return available->status;
}

static int regex_error(praktor_regex_status status) {
  switch (status) {
    case PRAKTOR_REGEX_UNSUPPORTED: return SALTS_ENOTSUP;
    case PRAKTOR_REGEX_LIMIT: return SALTS_ENOBUFS;
    case PRAKTOR_REGEX_NOMEM: return SALTS_ENOMEM;
    case PRAKTOR_REGEX_ENGINE: return SALTS_EIO;
    default: return SALTS_EINVAL;
  }
}
static native_sleep_state *find_sleep(native_host_impl *impl,
                                       const praktor_leaf_request *request) {
  size_t i;
  for (i = 0u; i < impl->max_running_leaves; ++i) {
    native_sleep_state *state = &impl->sleep_states[i];
    if (state->active && state->session_owner == request->session_owner &&
        state->node_index == request->node_index) return state;
  }
  return NULL;
}

/* Supported duration forms match the legacy non-negative finite range and
 * truncation to integer milliseconds. Unknown suffixes are rejected rather
 * than silently interpreted as milliseconds. */
static int sleep_duration(native_host_impl *impl,
                           const praktor_leaf_parameter *param,
                           uint64_t *out_duration_ns) {
  double value = 0.0, multiplier = 1.0;
  char *end = NULL;
  int result;
  if (param != NULL) {
    if (param->kind == PRAKTOR_LEAF_NUMBER) {
      value = param->number;
    } else if (param->kind == PRAKTOR_LEAF_STRING) {
      result = resolve(impl, param, impl->scratch);
      if (result != SALTS_OK) return result;
      errno = 0;
      value = strtod(impl->scratch, &end);
      if (end == impl->scratch || errno == ERANGE) return SALTS_EINVAL;
      if (strcmp(end, "s") == 0) multiplier = 1000.0;
      else if (strcmp(end, "m") == 0) multiplier = 60000.0;
      else if (strcmp(end, "h") == 0) multiplier = 3600000.0;
      else if (*end != '\0' && strcmp(end, "ms") != 0) return SALTS_EINVAL;
    } else return SALTS_EINVAL;
  }
  value *= multiplier;
  if (!isfinite(value) || value < 0.0 || value > INT_MAX) return SALTS_EINVAL;
  *out_duration_ns = (uint64_t)value * UINT64_C(1000000);
  return SALTS_OK;
}

static uint64_t default_now_ns(void *user) { (void)user; return hrtime(); }

static int tick_sleep(native_host_impl *impl, const praktor_leaf_request *request,
                       salts_bt_node_status *status) {
  native_sleep_state *state;
  uint64_t now, duration = 0u;
  size_t i;
  int result;
  if (request->session_owner == NULL) return SALTS_EINVAL;
  state = find_sleep(impl, request);
  if (state == NULL) {
    result = sleep_duration(impl, parameter(request, "duration"), &duration);
    if (result != SALTS_OK) return result;
    if (duration == 0u) { *status = SALTS_BT_SUCCESS; return SALTS_OK; }
    for (i = 0u; i < impl->max_running_leaves; ++i) {
      if (!impl->sleep_states[i].active) { state = &impl->sleep_states[i]; break; }
    }
    if (state == NULL) return SALTS_ENOBUFS;
    state->session_owner = request->session_owner;
    state->node_index = request->node_index;
    state->duration_ns = duration;
    state->started_ns = impl->now_ns(impl->clock_user);
    state->active = true;
    *status = SALTS_BT_RUNNING;
    return SALTS_OK;
  }
  now = impl->now_ns(impl->clock_user);
  if (now < state->started_ns) return SALTS_EINVAL;
  if (now - state->started_ns < state->duration_ns) {
    *status = SALTS_BT_RUNNING;
  } else {
    memset(state, 0, sizeof(*state));
    *status = SALTS_BT_SUCCESS;
  }
  return SALTS_OK;
}

static int tick_parse_lines(native_host_impl *impl,
                             const praktor_leaf_request *request,
                             salts_bt_node_status *status) {
  const praktor_leaf_parameter *param;
  char *key = impl->scratch;
  char *input = key + impl->scratch_capacity;
  char *temporary = input + impl->scratch_capacity;
  char count_text[3u * sizeof(size_t) + 1u];
  char *cursor;
  praktor_regex *filter = NULL;
  praktor_regex_status regex_status = PRAKTOR_REGEX_OK;
  size_t required = 0u, key_size, written = 0u, count = 0u;
  int result, count_size;
  praktor_value_write writes[2];
  param = parameter(request, "filter");
  if (param != NULL) {
    result = resolve(impl, param, input);
    if (result != SALTS_OK) return result;
    if (*input != '\0') {
      regex_status = cached_regex(impl, input, &filter);
      /* Legacy deliberately treats an invalid filter as no filter. */
      if (regex_status == PRAKTOR_REGEX_INVALID_PATTERN) filter = NULL;
      else if (regex_status != PRAKTOR_REGEX_OK)
        return regex_error(regex_status);
    }
  }
  param = parameter(request, "input_key");
  const char *source = source_key(param, "shell_output");
  if (!source) return SALTS_EINVAL;
  result = lookup_value(impl, source, input,
                                       impl->scratch_capacity, &required);
  if (result == PRAKTOR_VALUE_ENOTFOUND) {
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (result != PRAKTOR_VALUE_OK) return SALTS_EINVAL;
  param = parameter(request, "output_key");
  result = param == NULL ? copy_text(key, impl->scratch_capacity, "parsed_lines")
                         : resolve(impl, param, key);
  if (result != SALTS_OK) return result;
  key_size = strlen(key);
  if (impl->scratch_capacity - key_size <= sizeof("_count") - 1u)
    return SALTS_ENOBUFS;
  memcpy(temporary, key, key_size);
  memcpy(temporary + key_size, "_count", sizeof("_count"));
  /* Match getline + Legacy's !result.empty() join exactly: CR is retained,
   * a final LF is not another line, and leading empty lines count but do not
   * add separators before the first nonempty line. Compact in place. */
  cursor = input;
  while (*cursor != '\0') {
    char *end = strchr(cursor, '\n');
    size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
    char *next = end == NULL ? cursor + length : end + 1;
    if (filter != NULL) {
      praktor_regex_span match;
      regex_status = praktor_regex_search_group(
          filter, cursor, length, 0u, &match);
      if (regex_status == PRAKTOR_REGEX_NO_MATCH) {
        cursor = next;
        continue;
      }
      if (regex_status != PRAKTOR_REGEX_OK) return regex_error(regex_status);
    }
    ++count;
    if (written != 0u) input[written++] = '\n';
    memmove(input + written, cursor, length);
    written += length;
    cursor = next;
  }
  input[written] = '\0';
  count_size = snprintf(count_text, sizeof(count_text), "%zu", count);
  if (count_size < 0 || (size_t)count_size >= sizeof(count_text)) return SALTS_ENOBUFS;
  writes[0].key = key; writes[0].value = input;
  writes[1].key = temporary; writes[1].value = count_text;
  if (event_field(impl,key) || event_field(impl,temporary) ||
      chart_field(impl,key) || chart_field(impl,temporary)) return SALTS_EINVAL;
  result = praktor_value_store_set_many(&impl->values, writes, 2u);
  if (result != PRAKTOR_VALUE_OK)
    return result == PRAKTOR_VALUE_EINVAL ? SALTS_EINVAL : SALTS_ENOBUFS;
  *status = SALTS_BT_SUCCESS;
  return SALTS_OK;
}

static int json_output(native_host_impl *impl,const char *key,const char *value) {
  const int written=write_value(impl,key,value);
  if(written==PRAKTOR_VALUE_OK)return SALTS_OK;
  if(written==PRAKTOR_VALUE_ENOMEM)return SALTS_ENOMEM;
  if(written==PRAKTOR_VALUE_EINVAL)return SALTS_EINVAL;
  return SALTS_ENOBUFS;
}

/* The default result keeps the existing first-match DOM semantics. The
 * cardinality-aware modes count matches in the SDK's SAX query stream. */
static int tick_parse_json(native_host_impl *impl,
                           const praktor_leaf_request *request,
                           salts_bt_node_status *status) {
  const praktor_leaf_parameter *param;
  char *key=impl->scratch;
  char *input=key+impl->scratch_capacity;
  char *path=input+impl->scratch_capacity;
  char *out_key=path+impl->scratch_capacity;
  json_value_t *document=NULL,*selected=NULL;
  char *serialized=NULL;
  const char *value=NULL;
  size_t required=0u,length=0u;
  int r;
  praktor_json_result mode=PRAKTOR_JSON_FIRST;
  param=parameter(request,"result");
  if(param&&(param->kind!=PRAKTOR_LEAF_STRING||!param->text||
             praktor_json_result_parse(param->text,&mode)!=SALTS_OK))return SALTS_EINVAL;
  param=parameter(request,"input_key");
  const char *source=source_key(param,"shell_output");
  if(!source)return SALTS_EINVAL;
  r=lookup_value(impl,source,input,
                                impl->scratch_capacity,&required);
  if(r==PRAKTOR_VALUE_ENOTFOUND) {
    if(mode!=PRAKTOR_JSON_FIRST) {
      if(request->error&&request->error_capacity)
        (void)snprintf(request->error,request->error_capacity,
                       "ParseJson: input variable '%s' is missing",source);
      return SALTS_ENOENT;
    }
    *status=SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if(r!=PRAKTOR_VALUE_OK)
    return r==PRAKTOR_VALUE_ENOBUFS?SALTS_ENOBUFS:SALTS_EINVAL;
  param=parameter(request,"path");
  const char *fixed_path="";
  if(mode==PRAKTOR_JSON_FIRST) {
    r=param?resolve(impl,param,path):copy_text(path,impl->scratch_capacity,"");
    if(r!=SALTS_OK)return r;
    r=praktor_json_path_normalize(path,impl->scratch_capacity);
    if(r!=SALTS_OK)return r;
  }else if(param){
    if(param->kind!=PRAKTOR_LEAF_STRING||!param->text)return SALTS_EINVAL;
    fixed_path=param->text;
  }
  param=parameter(request,"output_key");
  r=param?resolve(impl,param,out_key)
         :copy_text(out_key,impl->scratch_capacity,"parsed_output");
  if(r!=SALTS_OK)return r;
  if(mode!=PRAKTOR_JSON_FIRST) {
    r=praktor_json_selection_run(mode,fixed_path,input,required,path,
        impl->max_value_bytes+1u,request->error,request->error_capacity);
    if(r!=SALTS_OK)return r;
    r=json_output(impl,out_key,path);
    if(r==SALTS_OK)*status=SALTS_BT_SUCCESS;
    return r;
  }
  document=json_parse(input,strlen(input));
  if(!document){*status=SALTS_BT_FAILURE;return SALTS_OK;}
  selected=json_path_get(document,path); /* borrowed from document */
  if(!selected){json_free(document);*status=SALTS_BT_FAILURE;return SALTS_OK;}
  if(json_type(selected)==JSON_STRING){
    value=json_string(selected);
    length=json_string_len(selected);
    if((!value&&length)||(length&&memchr(value,'\0',length))){
      json_free(document);return SALTS_ENOTSUP;
    }
    if(!value)value="";
  }else{
    serialized=json_serialize(selected,&length);
    if(!serialized){json_free(document);return SALTS_ENOMEM;}
    value=serialized;
  }
  if(length>=impl->scratch_capacity)r=SALTS_ENOBUFS;
  else r=json_output(impl,out_key,value);
  json_serialize_free(serialized);
  json_free(document);
  if(r!=SALTS_OK)return r;
  *status=SALTS_BT_SUCCESS;return SALTS_OK;
}

static int tick_parse_regex(native_host_impl *impl,
                            const praktor_leaf_request *request,
                            salts_bt_node_status *status) {
  const praktor_leaf_parameter *param;
  char *key = impl->scratch;
  char *input = key + impl->scratch_capacity;
  char *temporary = input + impl->scratch_capacity;
  char *pattern = temporary + impl->scratch_capacity;
  praktor_regex *regex = NULL;
  praktor_regex_span match;
  praktor_regex_status regex_status;
  size_t required = 0u;
  int result, capture_group = 1;

  param = parameter(request, "input_key");
  const char *source = source_key(param, "shell_output");
  if (!source) return SALTS_EINVAL;
  result = lookup_value(impl, source, input,
                                       impl->scratch_capacity, &required);
  if (result == PRAKTOR_VALUE_ENOTFOUND) {
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (result != PRAKTOR_VALUE_OK) return SALTS_EINVAL;

  param = parameter(request, "pattern");
  result = param == NULL ? copy_text(pattern, impl->scratch_capacity, "")
                         : resolve(impl, param, pattern);
  if (result != SALTS_OK) return result;
  regex_status = cached_regex(impl, pattern, &regex);
  if (regex_status == PRAKTOR_REGEX_INVALID_PATTERN) {
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (regex_status != PRAKTOR_REGEX_OK) return regex_error(regex_status);
  result = resolve_integer(impl, parameter(request, "capture_group"), 1,
                           temporary, &capture_group);
  if (result != SALTS_OK) return result;
  if (capture_group <= 0 ||
      (size_t)capture_group > praktor_regex_capture_count(regex)) {
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  regex_status = praktor_regex_search_group(
      regex, input, strlen(input), (size_t)capture_group, &match);
  if (regex_status == PRAKTOR_REGEX_NO_MATCH) {
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (regex_status != PRAKTOR_REGEX_OK) return regex_error(regex_status);

  param = parameter(request, "output_key");
  result = param == NULL ? copy_text(key, impl->scratch_capacity, "parsed_output")
                         : resolve(impl, param, key);
  if (result != SALTS_OK) return result;
  if (!match.participated) {
    temporary[0] = '\0';
  } else {
    if (match.size >= impl->scratch_capacity) return SALTS_ENOBUFS;
    memcpy(temporary, input + match.offset, match.size);
    temporary[match.size] = '\0';
  }
  result = write_value(impl,key,temporary);
  if (result != PRAKTOR_VALUE_OK)
    return result == PRAKTOR_VALUE_EINVAL ? SALTS_EINVAL : SALTS_ENOBUFS;
  *status = SALTS_BT_SUCCESS;
  return SALTS_OK;
}

static int tick_parse_key_value(native_host_impl *impl,
    const praktor_leaf_request *request, salts_bt_node_status *status) {
  const char *names[] = {"delimiter", "line_separator", "output_key"};
  const char *defaults[] = {"=", "\n", "parsed_output"};
  char *input = impl->scratch;
  char *delimiter = input + impl->scratch_capacity;
  char *separator = delimiter + impl->scratch_capacity;
  char *key = separator + impl->scratch_capacity;
  char *output = key + impl->scratch_capacity;
  char *ports[] = {delimiter, separator, key};
  size_t i, required = 0u;
  int result;
  /* Snapshot the input, then resolve the remaining ports. All
   * aliases are safe: no value store publication happens until encoding ends. */
  const char *source = source_key(parameter(request, "input_key"), "shell_output");
  if (!source) return SALTS_EINVAL;
  result = lookup_value(impl, source, input, impl->scratch_capacity, &required);
  if (result == PRAKTOR_VALUE_ENOTFOUND) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
  if (result != PRAKTOR_VALUE_OK) return SALTS_EINVAL;
  for (i = 0u; i < 3u; ++i) {
    const praktor_leaf_parameter *param = parameter(request, names[i]);
    result = param == NULL ? copy_text(ports[i], impl->scratch_capacity, defaults[i])
        : resolve(impl, param, ports[i]);
    if (result != SALTS_OK) return result;
  }
  result = praktor_output_key_value_json(
      (praktor_output_span){input, strlen(input)},
      (praktor_output_span){delimiter, strlen(delimiter)},
      (praktor_output_span){separator, strlen(separator)},
      impl->pairs, impl->pair_capacity, output, impl->scratch_capacity);
  if (result != 0) return result == -2 ? SALTS_ENOBUFS : SALTS_EINVAL;
  result = write_value(impl,key,output);
  if (result != PRAKTOR_VALUE_OK)
    return result == PRAKTOR_VALUE_EINVAL ? SALTS_EINVAL : SALTS_ENOBUFS;
  *status = SALTS_BT_SUCCESS;
  return SALTS_OK;
}

static int tick_leaf(void *user, const praktor_leaf_request *request,
                      salts_bt_node_status *status) {
  native_host_impl *impl = (native_host_impl *)user;
  const praktor_leaf_parameter *param;
  char *key, *value;
  int result, expected = 0, actual = 0;
  size_t required = 0u, i, j;
  if (impl == NULL || request == NULL || request->action == NULL || status == NULL ||
      (request->parameter_count != 0u && request->parameters == NULL)) return SALTS_EINVAL;
  /* This host owns one root store, not branch-local transactions. */
  if (request->in_parallel_branch) return SALTS_ENOTSUP;
  for (i = 0u; i < request->parameter_count; ++i) {
    if (request->parameters[i].name == NULL) return SALTS_EINVAL;
    for (j = 0u; j < i; ++j)
      if (strcmp(request->parameters[i].name, request->parameters[j].name) == 0) return SALTS_EINVAL;
  }
  if (strcmp(request->action, "Sleep") == 0) return tick_sleep(impl, request, status);
  if (find_sleep(impl, request) != NULL) return SALTS_EINVAL;
  if (strcmp(request->action, "ParseJson") == 0) return tick_parse_json(impl, request, status);
  if (strcmp(request->action, "ParseRegex") == 0) return tick_parse_regex(impl, request, status);
  if (strcmp(request->action, "ParseLines") == 0) return tick_parse_lines(impl, request, status);
  if (strcmp(request->action, "ParseKeyValue") == 0) return tick_parse_key_value(impl, request, status);
  key = impl->scratch;
  value = key + impl->scratch_capacity;
  if (strcmp(request->action, "SetVariable") == 0) {
    param = parameter(request, "key");
    if (param == NULL) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
    result = resolve(impl, param, key);
    if (result != SALTS_OK) return result;
    param = parameter(request, "value");
    if (param != NULL) {
      result = resolve(impl, param, value);
    } else if ((param = parameter(request, "from")) != NULL) {
      const char *source = source_key(param, NULL);
      if (!source) return SALTS_EINVAL;
      result = lookup_value(impl, source, value, impl->scratch_capacity, &required);
      if (result == PRAKTOR_VALUE_ENOTFOUND) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
      result = result == PRAKTOR_VALUE_OK ? SALTS_OK : SALTS_EINVAL;
    } else { *status = SALTS_BT_FAILURE; return SALTS_OK; }
    if (result != SALTS_OK) return result;
    result = write_value(impl,key,value);
    if (result != PRAKTOR_VALUE_OK) return result == PRAKTOR_VALUE_EINVAL ? SALTS_EINVAL : SALTS_ENOBUFS;
    *status = SALTS_BT_SUCCESS;
    return SALTS_OK;
  }
  if (strcmp(request->action, "CheckExitCode") == 0) {
    param = parameter(request, "input_key");
    const char *source = source_key(param, "shell_exit_code");
    if (!source) return SALTS_EINVAL;
    param = parameter(request, "expected");
    if (param != NULL) {
      if (param->kind == PRAKTOR_LEAF_BOOLEAN) return SALTS_EINVAL;
      if (param->kind == PRAKTOR_LEAF_NUMBER) {
        if (!isfinite(param->number) || param->number < INT_MIN || param->number > INT_MAX ||
            fabs(param->number - trunc(param->number)) > 0.0) return SALTS_EINVAL;
        expected = (int)param->number;
      } else {
        result = resolve(impl, param, value);
        if (result != SALTS_OK || parse_integer(value, &expected) != SALTS_OK) return SALTS_EINVAL;
      }
    }
    result = lookup_value(impl, source, value,
                                         impl->scratch_capacity, &required);
    if (result == PRAKTOR_VALUE_ENOTFOUND) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
    if (result != PRAKTOR_VALUE_OK || parse_integer(value, &actual) != SALTS_OK) return SALTS_EINVAL;
    *status = actual == expected ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  return SALTS_ENOTSUP;
}
static void halt_typed_leaf(void *user, const praktor_leaf_request *request) {
  native_host_impl *impl = (native_host_impl *)user;
  native_sleep_state *state;
  if (impl == NULL || request == NULL || request->session_owner == NULL) return;
  state = find_sleep(impl, request);
  if (state != NULL) memset(state, 0, sizeof(*state));
}

int praktor_native_host_init_with_config(
    praktor_native_host *host, const praktor_native_host_config *config) {
  native_host_impl *impl;
  size_t scratch_bytes, regex_capacity, regex_captures, i;
  int result;
  if (host == NULL || host->impl != NULL || config == NULL) return SALTS_EINVAL;
  scratch_bytes = config->max_key_bytes > config->max_value_bytes
      ? config->max_key_bytes : config->max_value_bytes;
  regex_capacity = config->regex_cache_capacity == 0u
      ? PRAKTOR_NATIVE_REGEX_CACHE_DEFAULT : config->regex_cache_capacity;
  regex_captures = config->max_regex_captures == 0u
      ? PRAKTOR_NATIVE_REGEX_CAPTURE_DEFAULT : config->max_regex_captures;
  if (scratch_bytes == SIZE_MAX || scratch_bytes + 1u > SIZE_MAX / 5u ||
      config->max_value_bytes / 2u + 1u > SIZE_MAX / sizeof(praktor_output_pair) ||
      config->max_running_leaves > SIZE_MAX / sizeof(native_sleep_state) ||
      regex_capacity > SIZE_MAX / sizeof(native_regex_cache_entry) ||
      regex_capacity > SIZE_MAX / (scratch_bytes + 1u) ||
      regex_captures > PRAKTOR_NATIVE_REGEX_CAPTURE_DEFAULT) return SALTS_EINVAL;
  impl = (native_host_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  result = praktor_value_store_init(&impl->values, config->value_capacity,
                                        config->max_key_bytes, config->max_value_bytes);
  if (result != PRAKTOR_VALUE_OK) {
    free(impl);
    return result == PRAKTOR_VALUE_ENOMEM ? SALTS_ENOMEM : SALTS_EINVAL;
  }
  impl->scratch_capacity = scratch_bytes + 1u;
  impl->max_value_bytes = config->max_value_bytes;
  impl->parent_values = config->parent_values;
  impl->reserve_event_fields = config->reserve_event_fields;
  impl->chart_active = config->chart_active;
  impl->chart_user = config->chart_user;
  impl->scratch = (char *)calloc(5u, impl->scratch_capacity);
  impl->pair_capacity = config->max_value_bytes / 2u + 1u;
  impl->pairs = (praktor_output_pair *)calloc(impl->pair_capacity, sizeof(praktor_output_pair));
  if (config->max_running_leaves != 0u)
    impl->sleep_states = (native_sleep_state *)calloc(config->max_running_leaves, sizeof(native_sleep_state));
  impl->regex_cache = (native_regex_cache_entry *)calloc(
      regex_capacity, sizeof(native_regex_cache_entry));
  impl->regex_patterns = (char *)calloc(regex_capacity, impl->scratch_capacity);
  if (impl->scratch == NULL || impl->pairs == NULL ||
      (config->max_running_leaves != 0u && impl->sleep_states == NULL) ||
      impl->regex_cache == NULL || impl->regex_patterns == NULL) {
    praktor_value_store_destroy(&impl->values);
    free(impl->scratch); free(impl->pairs); free(impl->sleep_states);
    free(impl->regex_cache); free(impl->regex_patterns); free(impl);
    return SALTS_ENOMEM;
  }
  for (i = 0u; i < regex_capacity; ++i)
    impl->regex_cache[i].pattern =
        impl->regex_patterns + i * impl->scratch_capacity;
  impl->regex_cache_capacity = regex_capacity;
  impl->regex_limits.max_capture_count = regex_captures;
  impl->max_running_leaves = config->max_running_leaves;
  impl->now_ns = config->now_ns == NULL ? default_now_ns : config->now_ns;
  impl->clock_user = config->clock_user;
  host->impl = impl;
  return SALTS_OK;
}

void praktor_native_host_config_init(
    praktor_native_host_config *config, size_t value_capacity,
    size_t max_key_bytes, size_t max_value_bytes) {
  if (config == NULL) return;
  memset(config, 0, sizeof(*config));
  config->value_capacity = value_capacity;
  config->max_key_bytes = max_key_bytes;
  config->max_value_bytes = max_value_bytes;
  config->regex_cache_capacity = PRAKTOR_NATIVE_REGEX_CACHE_DEFAULT;
  config->max_regex_captures = PRAKTOR_NATIVE_REGEX_CAPTURE_DEFAULT;
}

int praktor_native_host_init(praktor_native_host *host, size_t capacity,
                                 size_t max_key_bytes, size_t max_value_bytes) {
  praktor_native_host_config config;
  praktor_native_host_config_init(
      &config, capacity, max_key_bytes, max_value_bytes);
  return praktor_native_host_init_with_config(host, &config);
}
void praktor_native_host_destroy(praktor_native_host *host) {
  native_host_impl *impl = host == NULL ? NULL : (native_host_impl *)host->impl;
  if (impl == NULL) return;
  praktor_value_store_destroy(&impl->values);
  free(impl->scratch);
  free(impl->pairs);
  free(impl->sleep_states);
  if (impl->regex_cache != NULL) {
    size_t i;
    for (i = 0u; i < impl->regex_cache_capacity; ++i)
      praktor_regex_destroy(impl->regex_cache[i].regex);
  }
  free(impl->regex_cache);
  free(impl->regex_patterns);
  free(impl);
  host->impl = NULL;
}
int praktor_native_host_bind(praktor_native_host *host, praktor_bt_host *out_hooks) {
  if (host == NULL || host->impl == NULL || out_hooks == NULL) return SALTS_EINVAL;
  memset(out_hooks, 0, sizeof(*out_hooks));
  out_hooks->user = host->impl;
  out_hooks->value_user = host->impl;
  out_hooks->value_get = get_value;
  out_hooks->value_set = set_value;
  out_hooks->value_contains = contains_value;
  out_hooks->tick_typed_leaf = tick_leaf;
  out_hooks->halt_typed_leaf = halt_typed_leaf;
  return SALTS_OK;
}
praktor_value_store *praktor_native_host_values(praktor_native_host *host) {
  native_host_impl *impl = host == NULL ? NULL : (native_host_impl *)host->impl;
  return impl == NULL ? NULL : &impl->values;
}
void praktor_native_host_event_values(praktor_native_host *host, const praktor_value_store *values) {
  native_host_impl *impl = host->impl;
  impl->event_values = values;
}
