#include "providers/wasm.h"
#include <cmeta/data_reflect.h>
#include <core/clock.h>
#include <core/fmt.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_INPUTS = 64, MAX_INPUT_BYTES = 65536, MAX_OUTPUT_BYTES = 4096 };

typedef struct wasm_job {
    const praktor_wasm_service *service;
    void *engine_job;
    uint64_t started;
    char *input;
    praktor_execution_result result;
    praktor_execution_output port;
    char output[MAX_OUTPUT_BYTES + 1u];
} wasm_job;

int praktor_wasm_validate_inputs(const praktor_execution_admission *request) {
    if (!request) return SALTS_EINVAL;
    const json_value_t *inputs = request->inputs;
    if ((inputs && json_type(inputs) != JSON_OBJECT) || json_object_size(inputs) > MAX_INPUTS) {
        if (request->error && request->error_capacity)
            fmt_text(request->error, request->error_capacity, "WASM accepts a map of up to 64 inputs");
        return SALTS_EINVAL;
    }
    for (size_t i = 0; i < json_object_size(inputs); ++i) {
        const char *key = json_object_key(inputs, i);
        const json_value_t *value = json_object_value(inputs, i);
        const char *text = json_string(value);
        if (!key || !*key || strlen(key) != json_object_key_len(inputs, i) ||
            !text || strlen(text) != json_string_len(value)) {
            if (request->error && request->error_capacity)
                fmt_text(request->error, request->error_capacity, "WASM inputs must be NUL-free text");
            return SALTS_EINVAL;
        }
    }
    return SALTS_OK;
}

static int admit(void *self, const praktor_execution_admission *request) {
    const praktor_wasm_service *service = self;
    return service && service->binding ? praktor_wasm_validate_inputs(request) : SALTS_EINVAL;
}

static wasm_job *owned_job(void *self, praktor_service_job *handle) {
    wasm_job *job = handle ? handle->impl : NULL;
    return job && job->service == self ? job : NULL;
}

static praktor_runtime_status check_job(void *opaque) {
    const wasm_job *job = opaque;
    const praktor_wasm_service *service = job->service;
    if (service->cancel && service->cancel(service->cancel_context))
        return PRAKTOR_RUNTIME_CANCELLED;
    const uint64_t now = hrtime();
    if (now >= service->workflow_deadline || now - job->started >= service->timeout_ns)
        return PRAKTOR_RUNTIME_DEADLINE;
    return PRAKTOR_RUNTIME_OK;
}

static void finish(wasm_job *job, praktor_runtime_status status, const char *error) {
    if (job->engine_job) {
        const praktor_wasm_host *host = job->service->host;
        host->release(host->context, job->engine_job);
        job->engine_job = NULL;
    }
    job->result.done = true;
    job->result.success = status == PRAKTOR_RUNTIME_OK;
    job->result.cancelled = status == PRAKTOR_RUNTIME_CANCELLED;
    job->result.terminal_status = status;
    switch (status) {
    case PRAKTOR_RUNTIME_CANCELLED: job->result.error = "WASM invocation cancelled"; break;
    case PRAKTOR_RUNTIME_DEADLINE: job->result.error = "WASM deadline exceeded"; break;
    case PRAKTOR_RUNTIME_RESOURCE: job->result.error = "WASM invocation exhausted a resource limit"; break;
    default: job->result.error = error; break;
    }
}

static int encode_inputs(const praktor_execution_request *request, char **out, size_t *size) {
    json_value_t *object = json_create_object();
    if (!object) return SALTS_ENOMEM;
    int rc = SALTS_OK;
    size_t remaining = MAX_INPUT_BYTES;
    for (size_t i = 0; i < json_object_size(request->inputs); ++i) {
        const char *key = json_object_key(request->inputs, i);
        const char *value = json_get_string(request->inputs, key);
        size_t key_length = strlen(key);
        if (key_length > remaining) { rc = SALTS_ENOBUFS; break; }
        remaining -= key_length;
        tstr resolved = NULL;
        rc = request->resolve_text(request->user, value, remaining, &resolved);
        if (rc != SALTS_OK) break;
        size_t length = tstr_len(resolved);
        if (length > remaining) {
            tstr_free(resolved);
            rc = SALTS_ENOBUFS;
            break;
        }
        remaining -= length;
        json_value_t *field = json_create_string_n(resolved, length);
        tstr_free(resolved);
        if (!field || !json_object_add_n(object, key, key_length, field)) {
            json_free(field);
            rc = SALTS_ENOMEM;
            break;
        }
    }
    if (rc == SALTS_OK) {
        *out = json_serialize(object, size);
        if (!*out) rc = SALTS_ENOMEM;
        else if (*size > MAX_INPUT_BYTES) rc = SALTS_ENOBUFS;
        else if (!vstr_utf8_valid(vstr_from_buf(*out, *size))) rc = SALTS_EINVAL;
    }
    json_free(object);
    return rc;
}

