#ifndef PRAKTOR_COMPONENT_HOST_H
#define PRAKTOR_COMPONENT_HOST_H
#include "praktor/runtime.h"
#include <cmeta/configurator.h>
#include "providers/process.h"
#include "yml/yaml_workflow_metadata.h"
#include <json_parser.h>
#include <stdbool.h>
typedef struct praktor_component_host {
    int admitted;
    salts_configurator_provider_binding bindings[PRAKTOR_WORKFLOW_MAX_SERVICES+1u];
    salts_configurator_deployment deployments[PRAKTOR_WORKFLOW_MAX_SERVICES+1u];
    salts_configurator_instance instances[PRAKTOR_WORKFLOW_MAX_SERVICES+1u];
    salts_configurator_dependency dependencies[PRAKTOR_WORKFLOW_MAX_SERVICES+1u];
    size_t activation_order[PRAKTOR_WORKFLOW_MAX_SERVICES+1u];
    cmeta_component_desc descriptions[PRAKTOR_WORKFLOW_MAX_SERVICES];
    praktor_process_config configs[PRAKTOR_WORKFLOW_MAX_SERVICES];
    praktor_process_service processes[PRAKTOR_WORKFLOW_MAX_SERVICES];
    praktor_execution_service services[PRAKTOR_WORKFLOW_MAX_SERVICES];
    json_value_t *definitions[PRAKTOR_WORKFLOW_MAX_SERVICES];
    size_t service_count;
    salts_configurator_context context;
    bool active;
} praktor_component_host;
int praktor_component_host_start(praktor_component_host *host,
                                 const praktor_runtime_limits *limits,
                                 const json_value_t *metadata,char *error,size_t capacity);
int praktor_component_host_service(praktor_component_host *host,const char *name,
                                    praktor_execution_service *out,const json_value_t **definition);
int praktor_component_host_stop(praktor_component_host *host);
#endif
