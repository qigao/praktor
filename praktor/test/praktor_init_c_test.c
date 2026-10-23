#include "util/praktor_init.h"

#include <cmeta_fs.h>
#include <stdio.h>
#include <string.h>
#include <tinytest.h>

#ifdef _WIN32
#include <process.h>
#define test_getpid _getpid
#else
#include <unistd.h>
#define test_getpid getpid
#endif

typedef struct init_fixture {
    char root[1024];
    char executable[1024];
    char executable_templates[1024];
    char application[1024];
    char application_templates[1024];
    char working[1024];
    char working_templates[1024];
    char destination[1024];
} init_fixture;

static int join_path(char *out, size_t capacity, const char *base, const char *child) {
    return cmeta_fs_path_join(out, capacity, base, child);
}

static int write_text(const char *path, const char *text) {
    cmeta_fs_buf_t content = cmeta_fs_buf_init((char *)text, strlen(text));
    return cmeta_fs_write_file(path, &content);
}

static int read_is(const char *path, const char *expected) {
    cmeta_fs_buf_t content = {0};
    int equal = 0;
    if (cmeta_fs_read_file(path, &content) == 0)
        equal = content.len == strlen(expected) && memcmp(content.base, expected, content.len) == 0;
    cmeta_fs_buf_free(&content);
    return equal;
}

static void remove_file(const char *path) {
    if (cmeta_fs_access(path, SALTS_FS_ACCESS_EXISTS) == 0) (void)cmeta_fs_unlink(path);
}

static void fixture_destroy(init_fixture *fixture) {
    char path[1024];
    const char *names[] = {"basic.yml", "alpha.yml", "zeta.yml", "broken.yml"};
    size_t i;
    remove_file(fixture->destination);
    for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (join_path(path, sizeof(path), fixture->executable_templates, names[i]) == 0) remove_file(path);
        if (join_path(path, sizeof(path), fixture->application_templates, names[i]) == 0) remove_file(path);
        if (join_path(path, sizeof(path), fixture->working_templates, names[i]) == 0) remove_file(path);
    }
    (void)cmeta_fs_rmdir(fixture->executable_templates);
    (void)cmeta_fs_rmdir(fixture->working_templates);
    (void)cmeta_fs_rmdir(fixture->application_templates);
    (void)cmeta_fs_rmdir(fixture->executable);
    (void)cmeta_fs_rmdir(fixture->working);
    (void)cmeta_fs_rmdir(fixture->application);
    (void)cmeta_fs_rmdir(fixture->root);
}

static int fixture_init(init_fixture *fixture) {
    char temporary[512];
    unsigned attempt;
    memset(fixture, 0, sizeof(*fixture));
    if (cmeta_fs_get_tmpdir(temporary, sizeof(temporary)) != 0) return 0;
    for (attempt = 0u; attempt < 100u; ++attempt) {
        char name[128];
        (void)snprintf(name, sizeof(name), "praktor-init-%ld-%u", (long)test_getpid(), attempt);
        if (join_path(fixture->root, sizeof(fixture->root), temporary, name) != 0) return 0;
        if (cmeta_fs_mkdir(fixture->root, 0700) == 0) break;
    }
    if (attempt == 100u) return 0;
    if (join_path(fixture->executable, sizeof(fixture->executable), fixture->root, "bin") != 0 ||
        join_path(fixture->executable_templates, sizeof(fixture->executable_templates), fixture->executable, "templates") != 0 ||
        join_path(fixture->application, sizeof(fixture->application), fixture->root, "praktor") != 0 ||
        join_path(fixture->application_templates, sizeof(fixture->application_templates), fixture->application, "templates") != 0 ||
        join_path(fixture->working, sizeof(fixture->working), fixture->root, "work") != 0 ||
        join_path(fixture->working_templates, sizeof(fixture->working_templates), fixture->working, "templates") != 0 ||
        join_path(fixture->destination, sizeof(fixture->destination), fixture->working, "praktor.yml") != 0)
        goto failed;
    if (cmeta_fs_mkdir(fixture->executable, 0700) != 0 ||
        cmeta_fs_mkdir(fixture->executable_templates, 0700) != 0 ||
        cmeta_fs_mkdir(fixture->application, 0700) != 0 ||
        cmeta_fs_mkdir(fixture->application_templates, 0700) != 0 ||
        cmeta_fs_mkdir(fixture->working, 0700) != 0 ||
        cmeta_fs_mkdir(fixture->working_templates, 0700) != 0)
        goto failed;
    return 1;
failed:
    fixture_destroy(fixture);
    return 0;
}