static int start(void *self, const praktor_execution_request *request, praktor_service_job *out) {
    praktor_wasm_service *service = self;
    if (!service || !service->binding || !request || !request->resolve_text || !out || out->impl)
        return SALTS_EINVAL;
    wasm_job *job = calloc(1u, sizeof(*job));
    if (!job) return SALTS_ENOMEM;
    job->service = service;
    job->started = hrtime();
    ++service->jobs;
    out->impl = job;
    size_t size = 0;
    const int encoded = encode_inputs(request, &job->input, &size);
    if (encoded != SALTS_OK) {
        finish(job, encoded == SALTS_ENOMEM || encoded == SALTS_ENOBUFS ?
            PRAKTOR_RUNTIME_RESOURCE : PRAKTOR_RUNTIME_INVALID, "cannot resolve WASM input");
        return SALTS_OK;
    }
    praktor_runtime_status status = check_job(job);
    if (status == PRAKTOR_RUNTIME_OK) {
        const praktor_wasm_host *host = service->host;
        void *engine_job = NULL;
        status = host->start(host->context, service->binding, (unsigned int)service->fuel,
                             job->input, size, &engine_job);
        if (status == PRAKTOR_RUNTIME_OK) {
            if (engine_job) job->engine_job = engine_job;
            else status = PRAKTOR_RUNTIME_INVALID;
        }
    }
    if (status != PRAKTOR_RUNTIME_OK) finish(job, status, "cannot start WASM invocation");
    return SALTS_OK;
}

static int poll(void *self, praktor_service_job *handle, praktor_execution_result *out) {
    const praktor_wasm_service *service = self;
    wasm_job *job = owned_job(self, handle);
    if (!job || !out) return SALTS_EINVAL;
    if (job->result.done) {
        *out = job->result;
        return SALTS_OK;
    }
    size_t size = 0;
    int done = 0;
    praktor_runtime_status status = check_job(job);
    if (status == PRAKTOR_RUNTIME_OK) {
        const praktor_wasm_host *host = service->host;
        status = host->step(host->context, job->engine_job, check_job, job,
                           job->output, MAX_OUTPUT_BYTES, &size, &done);
        if (status == PRAKTOR_RUNTIME_OK) status = check_job(job);
    }
    if (status != PRAKTOR_RUNTIME_OK) {
        finish(job, status, "WASM guest trapped or execution failed");
    } else if (done) {
        json_value_t *document = NULL;
        if (size && size <= MAX_OUTPUT_BYTES && !memchr(job->output, 0, size) &&
            vstr_utf8_valid(vstr_from_buf(job->output, size)))
            document = json_parse(job->output, size);
        if (!document) {
            finish(job, PRAKTOR_RUNTIME_INVALID, "WASM output must be one JSON document within 4096 bytes");
        } else {
            json_free(document);
            job->output[size] = '\0';
            job->port = (praktor_execution_output){service->output_key, job->output};
            job->result.outputs = &job->port;
            job->result.output_count = 1;
            finish(job, PRAKTOR_RUNTIME_OK, NULL);
        }
    }
    *out = job->result;
    return SALTS_OK;
}

static int cancel(void *self, praktor_service_job *handle) {
    wasm_job *job = owned_job(self, handle);
    if (!job) return SALTS_EINVAL;
    if (!job->result.done) finish(job, PRAKTOR_RUNTIME_CANCELLED, NULL);
    return SALTS_OK;
}

static int release(void *self, praktor_service_job *handle) {
    praktor_wasm_service *service = self;
    wasm_job *job = owned_job(self, handle);
    if (!job) return SALTS_EINVAL;
    if (!job->result.done) return SALTS_EBUSY;
    json_serialize_free(job->input);
    free(job);
    handle->impl = NULL;
    --service->jobs;
    return SALTS_OK;
}

CMETA_IMPLEMENTS(praktor_execution_service, wasm_service_impl, 0u,
    .admit=admit, .start=start, .poll=poll, .cancel=cancel, .release=release);

praktor_execution_service praktor_wasm_service_bind(praktor_wasm_service *service) {
    return praktor_execution_service_bind(service, &wasm_service_impl_vtable);
}

cmeta_reflect_data(praktor_wasm_service, "praktor.wasm.service",
    cmeta_field(int, fuel));

const cmeta_data_desc *praktor_wasm_service_reflection(void) {
    return cmeta_reflected_data(praktor_wasm_service);
}
