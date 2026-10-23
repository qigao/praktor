#include "yml/yaml_action_params.h"
#include <cmeta_fs.h>
#include <fmt.h>
#include <limits.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static praktor_yaml_document *document(const char *source) {
    praktor_yaml_document *owner = NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source), &owner, NULL) == SALTS_OK);
    return owner;
}
static praktor_yaml_action_params *params(const char *source, praktor_yaml_action_kind kind) {
    praktor_yaml_document *doc = document(source);
    praktor_yaml_action_params *owner = NULL;
    praktor_yaml_field_error error = {0};
    int status = praktor_yaml_action_params_create(praktor_yaml_root(doc), kind, &owner, &error);
    info("status %d: %s", status, error.text ? error.text : "");
    check(status == SALTS_OK && owner && !error.text);
    praktor_yaml_document_destroy(doc);
    praktor_yaml_field_error_dispose(&error);
    return owner;
}
static bool is_text(vstr text, const char *expected) { return vstr_eq(text, vstr_from_cstr(expected)) != 0; }
static void error_is(const char *source, praktor_yaml_action_kind kind, const char *message) {
    praktor_yaml_document *doc = document(source);
    praktor_yaml_action_params *previous = params("service: {name: retained}\n", PRAKTOR_YAML_ACTION_SERVICE), *result = previous;
    praktor_yaml_field_error error = {0};
    int status = praktor_yaml_action_params_create(praktor_yaml_root(doc), kind, &result, &error);
    info("status %d: %s", status, error.text ? error.text : "");
    check(status != SALTS_OK && result == previous && error.origin == PRAKTOR_YAML_FAILURE_INPUT);
    check(error.text && strstr(error.text, message));
    praktor_yaml_action_params_destroy(previous); praktor_yaml_document_destroy(doc);
    praktor_yaml_field_error_dispose(&error);
}
static size_t corpus(bool force) {
    tstr path = tstr_format("{}/{}", PRAKTOR_YAML_ACTION_CORPUS_DIR,
        force ? "force_terminate_corpus.tsv" : "system_action_string_corpus.tsv");
    cmeta_fs_buf_t file = {0};
    vstr rest;
    size_t count = 0;
    check(path && cmeta_fs_read_file(path, &file) == SALTS_OK); tstr_free(path);
    rest = vstr_from_buf(file.base, file.len);
    while (rest.len) {
        vstr line = vstr_trim_right(vstr_split_next(&rest, vstr_from_cstr("\n")), "\r"), fields[7];
        size_t index, columns = force ? 5u : 7u;
        tstr source;
        bool service, restart = false;
        praktor_yaml_document *doc;
        praktor_yaml_action_params *owner = NULL;
        praktor_yaml_field_error error = {0};
        int status;
        if (!line.len || line.data[0] == '#') continue;
        for (index = 0; index < columns; ++index) fields[index] = vstr_split_next(&line, vstr_from_cstr("\t"));
        check(!line.len);
        service = !force && is_text(fields[1], "service");
        if (force) source = tstr_format("managed_process:\n  identity: {{image_name: worker.exe}}\n  force_terminate: {}\n", fields[1]);
        else if (service) {
            source = tstr_format("service:\n  name: {}\n  profile: {}\n  arguments: [{}]\n",
                is_text(fields[2], "name") ? fields[3] : vstr_from_cstr("worker"),
                is_text(fields[2], "profile") ? fields[3] : vstr_from_cstr("windows_scm"),
                is_text(fields[2], "arguments") ? fields[3] : vstr_from_cstr("baseline"));
        } else {
            restart = is_text(fields[2], "restart_without_executable");
            source = tstr_format("managed_process:\n  operation: {}\n  identity: {{image_name: {}}}\n  arguments: [{}]\n",
                restart ? "restart" : "status", is_text(fields[2], "image_name") ? fields[3] : vstr_from_cstr("worker.exe"),
                is_text(fields[2], "arguments") ? fields[3] : vstr_from_cstr("baseline"));
            if (!restart) source = tstr_append_format(source, "  executable: {}\n",
                is_text(fields[2], "executable") ? fields[3] : vstr_from_cstr("worker.exe"));
        }
        check(source); doc = document(source); tstr_free(source);
        status = praktor_yaml_action_params_create(praktor_yaml_root(doc), service ? PRAKTOR_YAML_ACTION_SERVICE : PRAKTOR_YAML_ACTION_MANAGED_PROCESS, &owner, &error);
        info("corpus row %zu: status %d: %s", count, status, error.text ? error.text : "");
        if (is_text(fields[force ? 3 : 4], "pass")) {
            check(status == SALTS_OK && !error.text && owner);
            if (force) {
                praktor_yaml_managed_process_view view;
                check(praktor_yaml_action_managed_process(owner, &view) == SALTS_OK && view.force_terminate == is_text(fields[4], "true"));
            } else {
                vstr text = {0};
                if (is_text(fields[2], "arguments")) check(praktor_yaml_action_argument(owner, 0, &text) == SALTS_OK);
                else if (service) {
                    praktor_yaml_service_view view;
                    check(praktor_yaml_action_service(owner, &view) == SALTS_OK);
                    text = is_text(fields[2], "name") ? view.name : view.profile;
                } else {
                    praktor_yaml_managed_process_view view;
                    check(praktor_yaml_action_managed_process(owner, &view) == SALTS_OK);
                    text = is_text(fields[2], "image_name") ? view.image_name : view.executable;
                }
                check(vstr_eq(text, fields[5]));
            }
        } else check(status == SALTS_EINVAL && !owner && error.origin == PRAKTOR_YAML_FAILURE_INPUT &&
            (force ? strstr(error.text, "'force_terminate' must be a boolean") != NULL :
                vstr_find(tstr_to_v(error.text), fields[6]) != SIZE_MAX) );
        praktor_yaml_action_params_destroy(owner); praktor_yaml_document_destroy(doc); praktor_yaml_field_error_dispose(&error);
        ++count;
    }
    cmeta_fs_buf_free(&file);
    return count;
}

