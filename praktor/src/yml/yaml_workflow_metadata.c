#include "yml/yaml_workflow_metadata.h"
#include "yaml_diagnostic_internal.h"
#include "yml/yaml_task_fields.h"
#include <limits.h>
#include <string.h>
static bool timeout_value(const char *text,size_t length,int *out) {
    int value=0;
    if(!length)return false;
    for(size_t i=0u;i<length;++i) {
        if(text[i]<'0'||text[i]>'9')return false;
        int digit=text[i]-'0';
        if(value>(INT_MAX-digit)/10)return false;
        value=value*10+digit;
    }
    if(!value)return false;
    *out=value;return true;
}
static bool stream_value(const char *text,size_t length,bool *out) {
    if(length==4u&&!memcmp(text,"true",4u)){*out=true;return true;}
    if(length==5u&&!memcmp(text,"false",5u)){*out=false;return true;}
    return false;
}
int praktor_yaml_workflow_resolve_defaults(const json_value_t *metadata,
    const char *environment_timeout,const char *environment_stream,
    int *timeout_ms,bool *stream_output,const char **error) {
    if(error)*error=NULL;
    if(!metadata||!timeout_ms||!stream_output)return SALTS_EINVAL;
    int timeout=*timeout_ms;
    bool stream=*stream_output;
    const char *reason=NULL;
    if(environment_timeout&&!timeout_value(environment_timeout,strlen(environment_timeout),&timeout))
        reason="PRAKTOR_TIMEOUT_MS must be an integer from 1 to 2147483647";
    else if(environment_stream&&!stream_value(environment_stream,strlen(environment_stream),&stream))
        reason="PRAKTOR_STREAM_OUTPUT must be true or false";
    const json_value_t *defaults=json_object_get(metadata,"defaults");
    if(!reason&&defaults) {
        const json_value_t *value=json_object_get(defaults,"timeout");
        const char *text=json_string(value);
        if(value&&(!text||!timeout_value(text,json_string_len(value),&timeout)))
            reason="defaults.timeout must be an integer from 1 to 2147483647";
        value=json_object_get(defaults,"stream_output");text=json_string(value);
        if(!reason&&value&&(!text||!stream_value(text,json_string_len(value),&stream)))
            reason="defaults.stream_output must be true or false";
    }
    if(reason){if(error)*error=reason;return SALTS_EINVAL;}
    *timeout_ms=timeout;*stream_output=stream;return SALTS_OK;
}
static int at_error(praktor_yaml_node node,const char *message,praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail_at(node,vstr_from_cstr(message),error);
}
static int add_value(json_value_t *object,const char *key,json_value_t *value) {
    if(!value)return SALTS_ENOMEM;
    if(!json_object_add_n(object,key,strlen(key),value)){json_free(value);return SALTS_ENOMEM;}
    return SALTS_OK;
}
static int optional(praktor_yaml_node node,const char *key,praktor_yaml_node *out,praktor_yaml_field_error *error) {
    return praktor_yaml_field_get(node,key,out,error);
}
static int add_scalar(json_value_t *object,const char *key,praktor_yaml_node node,
                      const char *message,praktor_yaml_field_error *error) {
    tstr text=NULL;
    int status=praktor_yaml_field_scalar(node,vstr_from_cstr(message),&text,error);
    if(status!=SALTS_OK)return status;
    status=add_value(object,key,json_create_string(text));tstr_free(text);return status;
}

static int add_string_map(json_value_t *object, const char *key,
                          praktor_yaml_node node, praktor_yaml_field_error *error) {
    praktor_yaml_task_fields *owner = NULL;
    const praktor_yaml_string_map *map = NULL;
    json_value_t *result = NULL;
    size_t index;
    int status = praktor_yaml_task_fields_create(node, PRAKTOR_YAML_STRING_MAP, &owner, error);
    if (status != SALTS_OK) return status;
    status = praktor_yaml_task_map(owner, &map);
    if (status != SALTS_OK) goto done;
    result = json_create_object();
    if (!result) { status = SALTS_ENOMEM; goto done; }
    for (index = 0; index < praktor_yaml_string_map_size(map); ++index) {
        praktor_yaml_string_entry entry = {0};
        json_value_t *value;
        status = praktor_yaml_string_map_at(map, index, &entry);
        if (status != SALTS_OK) goto done;
        value = json_create_string_n(entry.value.data, entry.value.len);
        if (!value) { status = SALTS_ENOMEM; goto done; }
        if (!json_object_add_n(result, entry.key.data, entry.key.len, value)) {
            json_free(value); status = SALTS_ENOMEM; goto done;
        }
    }
    status = add_value(object, key, result);
    result = NULL;
done:
    json_free(result);
    praktor_yaml_task_fields_destroy(owner);
    if (status == SALTS_ENOMEM && error && error->origin == PRAKTOR_YAML_FAILURE_NONE)
        error->origin = PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}

static int add_string_list(json_value_t *object, const char *key,
                           praktor_yaml_node node, praktor_yaml_field_error *error) {
    praktor_yaml_task_fields *owner = NULL;
    const praktor_yaml_string_list *list = NULL;
    json_value_t *result = NULL;
    size_t index;
    int status = praktor_yaml_task_fields_create(node, PRAKTOR_YAML_STRING_LIST, &owner, error);
    if (status != SALTS_OK) return status;
    status = praktor_yaml_task_list(owner, &list);
    if (status != SALTS_OK) goto done;
    result = json_create_array();
    if (!result) { status = SALTS_ENOMEM; goto done; }
    for (index = 0; index < praktor_yaml_string_list_size(list); ++index) {
        vstr text = {0};
        json_value_t *value;
        status = praktor_yaml_string_list_at(list, index, &text);
        if (status != SALTS_OK) goto done;
        value = json_create_string_n(text.data, text.len);
        if (!value) { status = SALTS_ENOMEM; goto done; }
        if (!json_array_add_checked(result, value)) {
            json_free(value); status = SALTS_ENOMEM; goto done;
        }
    }
    status = add_value(object, key, result);
    result = NULL;
done:
    json_free(result);
    praktor_yaml_task_fields_destroy(owner);
    if (status == SALTS_ENOMEM && error && error->origin == PRAKTOR_YAML_FAILURE_NONE)
        error->origin = PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}
