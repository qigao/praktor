#include "util/praktor_init.h"

#include <cmeta_fs.h>
#include <cstl/vec.h>
#include <errno.h>
#include <fmt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#define PRAKTOR_INIT_MAX_PATH_BYTES (1024u * 1024u)
#define PRAKTOR_INIT_COPY_CHUNK_BYTES (64u * 1024u)

typedef struct praktor_init_names {
    vec_t values;
    size_t retained_bytes;
} praktor_init_names;

static void init_names_destroy(praktor_init_names *names) {
    size_t i;
    if (names == NULL) return;
    for (i = 0u; i < vec_size(&names->values); ++i)
        tstr_free(*(tstr *)vec_at(&names->values, i));
    vec_destroy(&names->values);
    names->retained_bytes = 0u;
}

static int init_names_init(praktor_init_names *names) {
    memset(names, 0, sizeof(*names));
    return vec_init_bytes(&names->values, sizeof(tstr), _Alignof(tstr),
                          PRAKTOR_INIT_MAX_DISCOVERED_TEMPLATES) == STL_OK;
}

static int init_name_exists(const praktor_init_names *names, const char *name,
                            size_t length) {
    size_t i;
    for (i = 0u; i < vec_size(&names->values); ++i) {
        tstr existing = *(tstr const *)vec_at_const(&names->values, i);
        if (tstr_len(existing) == length && memcmp(existing, name, length) == 0) return 1;
    }
    return 0;
}

static praktor_init_status init_names_add(praktor_init_names *names,
                                          const char *name, size_t length) {
    tstr owned;
    if (init_name_exists(names, name, length)) return PRAKTOR_INIT_OK;
    if (vec_size(&names->values) == PRAKTOR_INIT_MAX_DISCOVERED_TEMPLATES ||
        length >= PRAKTOR_INIT_MAX_DISCOVERED_NAME_BYTES - names->retained_bytes)
        return PRAKTOR_INIT_LIMIT_EXCEEDED;
    owned = tstr_dup_len(name, length);
    if (owned == NULL) return PRAKTOR_INIT_NO_MEMORY;
    if (vec_push(&names->values, &owned) != STL_OK) {
        tstr_free(owned);
        return PRAKTOR_INIT_NO_MEMORY;
    }
    names->retained_bytes += length + 1u;
    return PRAKTOR_INIT_OK;
}

static int compare_names(const void *lhs, const void *rhs) {
    return strcmp(*(tstr const *)lhs, *(tstr const *)rhs);
}

static tstr init_names_join(praktor_init_names *names) {
    size_t i, offset = 0u, total = 0u;
    tstr joined;
    if (vec_size(&names->values) == 0u) return tstr_dup("<none found>");
    qsort(vec_data(&names->values), vec_size(&names->values), sizeof(tstr), compare_names);
    for (i = 0u; i < vec_size(&names->values); ++i) {
        size_t length = tstr_len(*(tstr *)vec_at(&names->values, i));
        if (length > SIZE_MAX - total || (i != 0u && total > SIZE_MAX - 2u)) return NULL;
        total += length + (i == 0u ? 0u : 2u);
    }
    joined = tstr_new_len(NULL, total);
    if (joined == NULL) return NULL;
    for (i = 0u; i < vec_size(&names->values); ++i) {
        tstr name = *(tstr *)vec_at(&names->values, i);
        size_t length = tstr_len(name);
        if (i != 0u) {
            joined[offset++] = ',';
            joined[offset++] = ' ';
        }
        memcpy(joined + offset, name, length);
        offset += length;
    }
    joined[offset] = '\0';
    return joined;
}

static tstr init_path_join(const char *base, const char *child) {
    size_t base_length, child_length, capacity;
    tstr path;
    if (base == NULL || child == NULL) return NULL;
    if (base[0] == '\0') return tstr_dup(child);
    base_length = strlen(base);
    child_length = strlen(child);
    if (base_length > PRAKTOR_INIT_MAX_PATH_BYTES ||
        child_length > PRAKTOR_INIT_MAX_PATH_BYTES ||
        base_length > SIZE_MAX - child_length - 2u ||
        base_length + child_length + 2u > PRAKTOR_INIT_MAX_PATH_BYTES)
        return NULL;
    capacity = base_length + child_length + 2u;
    path = tstr_new_len(NULL, capacity - 1u);
    if (path == NULL) return NULL;
    if (cmeta_fs_path_join(path, capacity, base, child) != 0 ||
        !tstr_set_len_checked(path, strlen(path))) {
        tstr_free(path);
        return NULL;
    }
    return path;
}