static praktor_init_status run_init(const init_fixture *fixture, const char *name,
                                    praktor_init_result *result) {
    praktor_init_options options;
    praktor_init_options_init(&options);
    options.template_name = name;
    options.executable_directory = fixture->executable;
    options.working_directory = fixture->working;
    return praktor_init_project(&options, result);
}

spec("C praktor init") {
    it("uses the legacy search order and owns result messages") {
        init_fixture fixture;
        char executable_template[1024], application_template[1024], working_template[1024];
        praktor_init_result result = {0};
        check(fixture_init(&fixture));
        check(join_path(executable_template, sizeof(executable_template),
                        fixture.executable_templates, "basic.yml") == 0);
        check(join_path(working_template, sizeof(working_template),
                        fixture.working_templates, "basic.yml") == 0);
        check(join_path(application_template, sizeof(application_template),
                        fixture.application_templates, "basic.yml") == 0);
        check(write_text(executable_template, "first\n") == 0);
        check(write_text(working_template, "third\n") == 0);
        check(run_init(&fixture, "basic", &result) == PRAKTOR_INIT_OK);
        check(read_is(fixture.destination, "first\n"));
        check(strcmp(result.message,
                     "Successfully initialized praktor.yml using 'basic' template.") == 0);
        check(strstr(result.detail, fixture.executable_templates) != NULL);
        praktor_init_result_destroy(&result);

        remove_file(fixture.destination);
        remove_file(executable_template);
        check(write_text(application_template, "second\n") == 0);
        check(run_init(&fixture, "basic", &result) == PRAKTOR_INIT_OK);
        check(read_is(fixture.destination, "second\n"));
        check(strstr(result.detail, fixture.application_templates) != NULL);
        praktor_init_result_destroy(&result);
        fixture_destroy(&fixture);
    }

    it("refuses to replace an existing project file") {
        init_fixture fixture;
        char source[1024];
        praktor_init_result result = {0};
        check(fixture_init(&fixture));
        check(join_path(source, sizeof(source), fixture.executable_templates, "basic.yml") == 0);
        check(write_text(source, "new\n") == 0);
        check(write_text(fixture.destination, "keep\n") == 0);
        check(run_init(&fixture, "basic", &result) == PRAKTOR_INIT_ALREADY_EXISTS);
        check(read_is(fixture.destination, "keep\n"));
        check(strcmp(result.message, "Error: praktor.yml already exists.") == 0);
        check_null(result.detail);
        praktor_init_result_destroy(&result);
        fixture_destroy(&fixture);
    }

    it("reports sorted unique templates from all search roots") {
        init_fixture fixture;
        char path[1024];
        praktor_init_result result = {0};
        check(fixture_init(&fixture));
        check(join_path(path, sizeof(path), fixture.executable_templates, "zeta.yml") == 0);
        check(write_text(path, "z\n") == 0);
        check(join_path(path, sizeof(path), fixture.application_templates, "alpha.yml") == 0);
        check(write_text(path, "a\n") == 0);
        check(join_path(path, sizeof(path), fixture.working_templates, "zeta.yml") == 0);
        check(write_text(path, "duplicate\n") == 0);
        check(run_init(&fixture, "missing", &result) == PRAKTOR_INIT_TEMPLATE_NOT_FOUND);
        check(strcmp(result.message, "Error: Template 'missing' not found.") == 0);
        check(strcmp(result.detail, "Available templates: alpha, zeta") == 0);
        check(cmeta_fs_access(fixture.destination, SALTS_FS_ACCESS_EXISTS) != 0);
        praktor_init_result_destroy(&result);
        fixture_destroy(&fixture);
    }

    it("rejects invalid calls without creating a destination") {
        init_fixture fixture;
        praktor_init_options options;
        praktor_init_result result = {0};
        check(fixture_init(&fixture));
        praktor_init_options_init(&options);
        options.executable_directory = fixture.executable;
        options.working_directory = fixture.working;
        check(praktor_init_project(&options, &result) == PRAKTOR_INIT_INVALID_ARGUMENT);
        check(strcmp(result.message, "Error: Template name is required.") == 0);
        check(cmeta_fs_access(fixture.destination, SALTS_FS_ACCESS_EXISTS) != 0);
        praktor_init_result_destroy(&result);
        fixture_destroy(&fixture);
    }
}
