#ifndef PRAKTOR_COMMAND_TYPES_H
#define PRAKTOR_COMMAND_TYPES_H
/* The command/program grammar has no existing native output-format enum. These
 * internal projection tags carry parsing policy, not a runtime capability. */
typedef enum praktor_yaml_output_format {
    PRAKTOR_YAML_OUTPUT_TEXT, PRAKTOR_YAML_OUTPUT_JSON
} praktor_yaml_output_format;
typedef enum praktor_yaml_postprocessor {
    PRAKTOR_YAML_POST_NONE, PRAKTOR_YAML_POST_REGEX, PRAKTOR_YAML_POST_JSON,
    PRAKTOR_YAML_POST_LINES, PRAKTOR_YAML_POST_KEYVALUE
} praktor_yaml_postprocessor;
#endif
