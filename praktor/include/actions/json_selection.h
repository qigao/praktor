#ifndef PRAKTOR_ACTIONS_JSON_SELECTION_H
#define PRAKTOR_ACTIONS_JSON_SELECTION_H

#include <stddef.h>

typedef enum praktor_json_result {
    PRAKTOR_JSON_FIRST,
    PRAKTOR_JSON_ONE,
    PRAKTOR_JSON_COUNT
} praktor_json_result;

int praktor_json_result_parse(const char *text,praktor_json_result *out);
int praktor_json_path_normalize(char *path,size_t capacity);

/* Validate a fixed path for the two cardinality-aware result modes. */
int praktor_json_selection_validate(const char *path,char *error,size_t capacity);

/* Input and a disjoint output scratch buffer are borrowed until return.
 * Only the caller commits the scratch result to its value store on success. */
int praktor_json_selection_run(praktor_json_result mode,const char *path,
                               const char *input,size_t length,char *output,
                               size_t output_capacity,char *error,size_t error_capacity);

#endif
