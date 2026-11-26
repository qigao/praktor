#include "turbowasm_host.h"
#include <cyaml_json_adapter.h>
#include <gmssl/sha2.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_POLICY_BYTES = 64 * 1024, MAX_PATH_BYTES = 4096 };

static unsigned char *read_bounded(const char *path, size_t limit, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    unsigned char *bytes = malloc(limit + 1);
    if (bytes) *length = fread(bytes, 1, limit + 1, file);
    int read_error = ferror(file);
    if (fclose(file) || read_error || !*length || *length > limit) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

static bool read_string(const cyaml_doc_t *doc, cyaml_node_t *node, char *out, size_t capacity) {
    if (!cyaml_is_scalar(node)) return false;
    json_value_t *value = json_value_from_cyaml_node(doc, node);
    const char *text = json_string(value);
    size_t size = json_string_len(value);
    bool valid = text && size && size < capacity && !memchr(text, 0, size);
    if (valid) memcpy(out, text, size + 1);
    json_free(value);
    return valid;
}

static bool read_limit(const cyaml_doc_t *doc, cyaml_node_t *node, unsigned int *out) {
    int64_t value;
    if (!cyaml_is_scalar(node) || cyaml_scalar_kind(doc, node) != CYAML_KIND_INT ||
        !cyaml_as_int(doc, node, &value) || value < 1 || value > INT_MAX)
        return false;
    *out = (unsigned int)value;
    return true;
}

static bool valid_name(const char *name) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-";
    return name[strspn(name, chars)] == '\0';
}

static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool decode_digest(const char *text, uint8_t digest[SHA256_DIGEST_SIZE]) {
    if (strlen(text) != 2 * SHA256_DIGEST_SIZE) return false;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        int high = hex_digit(text[2 * i]);
        int low = hex_digit(text[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        digest[i] = (uint8_t)(16 * high + low);
    }
    return true;
}

static bool load_module(const cyaml_doc_t *doc, cyaml_node_t *node, approved_wasm_modules *approved,
    const char **error) {
    approved_wasm module = {0};
    char path[MAX_PATH_BYTES + 1], sha256[2 * SHA256_DIGEST_SIZE + 1];
    uint8_t expected[SHA256_DIGEST_SIZE], actual[SHA256_DIGEST_SIZE];
    if (!cyaml_is_map(node) || cyaml_map_len(node) != 6 ||
        !read_string(doc, cyaml_get(doc, node, "module"), module.module, sizeof module.module) ||
        !valid_name(module.module) ||
        !read_string(doc, cyaml_get(doc, node, "export"), module.entry, sizeof module.entry) ||
        !valid_name(module.entry) ||
        !read_string(doc, cyaml_get(doc, node, "path"), path, sizeof path) ||
        !read_string(doc, cyaml_get(doc, node, "sha256"), sha256, sizeof sha256) ||
        !decode_digest(sha256, expected) ||
        !read_limit(doc, cyaml_get(doc, node, "max_fuel"), &module.max_fuel) ||
        !read_limit(doc, cyaml_get(doc, node, "max_timeout_ms"), &module.max_timeout_ms)) {
        *error = "expected module, export, path, SHA-256 hex digest and positive max_fuel and max_timeout_ms";
        return false;
    }
    for (size_t i = 0; i < approved->count; ++i) {
        if (strcmp(approved->modules[i].module, module.module) == 0) {
            *error = "duplicate logical module";
            return false;
        }
    }
    module.bytes = read_bounded(path, MAX_MODULE_BYTES, &module.size);
    if (!module.bytes) {
        *error = "cannot read module within the 256 KiB limit";
        return false;
    }
    SHA256_CTX sha;
    sha256_init(&sha);
    sha256_update(&sha, module.bytes, module.size);
    sha256_finish(&sha, actual);
    if (memcmp(actual, expected, sizeof expected) != 0) {
        free(module.bytes);
        *error = "module SHA-256 mismatch";
        return false;
    }
    approved->modules[approved->count++] = module;
    return true;
}

static bool load_policy(const char *path, approved_wasm_modules *approved) {
    size_t size = 0;
    unsigned char *text = read_bounded(path, MAX_POLICY_BYTES, &size);
    if (!text || memchr(text, 0, size)) {
        fprintf(stderr, "Sandbox policy '%s': expected a YAML file within 64 KiB\n", path);
        free(text);
        return false;
    }
    cyaml_error_t parse_error = {0};
    cyaml_opts_t options = CYAML_OPTS_DEFAULT;
    options.max_depth = 8;
    cyaml_stream_t *stream = cyaml_parse_stream((const char *)text, size, &options, &parse_error);
    const cyaml_doc_t *doc = cyaml_stream_doc(stream, 0);
    cyaml_node_t *root = cyaml_root(doc);
    const char *error = NULL;
    size_t index = 0;
    if (!stream) {
        error = parse_error.msg[0] ? parse_error.msg : "cannot parse YAML";
    } else if (cyaml_stream_count(stream) != 1 || !cyaml_is_map(root) || cyaml_map_len(root) != 1) {
        error = "expected one YAML document with a modules key";
    } else {
        cyaml_node_t *modules = cyaml_get(doc, root, "modules");
        size_t count = cyaml_seq_len(modules);
        if (!cyaml_is_seq(modules) || !count || count > MAX_APPROVED_MODULES) {
            error = "expected a list of 1 to 16 modules";
        } else {
            for (; index < count; ++index) {
                if (!load_module(doc, cyaml_seq_get(modules, (uint32_t)index), approved, &error))
                    break;
            }
        }
    }
    if (error)
        fprintf(stderr, "Sandbox policy '%s', module %zu: %s\n", path, index + 1, error);
    if (stream) cyaml_stream_free(stream);
    free(text);
    return !error;
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s SANDBOX_POLICY.yml WORKFLOW.yml [TASK]\n", argv[0]);
        return EXIT_FAILURE;
    }
    approved_wasm_modules approved = {0};
    size_t yaml_size = 0;
    unsigned char *yaml = NULL;
    bool authorized = load_policy(argv[1], &approved);
    int exit_code = EXIT_FAILURE;
    if (authorized) {
        yaml = read_bounded(argv[2], 1024 * 1024, &yaml_size);
        if (!yaml) fprintf(stderr, "Cannot read workflow '%s' within the 1 MiB limit\n", argv[2]);
    }
    if (yaml) {
        const praktor_wasm_host host = make_turbowasm_host(&approved);
        const praktor_execute_request request = {
            .yaml = (const char *)yaml, .yaml_size = yaml_size, .limits = praktor_default_limits()
        };
        praktor_runtime_result result = {0};
        praktor_runtime_status status = praktor_execute_with_wasm(
            &request, argc == 4 ? argv[3] : NULL, &host, &result);
        if (result.json) puts(result.json);
        if (status != PRAKTOR_RUNTIME_OK)
            fprintf(stderr, "Praktor status %d: %s\n", status, result.error);
        else
            exit_code = EXIT_SUCCESS;
        praktor_release_result(&result);
    }
    free(yaml);
    for (size_t i = 0; i < approved.count; ++i) free(approved.modules[i].bytes);
    return exit_code;
}
