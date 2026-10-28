#ifndef PRAKTOR_COMPONENT_HOST_H
#define PRAKTOR_COMPONENT_HOST_H
#include "praktor/runtime.h"
#include <salts/configurator.h>
#include <stdbool.h>
typedef struct praktor_component_host {
    int admitted;
    salts_component_provider_binding binding;
    salts_configurator_deployment deployment;
    salts_configurator_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_configurator_context context;
    bool active;
} praktor_component_host;
int praktor_component_host_start(praktor_component_host *host,
                                 const praktor_runtime_limits *limits);
int praktor_component_host_stop(praktor_component_host *host);
#endif
