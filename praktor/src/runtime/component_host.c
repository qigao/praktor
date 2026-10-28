/* CMeta immutable component and reflection plus Salts Configurator.
 * This is a static provider; Plugin/ServiceHost generations are not faked. */
#include "component_host.h"
#include <cmeta/data_reflect.h>
#include <cmeta/component.h>
#include <cmeta/interface.h>
#include <cmeta/object_interface.h>
#include <salts/error_codes.h>
#include <string.h>

cmeta_reflect_value(praktor_runtime_limits,"praktor.runtime.limits.v3",
    cmeta_field(int,max_tasks)
    cmeta_field(int,value_capacity)
    cmeta_field(int,timeout_ms));

praktor_runtime_limits praktor_runtime_limits_default(void) {
    praktor_runtime_limits options={32,64,30000};
    return options;
}
const cmeta_data_desc *praktor_runtime_limits_reflection(void) {
    return cmeta_reflected_data(praktor_runtime_limits);
}

#define PRAKTOR_OWNER_METHODS(X,I) X(I,R0,int,available,_)
CMETA_INTERFACE(praktor_owner_service,PRAKTOR_OWNER_METHODS);
cmeta_component_configured(praktor_runtime_component,
    cmeta_reflected_data(praktor_runtime_limits),
    cmeta_provides(praktor_owner_service));

static int owner_available(void *self) { return *(int *)self; }
CMETA_IMPLEMENTS(praktor_owner_service,praktor_owner_service_impl,0u,
    .available=owner_available);
CMETA_OBJECT_INTERFACE_ADAPTER(praktor_owner_service);

static cmeta_status project_owner(void *user,const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,cmeta_interface_projection *out) {
    (void)user;
    if (!object || !out || !cmeta_interface_desc_equal(
            expected,praktor_owner_service_interface()))
        return CMETA_TRAIT_MISSING;
    out->size=sizeof(*out);
    out->interface=praktor_owner_service_interface();
    out->self=object->object;
    out->dispatch=&praktor_owner_service_impl_vtable;
    return CMETA_OK;
}
static const cmeta_object_interface_provider interfaces={
    sizeof(cmeta_object_interface_provider),NULL,project_owner};

static cmeta_status SALTS_COMPONENT_CALL create_owner(void *ctx,
    const cmeta_data_desc *config_data,const void *config_value,
    const salts_component_dependency *deps,size_t count,
    cmeta_object_ref *out) {
    const praktor_runtime_limits *limits=(const praktor_runtime_limits *)config_value;
    (void)deps;
    if (!ctx || !limits || count!=0u ||
        !cmeta_data_desc_equal(config_data,
             cmeta_reflected_data(praktor_runtime_limits)) ||
        limits->max_tasks<1 || limits->max_tasks>64 ||
        limits->value_capacity<1 || limits->value_capacity>4096 ||
        limits->timeout_ms<1)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out,ctx,&cmeta_data_int,NULL);
}
static cmeta_status SALTS_COMPONENT_CALL activate_owner(
    void *ctx,const cmeta_object_ref *object) {
    if (!ctx || !cmeta_object_ref_valid(object))return CMETA_INVALID_ARGUMENT;
    *(int *)ctx=1;
    return CMETA_OK;
}
static void SALTS_COMPONENT_CALL deactivate_owner(
    void *ctx,const cmeta_object_ref *object) {
    (void)object;
    *(int *)ctx=0;
}
int praktor_component_host_start(praktor_component_host *host,
                                 const praktor_runtime_limits *limits) {
    salts_configurator_service service={0};
    praktor_owner_service owner=praktor_owner_service_bind(NULL,NULL);
    if (!host || !limits)return SALTS_EINVAL;
    memset(host,0,sizeof(*host));
    host->binding=(salts_component_provider_binding){
        sizeof(salts_component_provider_binding),
        SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
        cmeta_component_meta(praktor_runtime_component),
        &host->admitted,&interfaces,
        create_owner,activate_owner,deactivate_owner
    };
    host->deployment=(salts_configurator_deployment){
        &host->binding,cmeta_reflected_data(praktor_runtime_limits),limits};
    if (salts_configurator_context_init(&host->context,&host->deployment,1u,
            NULL,0u,host->instances,1u,host->dependencies,1u,
            host->activation_order,1u)!=SALTS_COMPONENT_OK)
        return SALTS_EINVAL;
    if (salts_configurator_context_resolve(&host->context)!=SALTS_COMPONENT_OK ||
        salts_configurator_context_start(&host->context)!=SALTS_COMPONENT_OK)
        return SALTS_EINVAL;
    host->active=true;
    if (salts_configurator_context_find_service(&host->context,
            praktor_owner_service_interface(),&service)!=SALTS_COMPONENT_OK ||
        praktor_owner_service_borrow_from_object(
            service.object,service.interfaces,&owner)!=CMETA_OK ||
        praktor_owner_service_available(&owner)!=1) {
        /* Activated provider still has an owned Configurator lifecycle.
         * Refusing service projection must not leak that component scope. */
        (void)salts_configurator_context_stop(&host->context);
        host->active=false;
        return SALTS_EINVAL;
    }
    return SALTS_OK;
}
int praktor_component_host_stop(praktor_component_host *host) {
    if (!host)return SALTS_EINVAL;
    if (!host->active)return SALTS_OK;
    if (salts_configurator_context_stop(&host->context)!=SALTS_COMPONENT_OK)
        return SALTS_EIO;
    host->active=false;
    return SALTS_OK;
}
