#ifndef PRAKTOR_YAML_ACTION_PARAMS_H
#define PRAKTOR_YAML_ACTION_PARAMS_H
#include "yml/yaml_fields.h"
#include "yml/command_types.h"
#include <praktor/process_control.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API, outside the installed SDK ABI. Only the listed
 * YAML parameter constructors are provided; this is not an execution backend. */
typedef enum praktor_yaml_action_kind {
    PRAKTOR_YAML_ACTION_DOWNLOAD,
    PRAKTOR_YAML_ACTION_SERVICE,
    PRAKTOR_YAML_ACTION_MANAGED_PROCESS,
    PRAKTOR_YAML_ACTION_COMMAND,
    PRAKTOR_YAML_ACTION_PROGRAM
} praktor_yaml_action_kind;
#define PRAKTOR_YAML_ACTION_MAX_ARGUMENTS 4096u
#define PRAKTOR_YAML_ACTION_MAX_BYTES (1024u * 1024u)

/* Unique opaque owner. Create reads a borrowed task node: named system blocks
 * or Command/Program fields with sibling options. It copies decoded strings
 * and retains no document/runtime references. On failure
 * it frees provisional state and leaves *out untouched. Transfer the pointer,
 * never copy its ownership. Destroy accepts NULL. Independent owners may be used
 * concurrently; no concurrent destruction/read of one owner is permitted.
 *
 * Existing YAML types, defaults, field order, case-sensitive operation names,
 * int timeout conversion and diagnostics are retained. Individual strings retain
 * the 10 KiB field limit for Download/Service/ManagedProcess. Command/Program
 * scalars keep their prior length policy within the aggregate owner limit.
 * Admission bounds allow at most 4096 list entries (arguments/commands) and
 * 1 MiB of retained decoded text including NUL terminators; excess fails closed.
 * These do not bound parser/temporary decoding allocations. Error storage follows
 * yaml_fields.h and must be disjoint from inputs and outputs. Returns SALTS_OK,
 * EINVAL for shape/type/value/capacity refusal, ERANGE for strict integer
 * conversion, or ENOMEM with the origin distinguishing owned/provider failure.
 * Example: create(task, PRAKTOR_YAML_ACTION_SERVICE, &owner, &error), read
 * service(owner, &view), then destroy(owner); dispose error on every path. */
typedef struct praktor_yaml_action_params praktor_yaml_action_params;
int praktor_yaml_action_params_create(praktor_yaml_node task, praktor_yaml_action_kind kind,
    praktor_yaml_action_params **out, praktor_yaml_field_error *error);
void praktor_yaml_action_params_destroy(praktor_yaml_action_params *params);

/* Typed borrowed views, valid until owner destruction, including across pointer
 * transfer. Text views are also NUL terminated, with legacy decoded-NUL truncation.
 * Getters allocate nothing, reject mismatched kinds/invalid arguments, and leave
 * outputs untouched on failure. Operation reuses the native Process contract. */
typedef struct praktor_yaml_download_view {
    vstr url, path, sha256;
    bool overwrite;
    int timeout_ms;
} praktor_yaml_download_view;
typedef struct praktor_yaml_service_view {
    praktor_process_operation operation;
    vstr name, profile;
    size_t argument_count;
    int timeout_ms, poll_interval_ms;
} praktor_yaml_service_view;
typedef struct praktor_yaml_managed_process_view {
    praktor_process_operation operation;
    vstr image_name, executable, working_directory;
    size_t argument_count;
    int startup_timeout_ms, stop_timeout_ms;
    bool force_terminate;
} praktor_yaml_managed_process_view;
typedef struct praktor_yaml_command_view {
    bool sequence;
    vstr command; /* Scalar form only. Sequence entries use action_argument. */
    size_t command_count;
    praktor_yaml_output_format output_format;
    praktor_yaml_postprocessor postprocessor;
    union {
        struct { vstr pattern, output_key; int capture_group; } regex;
        struct { vstr path, output_key; } json;
        struct { vstr filter, output_key; } lines;
        struct { vstr delimiter, line_separator, output_key; } keyvalue;
    } parser;
} praktor_yaml_command_view;
typedef struct praktor_yaml_program_view {
    vstr program, input;
    size_t argument_count;
    praktor_yaml_output_format output_format;
} praktor_yaml_program_view;
int praktor_yaml_action_command(const praktor_yaml_action_params *params, praktor_yaml_command_view *out);
int praktor_yaml_action_program(const praktor_yaml_action_params *params, praktor_yaml_program_view *out);
int praktor_yaml_action_download(const praktor_yaml_action_params *params, praktor_yaml_download_view *out);
int praktor_yaml_action_service(const praktor_yaml_action_params *params, praktor_yaml_service_view *out);
int praktor_yaml_action_managed_process(const praktor_yaml_action_params *params, praktor_yaml_managed_process_view *out);
/* Also reads Command sequence entries. ENOENT for an index outside the list;
 * EINVAL for invalid arguments. */
int praktor_yaml_action_argument(const praktor_yaml_action_params *params, size_t index, vstr *out);

#ifdef __cplusplus
}
#endif
#endif