typedef struct start_probe { bool running, observed; } start_probe;
static praktor_process_backend_result query(void *context, const char *name, praktor_process_snapshot *out) {
    start_probe *probe = (start_probe *)context;
    *out = (praktor_process_snapshot){probe->running, probe->running ? 42u : 0u, 1, 7, name};
    return (praktor_process_backend_result){true, 0, ""};
}
static praktor_process_backend_result start(void *context, const praktor_process_control_request *request, praktor_process_snapshot *out) {
    start_probe *probe = (start_probe *)context;
    probe->observed = request->operation == PRAKTOR_PROCESS_START && strcmp(request->image_name, "worker.exe") == 0 &&
        strcmp(request->executable, "worker.exe") == 0 && request->argument_count == 2 && strcmp(request->arguments[1], "second") == 0;
    probe->running = true;
    *out = (praktor_process_snapshot){true, 42, 1, 7, request->image_name};
    return (praktor_process_backend_result){true, 0, ""};
}
static praktor_process_backend_result command(void *context, const praktor_process_snapshot *snapshot) {
    (void)context; (void)snapshot; return (praktor_process_backend_result){true, 0, ""};
}
static void independent_params(void *argument) {
    bool *passed = (bool *)argument;
    unsigned index;
    *passed = true;
    for (index = 0; index < 50 && *passed; ++index) {
        praktor_yaml_document *doc = NULL;
        praktor_yaml_action_params *owner = NULL;
        praktor_yaml_service_view view;
        *passed = praktor_yaml_document_create(vstr_from_cstr("service: {name: worker, arguments: ['a', 'b']}\n"), &doc, NULL) == SALTS_OK &&
            praktor_yaml_action_params_create(praktor_yaml_root(doc), PRAKTOR_YAML_ACTION_SERVICE, &owner, NULL) == SALTS_OK &&
            praktor_yaml_action_service(owner, &view) == SALTS_OK && view.argument_count == 2;
        praktor_yaml_action_params_destroy(owner); praktor_yaml_document_destroy(doc);
    }
}

