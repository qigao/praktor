#ifndef PRAKTOR_INIT_H
#define PRAKTOR_INIT_H

#include <stddef.h>
#include <tstr.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRAKTOR_INIT_MAX_TEMPLATE_NAME_BYTES 4096u
#define PRAKTOR_INIT_MAX_DISCOVERED_TEMPLATES 4096u
#define PRAKTOR_INIT_MAX_DISCOVERED_NAME_BYTES (1024u * 1024u)

typedef enum praktor_init_status {
    PRAKTOR_INIT_OK = 0,
    PRAKTOR_INIT_INVALID_ARGUMENT,
    PRAKTOR_INIT_ALREADY_EXISTS,
    PRAKTOR_INIT_TEMPLATE_NOT_FOUND,
    PRAKTOR_INIT_LIMIT_EXCEEDED,
    PRAKTOR_INIT_NO_MEMORY,
    PRAKTOR_INIT_IO_ERROR
} praktor_init_status;

typedef struct praktor_init_options {
    /* Borrowed for the duration of praktor_init_project(). */
    const char *template_name;
    /* NULL selects the directory of the current executable. */
    const char *executable_directory;
    /* NULL selects the process current working directory. */
    const char *working_directory;
} praktor_init_options;

typedef struct praktor_init_result {
    /* Unique tstr owners. Destroy with praktor_init_result_destroy(). */
    tstr message;
    tstr detail;
} praktor_init_result;

void praktor_init_options_init(praktor_init_options *options);
void praktor_init_result_destroy(praktor_init_result *result);

/*
 * Creates <working_directory>/praktor.yml from the first matching template in
 * executable/templates, executable/../praktor/templates, then
 * working_directory/templates. Destination creation is exclusive; a failed
 * copy removes its partial destination. The caller must pass a zero-initialized
 * result and retains all option strings. Result strings remain owned by the
 * caller until praktor_init_result_destroy().
 */
praktor_init_status praktor_init_project(const praktor_init_options *options,
                                         praktor_init_result *result);

#ifdef __cplusplus
}
#endif
#endif
