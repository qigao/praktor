#ifndef PRAKTOR_YAML_SERVICE_H
#define PRAKTOR_YAML_SERVICE_H
#include "yml/yaml_value.h"
/* Pure, bounded declaration parsing. Output owns its strings/argument list;
 * no file/process/runtime reference survives. Failed parsing leaves *out intact.
 * Only exe providers are currently admitted; other kinds fail explicitly. */
int praktor_yaml_service_definition_create(praktor_yaml_node node, json_value_t **out,
                                           praktor_yaml_field_error *error);
#endif
