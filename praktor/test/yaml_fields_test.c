#include "yml/yaml_fields.h"
#include <cmeta_fs.h>
#include <fmt.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static praktor_yaml_document *document(vstr source) {
    praktor_yaml_document *owner = NULL;
    check(praktor_yaml_document_create(source, &owner, NULL) == SALTS_OK);
    return owner;
}
static praktor_yaml_document *scalar_document(const char *scalar) {
    tstr source = tstr_format("value: {}\n", scalar);
    praktor_yaml_document *owner;
    check(source);
    owner = document(tstr_to_v(source));
    tstr_free(source);
    return owner;
}
static praktor_yaml_node value_node(praktor_yaml_document *owner) {
    return praktor_yaml_at(praktor_yaml_root(owner), 0);
}
static void boolean_case(const char *scalar, int strict, int force) {
    praktor_yaml_document *owner = scalar_document(scalar);
    praktor_yaml_field_error error = {0};
    unsigned policy;
    for (policy = 0; policy < 2; ++policy) {
        int expected = policy ? force : strict;
        bool value = expected < 0 ? true : expected == 0;
        bool previous = value;
        int status = praktor_yaml_field_boolean(value_node(owner), vstr_from_cstr("flag"),
            (praktor_yaml_boolean_policy)policy, &value, &error);
        info("scalar %s, policy %u, status %d", scalar, policy, status);
        if (expected < 0) {
            check(status == SALTS_EINVAL && value == previous);
            check(error.origin == PRAKTOR_YAML_FAILURE_INPUT && error.text);
            check(strcmp(error.text, "Parse error at line 1, column 1: 'flag' must be a boolean") == 0);
        } else check(status == SALTS_OK && value == (expected == 1) && !error.text);
    }
    praktor_yaml_field_error_dispose(&error);
    praktor_yaml_document_destroy(owner);
}

static size_t corpus(const char *name, bool boolean) {
    tstr path = tstr_format("{}/{}", PRAKTOR_YAML_FIELD_CORPUS_DIR, name);
    cmeta_fs_buf_t file = {0};
    vstr rest;
    size_t count = 0;
    check(path && cmeta_fs_read_file(path, &file) == SALTS_OK);
    tstr_free(path);
    rest = vstr_from_buf(file.base, file.len);
    while (rest.len) {
        vstr line = vstr_trim_right(vstr_split_next(&rest, vstr_from_cstr("\n")), "\r");
        vstr fields[7];
        size_t index, columns = boolean ? 5u : 7u;
        praktor_yaml_document *owner;
        praktor_yaml_field_error error = {0};
        tstr source;
        bool flag = false;
        tstr text = NULL;
        int status;
        if (!line.len || line.data[0] == '#') continue;
        for (index = 0; index < columns; ++index) fields[index] = vstr_split_next(&line, vstr_from_cstr("\t"));
        check(!line.len && fields[columns - 1].len);
        /* Missing executable is a task-level rule, still covered by TaskParser. */
        if (!boolean && vstr_eq(fields[2], vstr_from_cstr("restart_without_executable"))) continue;
        source = tstr_format("value: {}\n", fields[boolean ? 1 : 3]);
        check(source);
        owner = document(tstr_to_v(source));
        tstr_free(source);
        status = boolean ? praktor_yaml_field_boolean(value_node(owner), vstr_from_cstr("force_terminate"),
            PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE, &flag, &error) :
            praktor_yaml_field_string(value_node(owner), vstr_from_cstr("must be a string"), &text, &error);
        if (vstr_eq(fields[boolean ? 3 : 4], vstr_from_cstr("pass"))) {
            check(status == SALTS_OK && !error.text);
            if (boolean) check(flag == (vstr_eq(fields[4], vstr_from_cstr("true")) != 0));
            else check(text && vstr_eq(tstr_to_v(text), fields[5]));
        } else check(status == SALTS_EINVAL && error.origin == PRAKTOR_YAML_FAILURE_INPUT && !text);
        tstr_free(text);
        praktor_yaml_field_error_dispose(&error);
        praktor_yaml_document_destroy(owner);
        ++count;
    }
    cmeta_fs_buf_free(&file);
    return count;
}

static void independent_reader(void *argument) {
    bool *passed = (bool *)argument;
    unsigned index;
    *passed = true;
    for (index = 0; index < 100 && *passed; ++index) {
        praktor_yaml_document *owner = NULL;
        praktor_yaml_field_error error = {0};
        bool result = false;
        *passed = praktor_yaml_document_create(vstr_from_cstr("flag: '0x_1'\n"), &owner, NULL) == SALTS_OK &&
            praktor_yaml_field_boolean(value_node(owner), vstr_from_cstr("force_terminate"),
                PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE, &result, &error) == SALTS_OK && result && !error.text;
        praktor_yaml_field_error_dispose(&error);
        praktor_yaml_document_destroy(owner);
    }
}

