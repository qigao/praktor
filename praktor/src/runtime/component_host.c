/* CMeta immutable component and reflection plus Salts Configurator.
 * This is a static provider; Plugin/ServiceHost generations are not faked. */
#include "component_host.h"
#include <cmeta/data_reflect.h>
#include <cmeta/component.h>
#include <cmeta/interface.h>
#include <cmeta/object_interface.h>
#include <core/error_codes.h>
#include "yml/yaml_service.h"
#include <core/fmt.h>
#include <file/fs.h>
#include <stdlib.h>
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

static cmeta_status SALTS_CONFIGURATOR_CALL create_owner(void *ctx,
    const cmeta_data_desc *config_data,const void *config_value,
    const salts_configurator_dependency *deps,size_t count,
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
static cmeta_status SALTS_CONFIGURATOR_CALL activate_owner(
    void *ctx,const cmeta_object_ref *object) {
    if (!ctx || !cmeta_object_ref_valid(object))return CMETA_INVALID_ARGUMENT;
    *(int *)ctx=1;
    return CMETA_OK;
}
static void SALTS_CONFIGURATOR_CALL deactivate_owner(
    void *ctx,const cmeta_object_ref *object) {
    (void)object;
    *(int *)ctx=0;
}
/* One binding per explicit service name; multiple instances of the same
 * Interface use Configurator's existing explicit provider selection. */
cmeta_component(praktor_process_component,cmeta_provides(praktor_execution_service));
CMETA_OBJECT_INTERFACE_ADAPTER(praktor_execution_service);
static cmeta_status project_execution(void *user,const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,cmeta_interface_projection *out) {
    (void)user;
    if(!object||!out||!cmeta_interface_desc_equal(expected,praktor_execution_service_interface()))
        return CMETA_TRAIT_MISSING;
    praktor_execution_service service=praktor_process_service_bind(object->object);
    out->size=sizeof(*out);out->interface=praktor_execution_service_interface();
    out->self=service.self;out->dispatch=service.vtable;
    return CMETA_OK;
}
static const cmeta_object_interface_provider execution_interfaces={
    sizeof(cmeta_object_interface_provider),NULL,project_execution};
static cmeta_status SALTS_CONFIGURATOR_CALL create_execution(void *ctx,
    const cmeta_data_desc *data,const void *value,
    const salts_configurator_dependency *deps,size_t count,cmeta_object_ref *out) {
    (void)deps;
    const praktor_process_config *config=value;
    if(!ctx||!config||count||!cmeta_data_desc_equal(data,praktor_process_config_reflection())||
       !config->path||!*config->path||config->timeout_ms<=0)return CMETA_INVALID_ARGUMENT;
    praktor_process_service *service=ctx;
    *service=(praktor_process_service){.path=config->path,.working_dir=config->working_dir,.timeout_ms=config->timeout_ms};
    return cmeta_object_borrow(out,ctx,praktor_process_service_reflection(),NULL);
}
static cmeta_status SALTS_CONFIGURATOR_CALL activate_execution(void *ctx,const cmeta_object_ref *object) {
    if(!ctx||!cmeta_object_ref_valid(object))return CMETA_INVALID_ARGUMENT;
    ((praktor_process_service *)ctx)->active=true;return CMETA_OK;
}
static void SALTS_CONFIGURATOR_CALL deactivate_execution(void *ctx,const cmeta_object_ref *object) {
    (void)object;
    praktor_process_service *service=ctx;
    if(service->jobs)abort();
    service->active=false;
}
/* File references are explicit runtime IO. Parse before Configurator start or
 * any task side effect, and retain only normalized owned declarations. */
static int load_definition(const char *path,json_value_t **out,char *error,size_t capacity) {
    enum {MAX_DEFINITION_BYTES=1024u*1024u};
    file_t file=SALTS_INVALID_FILE;
    tstr source=NULL;
    praktor_yaml_document *document=NULL;
    praktor_yaml_field_error field={0};
    char detail[256]={0},chunk[4096];
    praktor_yaml_diagnostic diagnostic={detail,sizeof detail,PRAKTOR_YAML_FAILURE_NONE};
    int rc=SALTS_EINVAL;
    if(!path||!*path)goto done;
    fs_stat_t stat;
    if(fs_stat(path,&stat)!=0||!stat.is_file||stat.size>MAX_DEFINITION_BYTES) {
        fmt_text(detail,sizeof detail,"cannot open service definition as a regular file within 1 MiB");goto done;
    }
    file=fs_open(path,SALTS_FS_O_RDONLY,0);
    if(file==SALTS_INVALID_FILE){fmt_text(detail,sizeof detail,"cannot open service definition");goto done;}
    source=tstr_new();if(!source){rc=SALTS_ENOMEM;goto done;}
    for(;;) {
        int n=fs_read(file,chunk,sizeof chunk);
        if(n<0){rc=SALTS_EIO;goto done;}
        if(n==0)break;
        if((size_t)n>MAX_DEFINITION_BYTES-tstr_len(source)) {
            fmt_text(detail,sizeof detail,"service definition exceeds 1 MiB");goto done;
        }
        tstr next=tstr_cat_len(source,chunk,(size_t)n);
        if(!next){rc=SALTS_ENOMEM;goto done;}source=next;
    }
    rc=praktor_yaml_document_create(tstr_to_v(source),&document,&diagnostic);
    if(rc==SALTS_OK)rc=praktor_yaml_service_definition_create(praktor_yaml_root(document),out,&field);
    if(field.text)fmt_text(detail,sizeof detail,field.text);
done:
    if(file!=SALTS_INVALID_FILE&&fs_close(file)!=0) {
        if(rc==SALTS_OK){json_free(*out);*out=NULL;}rc=SALTS_EIO;
    }
    if(rc!=SALTS_OK&&error&&capacity)fmt(error,capacity,"service '{}': {}",path?path:"",detail[0]?detail:"invalid declaration or IO failure");
    praktor_yaml_field_error_dispose(&field);praktor_yaml_document_destroy(document);tstr_free(source);
    return rc;
}
int praktor_component_host_start(praktor_component_host *host,
                                 const praktor_runtime_limits *limits,
                                 const json_value_t *metadata,char *error,size_t capacity) {
    salts_configurator_service service={0};
    praktor_owner_service owner=praktor_owner_service_bind(NULL,NULL);
    if (!host || !limits)return SALTS_EINVAL;
    memset(host,0,sizeof(*host));
    host->bindings[0]=(salts_configurator_provider_binding){
        sizeof(salts_configurator_provider_binding),
        SALTS_CONFIGURATOR_PROVIDER_BINDING_ABI_VERSION,
        cmeta_component_meta(praktor_runtime_component),
        &host->admitted,&interfaces,
        create_owner,activate_owner,deactivate_owner
    };
    host->deployments[0]=(salts_configurator_deployment){
        &host->bindings[0],cmeta_reflected_data(praktor_runtime_limits),limits};
    const json_value_t *services=json_object_get(metadata,"services");
    size_t service_count=json_object_size(services);
    if(service_count>PRAKTOR_WORKFLOW_MAX_SERVICES)return SALTS_EINVAL;
    host->service_count=service_count;
    for(size_t i=0u;i<host->service_count;++i) {
        const char *name=json_object_key(services,i);
        const char *file=json_get_string(services,name);
        int rc=load_definition(file,&host->definitions[i],error,capacity);
        if(rc!=SALTS_OK)return rc;
        const json_value_t *entry=host->definitions[i];
        const char *path=json_get_string(entry,"path");
        const json_value_t *timeout=json_object_get(entry,"timeout_ms");
        if(!path||!timeout)return SALTS_EINVAL;
        host->configs[i]=(praktor_process_config){path,json_get_string(entry,"working_dir"),(int)json_number(timeout)};
        host->descriptions[i]=*cmeta_component_meta(praktor_process_component);
        host->descriptions[i].stable_id=name;
        host->descriptions[i].config=praktor_process_config_reflection();
        host->bindings[i+1u]=(salts_configurator_provider_binding){
            sizeof(salts_configurator_provider_binding),SALTS_CONFIGURATOR_PROVIDER_BINDING_ABI_VERSION,
            &host->descriptions[i],&host->processes[i],&execution_interfaces,
            create_execution,activate_execution,deactivate_execution};
        host->deployments[i+1u]=(salts_configurator_deployment){
            &host->bindings[i+1u],praktor_process_config_reflection(),&host->configs[i]};
    }
    size_t count=host->service_count+1u;
    if (salts_configurator_context_init(&host->context,host->deployments,count,
            NULL,0u,host->instances,count,host->dependencies,count,
            host->activation_order,count)!=SALTS_CONFIGURATOR_OK)
        return SALTS_EINVAL;
    if (salts_configurator_context_resolve(&host->context)!=SALTS_CONFIGURATOR_OK ||
        salts_configurator_context_start(&host->context)!=SALTS_CONFIGURATOR_OK)
        return SALTS_EINVAL;
    host->active=true;
    if (salts_configurator_context_find_service(&host->context,
            praktor_owner_service_interface(),&service)!=SALTS_CONFIGURATOR_OK ||
        praktor_owner_service_borrow_from_object(
            service.object,service.interfaces,&owner)!=CMETA_OK ||
        praktor_owner_service_available(&owner)!=1) {
        /* Activated provider still has an owned Configurator lifecycle.
         * Refusing service projection must not leak that component scope. */
        (void)salts_configurator_context_stop(&host->context);
        host->active=false;
        return SALTS_EINVAL;
    }
    for(size_t i=0u;i<host->service_count;++i) {
        if(salts_configurator_context_find_service_from(&host->context,
            host->descriptions[i].stable_id,praktor_execution_service_interface(),&service)!=SALTS_CONFIGURATOR_OK||
           praktor_execution_service_borrow_from_object(service.object,service.interfaces,&host->services[i])!=CMETA_OK) {
            (void)praktor_component_host_stop(host);return SALTS_EINVAL;
        }
    }
    return SALTS_OK;
}
int praktor_component_host_service(praktor_component_host *host,const char *name,
                                    praktor_execution_service *out,const json_value_t **definition) {
    if(!host||!host->active||!name||!out||!definition)return SALTS_EINVAL;
    for(size_t i=0u;i<host->service_count;++i)
        if(!strcmp(name,host->descriptions[i].stable_id)) {*out=host->services[i];*definition=host->definitions[i];return SALTS_OK;}
    return SALTS_ENOENT;
}
int praktor_component_host_stop(praktor_component_host *host) {
    if (!host)return SALTS_EINVAL;
    for(size_t i=0u;i<host->service_count;++i)
        if(host->processes[i].jobs)return SALTS_EBUSY;
    if (host->active&&salts_configurator_context_stop(&host->context)!=SALTS_CONFIGURATOR_OK)
        return SALTS_EIO;
    host->active=false;
    for(size_t i=0u;i<host->service_count;++i) {
        json_free(host->definitions[i]);host->definitions[i]=NULL;
    }
    host->service_count=0u;
    return SALTS_OK;
}