spec("C-owned typed YAML Action parameters") {
    it("retains download defaults and scalar compatibility") {
        praktor_yaml_action_params *owner = params("download: {url: true, path: 7}\n", PRAKTOR_YAML_ACTION_DOWNLOAD);
        praktor_yaml_download_view view;
        check(praktor_yaml_action_download(owner, &view) == SALTS_OK);
        check(is_text(view.url, "true") && is_text(view.path, "7") && !view.sha256.len && !view.overwrite && view.timeout_ms == 300000);
        praktor_yaml_action_params_destroy(owner);
        owner = params("download: {url: 'https://example.invalid/file', path: out, sha256: '', overwrite: 'YeS', timeout_ms: '7'}\n", PRAKTOR_YAML_ACTION_DOWNLOAD);
        check(praktor_yaml_action_download(owner, &view) == SALTS_OK && view.overwrite && view.timeout_ms == 7);
        praktor_yaml_action_params_destroy(owner);
    }
    it("owns service text and empty arguments beyond source lifetime and pointer transfer") {
        praktor_yaml_action_params *owner = params("service: {name: worker, arguments: ['', 'a b', \"x\\0y\"]}\n", PRAKTOR_YAML_ACTION_SERVICE), *moved = owner;
        praktor_yaml_service_view view;
        vstr text;
        owner = NULL;
        check(praktor_yaml_action_service(moved, &view) == SALTS_OK && is_text(view.name, "worker") && is_text(view.profile, "windows_scm"));
        check(view.operation == PRAKTOR_PROCESS_STATUS && view.timeout_ms == 30000 && view.poll_interval_ms == 200 && view.argument_count == 3);
        check(praktor_yaml_action_argument(moved, 0, &text) == SALTS_OK && !text.len && text.data);
        check(praktor_yaml_action_argument(moved, 2, &text) == SALTS_OK && is_text(text, "x"));
        praktor_yaml_action_params_destroy(owner); praktor_yaml_action_params_destroy(moved);
    }
    it("retains managed-process defaults and restart construction") {
        praktor_yaml_action_params *owner = params("managed_process: {identity: {image_name: worker.exe}}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS);
        praktor_yaml_managed_process_view view;
        check(praktor_yaml_action_managed_process(owner, &view) == SALTS_OK && view.operation == PRAKTOR_PROCESS_STATUS &&
            view.startup_timeout_ms == 5000 && view.stop_timeout_ms == 5000 && !view.force_terminate && !view.executable.len && !view.working_directory.len);
        praktor_yaml_action_params_destroy(owner);
        owner = params("managed_process: {identity: {image_name: worker.exe}, operation: restart, executable: worker.exe, working_directory: '.', startup_timeout_ms: 9, stop_timeout_ms: 8, force_terminate: '0X__1_'}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS);
        check(praktor_yaml_action_managed_process(owner, &view) == SALTS_OK && view.operation == PRAKTOR_PROCESS_RESTART &&
            view.startup_timeout_ms == 9 && view.stop_timeout_ms == 8 && view.force_terminate);
        praktor_yaml_action_params_destroy(owner);
    }
    it("consumes all system-action string corpus rows, including restart without an executable") { check(corpus(false) == 25); }
    it("consumes the full force-terminate corpus through the typed constructor") { check(corpus(true) == 34); }
    it("preserves validation order, case sensitivity and integer diagnostics") {
        error_is("download: {url: '', path: []}\n", PRAKTOR_YAML_ACTION_DOWNLOAD, "download.path must be a scalar");
        error_is("download: {url: x, path: y, extra: 1}\n", PRAKTOR_YAML_ACTION_DOWNLOAD, "Unknown key: 'extra'");
        error_is("download: {url: x}\n", PRAKTOR_YAML_ACTION_DOWNLOAD, "download requires 'path'");
        error_is("service: {name: worker, operation: START}\n", PRAKTOR_YAML_ACTION_SERVICE, "Unsupported service.operation value: 'START'");
        error_is("service: {name: worker, timeout_ms: +1}\n", PRAKTOR_YAML_ACTION_SERVICE, "Expected YAML integer at line 1");
        error_is("service: {name: worker, timeout_ms: 0}\n", PRAKTOR_YAML_ACTION_SERVICE, "service.timeout_ms must be positive");
        error_is("managed_process: {operation: start, arguments: [7]}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS, "managed_process requires 'identity'");
        error_is("managed_process: {identity: {image_name: w}, operation: start, arguments: [7]}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS, "managed_process.start requires 'executable'");
        error_is("managed_process: {identity: {image_name: w}, executable: '', working_directory: ''}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS, "managed_process.working_directory cannot be empty");
        error_is("other: x\n", PRAKTOR_YAML_ACTION_DOWNLOAD, "Parse error: download must be a map");
    }
    it("borrows decoded aliases while building an independent owner") {
        praktor_yaml_action_params *owner = params("base: &name worker\nargs: &args ['a', 'b']\nservice: {name: *name, arguments: *args}\n", PRAKTOR_YAML_ACTION_SERVICE);
        praktor_yaml_service_view view;
        check(praktor_yaml_action_service(owner, &view) == SALTS_OK && is_text(view.name, "worker") && view.argument_count == 2);
        praktor_yaml_action_params_destroy(owner);
    }
    it("enforces the argument count edge without partial owners") {
        size_t count, index;
        for (count = PRAKTOR_YAML_ACTION_MAX_ARGUMENTS; count <= PRAKTOR_YAML_ACTION_MAX_ARGUMENTS + 1; ++count) {
            tstr source = tstr_dup("service: {name: s, arguments: [");
            for (index = 0; index < count; ++index) { source = tstr_append_format(source, "{}", "'',"); check(source); }
            source = tstr_append_format(source, "{}", "]}\n"); check(source);
            if (count == PRAKTOR_YAML_ACTION_MAX_ARGUMENTS) {
                praktor_yaml_action_params *owner = params(source, PRAKTOR_YAML_ACTION_SERVICE);
                praktor_yaml_service_view view;
                check(praktor_yaml_action_service(owner, &view) == SALTS_OK && view.argument_count == count);
                praktor_yaml_action_params_destroy(owner);
            } else error_is(source, PRAKTOR_YAML_ACTION_SERVICE, "arguments exceed maximum count of 4096");
            tstr_free(source);
        }
    }
    it("enforces the retained-byte edge including terminators") {
        size_t extra, index, full = (PRAKTOR_YAML_ACTION_MAX_BYTES - 2u) / (PRAKTOR_YAML_MAX_PARAMETER_LENGTH + 1u);
        size_t tail = PRAKTOR_YAML_ACTION_MAX_BYTES - 2u - full * (PRAKTOR_YAML_MAX_PARAMETER_LENGTH + 1u) - 1u;
        tstr payload = tstr_new_len(NULL, PRAKTOR_YAML_MAX_PARAMETER_LENGTH);
        check(payload); memset(payload, 'x', tstr_len(payload));
        for (extra = 0; extra < 2; ++extra) {
            tstr source = tstr_dup("service: {name: s, arguments: [");
            for (index = 0; index < full; ++index) { source = tstr_append_format(source, "'{}',", payload); check(source); }
            source = tstr_append_format(source, "'{}']}}\n", vstr_from_buf(payload, tail + extra)); check(source);
            if (!extra) {
                praktor_yaml_action_params *owner = params(source, PRAKTOR_YAML_ACTION_SERVICE);
                praktor_yaml_action_params_destroy(owner);
            } else error_is(source, PRAKTOR_YAML_ACTION_SERVICE, "retained text exceeds maximum bytes of 1048576");
            tstr_free(source);
        }
        tstr_free(payload);
    }
    it("feeds the real C controller with typed parameters through an explicit test Host") {
        praktor_yaml_action_params *owner = params("managed_process: {identity: {image_name: worker.exe}, operation: start, executable: worker.exe, arguments: [first, second]}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS);
        praktor_yaml_managed_process_view view;
        vstr first, second;
        const char *args[2];
        start_probe probe = {0};
        praktor_process_control_host host = {&probe, query, start, command, command, NULL, NULL, 1};
        praktor_process_control_result result;
        praktor_process_control_request request;
        check(praktor_yaml_action_managed_process(owner, &view) == SALTS_OK);
        check(praktor_yaml_action_argument(owner, 0, &first) == SALTS_OK && praktor_yaml_action_argument(owner, 1, &second) == SALTS_OK);
        args[0] = first.data; args[1] = second.data;
        request = (praktor_process_control_request){view.operation, view.image_name.data, view.executable.data, args, 2,
            view.working_directory.data, view.startup_timeout_ms, view.stop_timeout_ms, view.force_terminate};
        check(praktor_process_control_execute(&request, &host, &result) == SALTS_OK && result.ok && result.changed && probe.observed);
        praktor_yaml_action_params_destroy(owner);
    }
    it("rejects invalid kinds, view projections and indices without changing outputs") {
        praktor_yaml_action_params *owner = params("download: {url: x, path: y}\n", PRAKTOR_YAML_ACTION_DOWNLOAD), *result = owner;
        praktor_yaml_service_view service = {0};
        praktor_yaml_field_error error = {0};
        vstr text = {"retained", 8};
        service.timeout_ms = 71;
        check(praktor_yaml_action_params_create((praktor_yaml_node){0}, (praktor_yaml_action_kind)7, &result, &error) == SALTS_EINVAL && result == owner);
        check(praktor_yaml_action_service(owner, &service) == SALTS_EINVAL && service.timeout_ms == 71);
        check(praktor_yaml_action_argument(owner, SIZE_MAX, &text) == SALTS_ENOENT && is_text(text, "retained"));
        check(praktor_yaml_action_argument(NULL, 0, &text) == SALTS_EINVAL && is_text(text, "retained"));
        praktor_yaml_field_error_dispose(&error); praktor_yaml_action_params_destroy(owner);
    }
    it("isolates concurrent independent parameter owners") {
        cmeta_thread_t threads[4] = {NULL};
        bool passed[4] = {false}, joined = true;
        size_t created = 0, index;
        for (; created < 4; ++created) if (cmeta_thread_create(&threads[created], independent_params, &passed[created]) != SALTS_OK) break;
        for (index = 0; index < created; ++index) if (cmeta_thread_join(&threads[index]) != SALTS_OK) joined = false;
        check(joined && created == 4);
        for (index = 0; index < 4; ++index) check(passed[index] && !threads[index]);
    }
}