/* Keep service files as explicit references; parsing a workflow snapshot never
 * opens files or starts providers. Runtime owns the admitted definitions. */
static int add_services(json_value_t *result,praktor_yaml_node node,praktor_yaml_field_error *error) {
    if(!praktor_yaml_is_map(node)||praktor_yaml_count(node)>PRAKTOR_WORKFLOW_MAX_SERVICES)
        return at_error(node,"services must map at most 16 names to YAML files",error);
    int rc=add_string_map(result,"services",node,error);if(rc!=SALTS_OK)return rc;
    const json_value_t *services=json_object_get(result,"services");
    if(json_object_size(services)!=praktor_yaml_count(node))return at_error(node,"duplicate service name",error);
    for(size_t i=0u;i<json_object_size(services);++i) {
        const char *name=json_object_key(services,i),*file=json_get_string(services,name);
        size_t n=strlen(name);
        if(!n||n>128u||!file||!*file||strlen(file)>4096u||!strcmp(name,"praktor_runtime_component"))
            return at_error(node,"invalid service name or YAML file reference",error);
        for(size_t j=0u;j<n;++j) {
            unsigned char c=(unsigned char)name[j];
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))
                return at_error(node,"invalid service name",error);
        }
    }
    return SALTS_OK;
}
int praktor_yaml_workflow_metadata_create(praktor_yaml_node node,json_value_t **out,
    praktor_yaml_field_error *error) {
    static const vstr allowed[]={{"name",4},{"description",11},{"outputs",7},
        {"variables",9},{"env",3},{"dotEnv",6},{"defaults",8},
        {"includes",8},{"tasks",5},{"services",8}};
    static const vstr default_keys[]={{"timeout",7},{"stream_output",13}};
    praktor_yaml_node child={0},defaults_node={0},timeout_node={0};
    praktor_yaml_node variables_node={0},env_node={0},dot_env_node={0};
    json_value_t *result=NULL,*defaults=NULL;
    int status;
    praktor_yaml_field_error_dispose(error);
    if(!out)return SALTS_EINVAL;
    if(!praktor_yaml_is_map(node))return at_error(node,"workflow must be a map",error);
    status=praktor_yaml_check_keys(node,allowed,sizeof(allowed)/sizeof(allowed[0]),error);
    if(status!=SALTS_OK)return status;
    result=json_create_object();
    if(!result){if(error)error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;return SALTS_ENOMEM;}
    status=optional(node,"services",&child,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(child)) {
        status=add_services(result,child,error);if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"name",&child,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(child)){status=add_scalar(result,"name",child,"Expected YAML scalar",error);if(status!=SALTS_OK)goto fail;}
    status=optional(node,"description",&child,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(child)){status=add_scalar(result,"description",child,"Expected YAML scalar",error);if(status!=SALTS_OK)goto fail;}
    status=optional(node,"variables",&variables_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(variables_node)) {
        status=add_string_map(result,"variables",variables_node,error);
        if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"env",&env_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(env_node)) {
        status=add_string_map(result,"env",env_node,error);
        if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"dotEnv",&dot_env_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(dot_env_node)) {
        status=add_string_list(result,"dot_env",dot_env_node,error);
        if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"defaults",&defaults_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(defaults_node)) {
        if(!praktor_yaml_is_map(defaults_node)){status=at_error(defaults_node,"defaults must be a map",error);goto fail;}
        status=praktor_yaml_check_keys(defaults_node,default_keys,2u,error);if(status!=SALTS_OK)goto fail;
        defaults=json_create_object();if(!defaults){status=SALTS_ENOMEM;goto fail;}
        for(size_t i=0u;i<2u;++i) {
            const char *key=i==0u?"timeout":"stream_output";
            status=optional(defaults_node,key,&timeout_node,error);if(status!=SALTS_OK)goto fail;
            if(praktor_yaml_valid(timeout_node)) {
                tstr text=NULL;int timeout=0;bool stream=false;
                const char *message=i==0u?
                    "defaults.timeout must be an integer from 1 to 2147483647":
                    "defaults.stream_output must be true or false";
                status=praktor_yaml_field_scalar(timeout_node,vstr_from_cstr(message),&text,error);
                if(status!=SALTS_OK)goto fail;
                bool valid=i==0u?timeout_value(text,tstr_len(text),&timeout):stream_value(text,tstr_len(text),&stream);
                if(!valid){tstr_free(text);status=at_error(timeout_node,message,error);goto fail;}
                status=add_value(defaults,key,json_create_string(text));tstr_free(text);
                if(status!=SALTS_OK)goto fail;
            }
        }
        status=add_value(result,"defaults",defaults);defaults=NULL;if(status!=SALTS_OK)goto fail;
    }
    *out=result;return SALTS_OK;
fail:
    json_free(defaults);json_free(result);
    if(status==SALTS_ENOMEM&&error&&error->origin==PRAKTOR_YAML_FAILURE_NONE)error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}