spec("C YAML field grammar without a C++ parser") {
    it("preserves separate strict and force-terminate boolean grammars") {
        const struct { const char *scalar; int strict, force; } cases[] = {
            {"true", 1, 1}, {"'TrUe'", 1, 1}, {"false", 0, 0}, {"'FaLsE'", 0, 0},
            {"YES", 1, 1}, {"nO", 0, 0}, {"1", 1, 1}, {"'0'", 0, 0},
            {"ON", -1, 1}, {"'oFf'", -1, 0}, {"+1", -1, 1}, {"-0", -1, 0},
            {"'01'", -1, 1}, {"0x1", -1, 1}, {"'0X_1_'", -1, 1},
            {"'0o__0_'", -1, 0}, {"'0b__1'", -1, 1}, {"'0_1_'", -1, 1},
            {"-1", -1, -1}, {"2", -1, -1}, {"1.0", -1, -1}, {"null", -1, -1},
            {"''", -1, -1}, {"' true'", -1, -1}, {"'yes '", -1, -1},
            {"[]", -1, -1}, {"{}", -1, -1}, {"'0x_'", -1, -1},
            {"'_1'", -1, -1}, {"'+_1'", -1, -1}, {"'0b2'", -1, -1},
            {"'18446744073709551616'", -1, -1}, {"\"true\\0false\"", 1, 1}
        };
        size_t index;
        for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
            boolean_case(cases[index].scalar, cases[index].strict, cases[index].force);
    }
    it("uses the existing force-terminate corpus through the real YAML provider") {
        check(corpus("force_terminate_corpus.tsv", true) == 34);
    }
    it("uses the existing system-action string corpus through the real YAML provider") {
        check(corpus("system_action_string_corpus.tsv", false) == 24);
    }
    it("keeps scalar conversion distinct from string-only action arguments") {
        const char *scalars[] = {"true", "7", "1.5", "null", "'true'", "'7'", "'1.5'", "'null'"};
        size_t index;
        for (index = 0; index < sizeof(scalars) / sizeof(scalars[0]); ++index) {
            praktor_yaml_document *owner = scalar_document(scalars[index]);
            tstr text = NULL;
            check(praktor_yaml_field_scalar(value_node(owner), vstr_from_cstr("scalar"), &text, NULL) ==
                (index == 3 ? SALTS_EINVAL : SALTS_OK));
            tstr_free(text); text = NULL;
            check(praktor_yaml_field_string(value_node(owner), vstr_from_cstr("string"), &text, NULL) ==
                (index < 4 ? SALTS_EINVAL : SALTS_OK));
            tstr_free(text);
            praktor_yaml_document_destroy(owner);
        }
    }
    it("enforces the decoded 10 KiB boundary and leaves prior output owners untouched") {
        size_t extra;
        for (extra = 0; extra < 2; ++extra) {
            tstr value = tstr_new_len(NULL, PRAKTOR_YAML_MAX_PARAMETER_LENGTH + extra), text = tstr_dup("previous");
            tstr previous = text, source;
            praktor_yaml_document *owner;
            praktor_yaml_field_error error = {0};
            int status;
            check(value && text);
            memset(value, 'a', tstr_len(value));
            source = tstr_format("value: '{}'\n", value);
            check(source);
            owner = document(tstr_to_v(source));
            status = praktor_yaml_field_scalar(value_node(owner), vstr_from_cstr("scalar"), &text, &error);
            if (extra) {
                check(status == SALTS_EINVAL && text == previous);
                check(strcmp(error.text, "Parse error at line 1, column 1: Parameter value exceeds maximum length of 10240 bytes") == 0);
            } else {
                check(status == SALTS_OK && tstr_len(text) == PRAKTOR_YAML_MAX_PARAMETER_LENGTH);
                tstr_free(text);
            }
            tstr_free(previous); tstr_free(source); tstr_free(value);
            praktor_yaml_field_error_dispose(&error);
            praktor_yaml_document_destroy(owner);
        }
    }
    it("retains complete long binary diagnostics and clears reusable error owners") {
        praktor_yaml_document *owner = scalar_document("[]");
        praktor_yaml_field_error error = {0};
        tstr message = tstr_new_len(NULL, 13000), expected, result = NULL;
        check(message);
        memset(message, 'x', tstr_len(message)); message[5] = '\0';
        expected = tstr_format("Parse error at line 1, column 1: {}", tstr_to_v(message));
        check(expected);
        check(praktor_yaml_field_string(value_node(owner), tstr_to_v(message), &result, &error) == SALTS_EINVAL);
        check(!result && error.text && tstr_len(error.text) == tstr_len(expected));
        check(memcmp(error.text, expected, tstr_len(expected)) == 0);
        check(praktor_yaml_field_boolean(value_node(owner), tstr_to_v(message),
            PRAKTOR_YAML_BOOLEAN_STRICT, &(bool){false}, &error) == SALTS_EINVAL);
        check(tstr_len(error.text) == 13000 + strlen("Parse error at line 1, column 1: '' must be a boolean"));
        praktor_yaml_document_destroy(owner);
        owner = scalar_document("'valid'");
        check(praktor_yaml_field_scalar(value_node(owner), vstr_from_cstr("scalar"), &result, &error) == SALTS_OK);
        check(!error.text && error.origin == PRAKTOR_YAML_FAILURE_NONE && strcmp(result, "valid") == 0);
        praktor_yaml_document_destroy(owner);
        check(strcmp(result, "valid") == 0);
        tstr_free(result); tstr_free(expected); tstr_free(message);
        praktor_yaml_field_error_dispose(&error);
        praktor_yaml_field_error_dispose(&error);
    }
    it("retains alias decoding and mapping-key diagnostic locations") {
        praktor_yaml_document *owner = document(vstr_from_cstr("base: &flag 'on'\ncopy: *flag\n"));
        praktor_yaml_field_error error = {0};
        bool value = false;
        praktor_yaml_node copy = praktor_yaml_at(praktor_yaml_root(owner), 1);
        check(praktor_yaml_field_boolean(copy, vstr_from_cstr("silent"), PRAKTOR_YAML_BOOLEAN_STRICT, &value, &error) == SALTS_EINVAL);
        check(strcmp(error.text, "Parse error at line 2, column 1: 'silent' must be a boolean") == 0 && !value);
        check(praktor_yaml_field_boolean(copy, vstr_from_cstr("force_terminate"),
            PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE, &value, &error) == SALTS_OK && value);
        praktor_yaml_field_error_dispose(&error);
        praktor_yaml_document_destroy(owner);
    }
    it("checks pure compatibility views and leaves unsupported outputs unchanged") {
        const char *invalid[] = {"", "+", "-", "0x", "0X1", "0b_", "0o8", "1__2", "1 ", "-1", "0xffffffffffffffff", "0x10000000000000000"};
        const char *valid[] = {"0", "-0", "+0", "0x__0_", "1", "+1", "0b_1_", "0o1", "0_1_"};
        size_t index;
        bool value = false;
        for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            check(praktor_yaml_force_terminate_integer(vstr_from_cstr(invalid[index]), &value) == SALTS_ENOENT);
            check(!value);
        }
        for (index = 0; index < sizeof(valid) / sizeof(valid[0]); ++index)
            check(praktor_yaml_force_terminate_integer(vstr_from_cstr(valid[index]), &value) == SALTS_OK && value == (index >= 4));
        value = false;
        check(praktor_yaml_force_terminate_integer(vstr_from_buf("1garbage", 1), &value) == SALTS_OK && value);
        check(praktor_yaml_force_terminate_integer(vstr_from_buf("1\0", 2), &value) == SALTS_ENOENT && value);
        check(praktor_yaml_force_terminate_integer((vstr){NULL, 1}, &value) == SALTS_EINVAL && value);
    }
    it("rejects invalid arguments without publishing text or booleans") {
        praktor_yaml_node empty = {0};
        praktor_yaml_field_error error = {0};
        tstr text = NULL;
        bool value = true;
        check(praktor_yaml_field_scalar(empty, (vstr){NULL, 1}, &text, &error) == SALTS_EINVAL && !text);
        check(praktor_yaml_field_string(empty, vstr_from_cstr("string"), NULL, &error) == SALTS_EINVAL);
        check(praktor_yaml_field_boolean(empty, vstr_from_cstr("flag"), (praktor_yaml_boolean_policy)7, &value, &error) == SALTS_EINVAL && value);
        check(praktor_yaml_field_boolean(empty, vstr_from_cstr("flag"), PRAKTOR_YAML_BOOLEAN_STRICT, NULL, NULL) == SALTS_EINVAL);
        check(praktor_yaml_field_scalar(empty, vstr_from_cstr("missing"), &text, &error) == SALTS_EINVAL);
        check(strcmp(error.text, "Parse error: missing") == 0 && !text);
        praktor_yaml_field_error_dispose(&error);
    }
    it("isolates concurrent field readers and diagnostic owners") {
        cmeta_thread_t threads[4] = {NULL};
        bool passed[4] = {false}, joined = true;
        size_t created = 0, index;
        for (; created < 4; ++created)
            if (cmeta_thread_create(&threads[created], independent_reader, &passed[created]) != SALTS_OK) break;
        for (index = 0; index < created; ++index)
            if (cmeta_thread_join(&threads[index]) != SALTS_OK) joined = false;
        check(joined && created == 4);
        for (index = 0; index < 4; ++index) check(passed[index] && !threads[index]);
    }
}