static tstr init_path_dirname(const char *path) {
    size_t capacity;
    tstr directory;
    if (path == NULL || path[0] == '\0') return tstr_new();
    if (strlen(path) >= PRAKTOR_INIT_MAX_PATH_BYTES) return NULL;
    capacity = strlen(path) + 2u;
    directory = tstr_new_len(NULL, capacity - 1u);
    if (directory == NULL) return NULL;
    if (cmeta_fs_path_dirname(path, directory, capacity) != 0 ||
        !tstr_set_len_checked(directory, strlen(directory))) {
        tstr_free(directory);
        return NULL;
    }
    return directory;
}

static tstr init_current_directory(void) {
    size_t capacity = 256u;
    while (capacity <= PRAKTOR_INIT_MAX_PATH_BYTES) {
        tstr directory = tstr_new_len(NULL, capacity - 1u);
        if (directory == NULL) return NULL;
#ifdef _WIN32
        {
            DWORD length = GetCurrentDirectoryA((DWORD)capacity, directory);
            if (length != 0u && (size_t)length < capacity) {
                (void)tstr_set_len_checked(directory, (size_t)length);
                return directory;
            }
            if (length != 0u && (size_t)length + 1u > capacity) capacity = (size_t)length + 1u;
            else capacity *= 2u;
        }
#else
        if (getcwd(directory, capacity) != NULL) {
            (void)tstr_set_len_checked(directory, strlen(directory));
            return directory;
        }
        if (errno != ERANGE) {
            tstr_free(directory);
            return NULL;
        }
        capacity *= 2u;
#endif
        tstr_free(directory);
    }
    return NULL;
}

static tstr init_executable_directory(void) {
#ifdef _WIN32
    size_t capacity = 256u;
    while (capacity <= PRAKTOR_INIT_MAX_PATH_BYTES) {
        tstr path = tstr_new_len(NULL, capacity - 1u);
        DWORD length;
        tstr directory;
        if (path == NULL) return NULL;
        SetLastError(ERROR_SUCCESS);
        length = GetModuleFileNameA(NULL, path, (DWORD)capacity);
        if (length != 0u && (size_t)length < capacity) {
            (void)tstr_set_len_checked(path, (size_t)length);
            directory = init_path_dirname(path);
            tstr_free(path);
            return directory;
        }
        tstr_free(path);
        capacity *= 2u;
    }
    return NULL;
#elif defined(__linux__)
    size_t capacity = 1024u;
    while (capacity <= PRAKTOR_INIT_MAX_PATH_BYTES) {
        tstr path = tstr_new_len(NULL, capacity - 1u);
        int length;
        tstr directory;
        if (path == NULL) return NULL;
        length = cmeta_fs_readlink("/proc/self/exe", path, capacity);
        if (length >= 0) {
            (void)tstr_set_len_checked(path, (size_t)length);
            directory = init_path_dirname(path);
            tstr_free(path);
            return directory;
        }
        tstr_free(path);
        if (length != -ENAMETOOLONG) return tstr_new();
        capacity *= 2u;
    }
    return NULL;
#else
    /* Preserve the previous non-Windows/non-Linux fallback search roots. */
    return tstr_new();
#endif
}

static praktor_init_status init_discover_directory(const char *directory,
                                                    praktor_init_names *names) {
    cmeta_fs_stat_t stat_value = {0};
    cmeta_fs_dir_t *handle = NULL;
    cmeta_fs_dirent_t entry;
    int read_status;
    if (cmeta_fs_access(directory, SALTS_FS_ACCESS_EXISTS) != 0 ||
        cmeta_fs_stat(directory, &stat_value) != 0 || !stat_value.is_directory)
        return PRAKTOR_INIT_OK;
    if (cmeta_fs_opendir(directory, &handle) != 0) return PRAKTOR_INIT_IO_ERROR;
    while ((read_status = cmeta_fs_readdir(handle, &entry)) > 0) {
        size_t length = strlen(entry.name);
        tstr full_path;
        praktor_init_status status;
        if (length <= 4u || strcmp(entry.name + length - 4u, ".yml") != 0) continue;
        full_path = init_path_join(directory, entry.name);
        if (full_path == NULL) {
            (void)cmeta_fs_closedir(handle);
            return PRAKTOR_INIT_NO_MEMORY;
        }
        memset(&stat_value, 0, sizeof(stat_value));
        read_status = cmeta_fs_access(full_path, SALTS_FS_ACCESS_EXISTS) == 0
            ? cmeta_fs_stat(full_path, &stat_value) : -ENOENT;
        tstr_free(full_path);
        if (read_status != 0 || !stat_value.is_file) continue;
        status = init_names_add(names, entry.name, length - 4u);
        if (status != PRAKTOR_INIT_OK) {
            (void)cmeta_fs_closedir(handle);
            return status;
        }
    }
    if (cmeta_fs_closedir(handle) != 0 || read_status < 0) return PRAKTOR_INIT_IO_ERROR;
    return PRAKTOR_INIT_OK;
}

static praktor_init_status init_find_template(tstr const search_paths[3],
                                              const char *filename,
                                              tstr *source_path) {
    size_t i;
    for (i = 0u; i < 3u; ++i) {
        cmeta_fs_stat_t stat_value = {0};
        tstr candidate = init_path_join(search_paths[i], filename);
        if (candidate == NULL) return PRAKTOR_INIT_NO_MEMORY;
        if (cmeta_fs_access(candidate, SALTS_FS_ACCESS_EXISTS) == 0 &&
            cmeta_fs_stat(candidate, &stat_value) == 0) {
            *source_path = candidate;
            return PRAKTOR_INIT_OK;
        }
        tstr_free(candidate);
    }
    return PRAKTOR_INIT_TEMPLATE_NOT_FOUND;
}

static praktor_init_status init_copy_exclusive(const char *source_path,
                                               const char *working_directory) {
    cmeta_fs_root_t *root = NULL;
    cmeta_fs_root_file_t *destination = NULL;
    cmeta_file_t source = SALTS_INVALID_FILE;
    char buffer[PRAKTOR_INIT_COPY_CHUNK_BYTES];
    praktor_init_status status = PRAKTOR_INIT_IO_ERROR;
    int created = 0, read_count = 0;
    if (cmeta_fs_root_open(working_directory, &root) != 0) goto finished;
    source = cmeta_fs_open(source_path, SALTS_FS_O_RDONLY, 0);
    if (source == SALTS_INVALID_FILE) goto finished;
    {
        int open_status = cmeta_fs_root_file_open(
            root, "praktor.yml",
            SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_ROOT_O_EXCL,
            SALTS_FS_DEFAULT_MODE, &destination);
        if (open_status == -EEXIST) {
            status = PRAKTOR_INIT_ALREADY_EXISTS;
            goto finished;
        }
        if (open_status != 0) goto finished;
        created = 1;
    }
    while ((read_count = cmeta_fs_read(source, buffer, sizeof(buffer))) > 0) {
        size_t offset = 0u;
        while (offset < (size_t)read_count) {
            int written = cmeta_fs_root_file_write(
                destination, buffer + offset, (size_t)read_count - offset);
            if (written <= 0) goto finished;
            offset += (size_t)written;
        }
    }
    if (read_count < 0) goto finished;
    if (cmeta_fs_root_file_close(destination) != 0) {
        destination = NULL;
        goto finished;
    }
    destination = NULL;
    if (cmeta_fs_close(source) != 0) {
        source = SALTS_INVALID_FILE;
        goto finished;
    }
    source = SALTS_INVALID_FILE;
    status = PRAKTOR_INIT_OK;

finished:
    if (destination != NULL) (void)cmeta_fs_root_file_close(destination);
    if (source != SALTS_INVALID_FILE) (void)cmeta_fs_close(source);
    if (created && status != PRAKTOR_INIT_OK && root != NULL)
        (void)cmeta_fs_root_unlink(root, "praktor.yml");
    if (root != NULL) (void)cmeta_fs_root_close(root);
    return status;
}

static praktor_init_status init_publish_result(praktor_init_result *result,
                                               tstr message, tstr detail,
                                               praktor_init_status status) {
    if (message == NULL || (detail == NULL && status != PRAKTOR_INIT_IO_ERROR &&
                            status != PRAKTOR_INIT_INVALID_ARGUMENT &&
                            status != PRAKTOR_INIT_ALREADY_EXISTS &&
                            status != PRAKTOR_INIT_LIMIT_EXCEEDED &&
                            status != PRAKTOR_INIT_NO_MEMORY)) {
        tstr_free(message);
        tstr_free(detail);
        return PRAKTOR_INIT_NO_MEMORY;
    }
    result->message = message;
    result->detail = detail;
    return status;
}

void praktor_init_options_init(praktor_init_options *options) {
    if (options != NULL) memset(options, 0, sizeof(*options));
}

void praktor_init_result_destroy(praktor_init_result *result) {
    if (result == NULL) return;
    tstr_freep(&result->message);
    tstr_freep(&result->detail);
}

praktor_init_status praktor_init_project(const praktor_init_options *options,
                                         praktor_init_result *result) {
    tstr executable_directory = NULL, working_directory = NULL, executable_parent = NULL;
    tstr search_paths[3] = {NULL, NULL, NULL};
    tstr template_filename = NULL, source_path = NULL;
    praktor_init_status status = PRAKTOR_INIT_INVALID_ARGUMENT;
    size_t i;
    if (result == NULL || result->message != NULL || result->detail != NULL) return status;
    if (options == NULL || options->template_name == NULL || options->template_name[0] == '\0' ||
        strlen(options->template_name) > PRAKTOR_INIT_MAX_TEMPLATE_NAME_BYTES) {
        return init_publish_result(result, tstr_dup("Error: Template name is required."),
                                   NULL, PRAKTOR_INIT_INVALID_ARGUMENT);
    }
    executable_directory = options->executable_directory != NULL
        ? tstr_dup(options->executable_directory) : init_executable_directory();
    working_directory = options->working_directory != NULL
        ? tstr_dup(options->working_directory) : init_current_directory();
    if (executable_directory == NULL || working_directory == NULL) {
        status = PRAKTOR_INIT_NO_MEMORY;
        goto finished;
    }
    {
        cmeta_fs_root_t *root = NULL;
        cmeta_fs_stat_t destination = {0};
        int existing;
        if (cmeta_fs_root_open(working_directory, &root) != 0) {
            status = PRAKTOR_INIT_IO_ERROR;
            goto finished;
        }
        existing = cmeta_fs_root_lstat(root, "praktor.yml", &destination);
        (void)cmeta_fs_root_close(root);
        if (existing == 0) {
            status = PRAKTOR_INIT_ALREADY_EXISTS;
            goto finished;
        }
        if (existing != -ENOENT) {
            status = PRAKTOR_INIT_IO_ERROR;
            goto finished;
        }
    }
    executable_parent = init_path_dirname(executable_directory);
    search_paths[0] = init_path_join(executable_directory, "templates");
    {
        tstr application_root = init_path_join(executable_parent, "praktor");
        if (application_root != NULL) search_paths[1] = init_path_join(application_root, "templates");
        tstr_free(application_root);
    }
    search_paths[2] = init_path_join(working_directory, "templates");
    template_filename = tstr_format("{}.yml", options->template_name);
    if (executable_parent == NULL || search_paths[0] == NULL || search_paths[1] == NULL ||
        search_paths[2] == NULL || template_filename == NULL) {
        status = PRAKTOR_INIT_NO_MEMORY;
        goto finished;
    }
    status = init_find_template(search_paths, template_filename, &source_path);
    if (status == PRAKTOR_INIT_TEMPLATE_NOT_FOUND) {
        praktor_init_names names;
        tstr available = NULL, message = NULL, detail = NULL;
        if (!init_names_init(&names)) {
            status = PRAKTOR_INIT_NO_MEMORY;
            goto finished;
        }
        for (i = 0u; i < 3u && status == PRAKTOR_INIT_TEMPLATE_NOT_FOUND; ++i) {
            praktor_init_status discovery = init_discover_directory(search_paths[i], &names);
            if (discovery != PRAKTOR_INIT_OK) status = discovery;
        }
        if (status == PRAKTOR_INIT_TEMPLATE_NOT_FOUND) {
            available = init_names_join(&names);
            message = tstr_format("Error: Template '{}' not found.", options->template_name);
            if (available != NULL) detail = tstr_format("Available templates: {}", available);
        }
        init_names_destroy(&names);
        tstr_free(available);
        if (status == PRAKTOR_INIT_TEMPLATE_NOT_FOUND) {
            status = init_publish_result(result, message, detail, status);
            goto finished;
        }
    }
    if (status != PRAKTOR_INIT_OK) goto finished;
    {
        tstr message = tstr_format("Successfully initialized praktor.yml using '{}' template.",
                                   options->template_name);
        tstr detail = tstr_format("Source: {}", source_path);
        if (message == NULL || detail == NULL) {
            tstr_free(message);
            tstr_free(detail);
            status = PRAKTOR_INIT_NO_MEMORY;
            goto finished;
        }
        status = init_copy_exclusive(source_path, working_directory);
        if (status == PRAKTOR_INIT_OK) {
            status = init_publish_result(result, message, detail, status);
            goto finished;
        }
        tstr_free(message);
        tstr_free(detail);
    }

finished:
    for (i = 0u; i < 3u; ++i) tstr_free(search_paths[i]);
    tstr_free(source_path);
    tstr_free(template_filename);
    tstr_free(executable_parent);
    tstr_free(executable_directory);
    tstr_free(working_directory);
    if (result->message == NULL) {
        tstr message = NULL;
        if (status == PRAKTOR_INIT_ALREADY_EXISTS)
            message = tstr_dup("Error: praktor.yml already exists.");
        else if (status == PRAKTOR_INIT_LIMIT_EXCEEDED)
            message = tstr_dup("Error: Template discovery exceeded configured limits.");
        else if (status == PRAKTOR_INIT_NO_MEMORY)
            message = tstr_dup("Error copying template: not enough memory");
        else if (status == PRAKTOR_INIT_IO_ERROR)
            message = tstr_dup("Error copying template: I/O error");
        if (message != NULL) result->message = message;
    }
    return status;
}
