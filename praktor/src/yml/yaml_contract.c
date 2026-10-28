#include "yml/yaml_contract.h"
#include "yaml_diagnostic_internal.h"
#include <string.h>
#include <fmt.h>

static int add_owned(json_value_t *object, const char *key, json_value_t *value) {
    if (value == NULL) return SALTS_ENOMEM;
    if (!json_object_add_n(object, key, strlen(key), value)) {
        json_free(value);
        return SALTS_ENOMEM;
    }
    return SALTS_OK;
}
static int add_text(json_value_t *object, const char *key, const char *text) {
    return add_owned(object, key, json_create_string(text));
}
static int at_error(praktor_yaml_node node, const char *message,
                    praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail_at(node, vstr_from_cstr(message), error);
}
static int optional_node(praktor_yaml_node node, const char *key,
                         praktor_yaml_node *out, praktor_yaml_field_error *error) {
    return praktor_yaml_field_get(node, key, out, error);
}
static int parse_field(praktor_yaml_node node, bool output, const char *field_name,
                       json_value_t **out, praktor_yaml_field_error *error) {
    static const vstr in_keys[]={{"type",4},{"required",8},{"default",7},{"enum",4},{"description",11}};
    static const vstr out_keys[]={{"type",4},{"required",8},{"default",7},{"enum",4},{"description",11},{"value",5}};
    praktor_yaml_node type_node={0}, required_node={0}, description_node={0};
    praktor_yaml_node value_node={0}, default_node={0}, enum_node={0};
    json_value_t *field=NULL,*value=NULL,*enum_values=NULL;
    tstr type_text=NULL,text=NULL;
    bool required=false;
    size_t i;
    int status;
    if(!praktor_yaml_is_map(node)) {
        tstr message=tstr_format("workflow contract field '{}' must be a map",field_name);
        if(!message)return praktor_yaml_field_fail(error,SALTS_ENOMEM,PRAKTOR_YAML_FAILURE_ALLOCATION,NULL);
        status=praktor_yaml_field_fail_at(node,tstr_to_v(message),error);
        tstr_free(message);return status;
    }
    status=praktor_yaml_check_keys(node,output?out_keys:in_keys,
        output?sizeof(out_keys)/sizeof(out_keys[0]):sizeof(in_keys)/sizeof(in_keys[0]),error);
    if(status!=SALTS_OK)return status;
    status=optional_node(node,"type",&type_node,error);
    if(status!=SALTS_OK)return status;
    if(!praktor_yaml_valid(type_node))return at_error(node,"workflow contract field requires 'type'",error);
    status=praktor_yaml_field_scalar(type_node,vstr_from_cstr("workflow contract type must be a scalar"),&type_text,error);
    if(status!=SALTS_OK)return status;
    if(strcmp(type_text,"string")&&strcmp(type_text,"boolean")&&strcmp(type_text,"integer")&&
       strcmp(type_text,"number")&&strcmp(type_text,"array")&&strcmp(type_text,"object")) {
        tstr message=tstr_format("unsupported workflow contract type '{}'",type_text);
        tstr_free(type_text);
        if(!message)return praktor_yaml_field_fail(error,SALTS_ENOMEM,PRAKTOR_YAML_FAILURE_ALLOCATION,NULL);
        status=praktor_yaml_field_fail_at(type_node,tstr_to_v(message),error);
        tstr_free(message);return status;
    }
    field=json_create_object();
    if(!field){tstr_free(type_text);return SALTS_ENOMEM;}
    status=add_text(field,"type",type_text);tstr_free(type_text);
    if(status!=SALTS_OK)goto fail;
    status=optional_node(node,"required",&required_node,error);
    if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(required_node)) {
        status=praktor_yaml_field_boolean(required_node,vstr_from_cstr("required"),
            PRAKTOR_YAML_BOOLEAN_STRICT,&required,error);
        if(status!=SALTS_OK)goto fail;
        status=add_owned(field,"required",json_create_bool(required));
        if(status!=SALTS_OK)goto fail;
    }
    status=optional_node(node,"description",&description_node,error);
    if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(description_node)) {
        status=praktor_yaml_field_scalar(description_node,vstr_from_cstr("description must be a scalar"),&text,error);
        if(status!=SALTS_OK)goto fail;
        status=add_text(field,"description",text);tstr_free(text);text=NULL;
        if(status!=SALTS_OK)goto fail;
    }
    status=optional_node(node,"value",&value_node,error);
    if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(value_node)) {
        status=praktor_yaml_field_scalar(value_node,vstr_from_cstr("output value must be a scalar"),&text,error);
        if(status!=SALTS_OK)goto fail;
        status=add_text(field,"value",text);tstr_free(text);text=NULL;
        if(status!=SALTS_OK)goto fail;
    }
    status=optional_node(node,"default",&default_node,error);
    if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(default_node)) {
        status=praktor_yaml_value(default_node,&value,error);
        if(status!=SALTS_OK)goto fail;
        if(!praktor_contract_value_matches_type(value,json_string(json_object_get(field,"type")))) {
            tstr message=tstr_format("default does not match declared type '{}'",json_string(json_object_get(field,"type")));
            json_free(value);value=NULL;
            if(!message){status=SALTS_ENOMEM;goto fail;}
            status=praktor_yaml_field_fail_at(default_node,tstr_to_v(message),error);
            tstr_free(message);goto fail;
        }
        status=add_owned(field,"default",value);value=NULL;
        if(status!=SALTS_OK)goto fail;
    }
    status=optional_node(node,"enum",&enum_node,error);
    if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(enum_node)) {
        if(!praktor_yaml_is_seq(enum_node)||praktor_yaml_count(enum_node)==0u) {
            status=at_error(enum_node,"enum must be a non-empty sequence",error);goto fail;
        }
        enum_values=json_create_array();
        if(!enum_values){status=SALTS_ENOMEM;goto fail;}
        for(i=0;i<praktor_yaml_count(enum_node);++i) {
            praktor_yaml_node item=praktor_yaml_at(enum_node,i);
            status=praktor_yaml_value(item,&value,error);
            if(status!=SALTS_OK)goto fail;
            if(!praktor_contract_value_matches_type(value,json_string(json_object_get(field,"type")))) {
                tstr message=tstr_format("enum value does not match declared type '{}'",json_string(json_object_get(field,"type")));
                json_free(value);value=NULL;
                if(!message){status=SALTS_ENOMEM;goto fail;}
                status=praktor_yaml_field_fail_at(item,tstr_to_v(message),error);
                tstr_free(message);goto fail;
            }
            if(!json_array_add_checked(enum_values,value)){json_free(value);value=NULL;status=SALTS_ENOMEM;goto fail;}
            value=NULL;
        }
        status=add_owned(field,"enum",enum_values);enum_values=NULL;
        if(status!=SALTS_OK)goto fail;
    }
    *out=field;return SALTS_OK;
fail:
    tstr_free(text);json_free(value);json_free(enum_values);json_free(field);return status;
}
int praktor_yaml_contract_fields_create(praktor_yaml_node node, bool output,
    json_value_t **out, praktor_yaml_field_error *error) {
    json_value_t *result=NULL,*field=NULL;
    size_t i;int status;
    praktor_yaml_field_error_dispose(error);
    if(!out)return SALTS_EINVAL;
    if(!praktor_yaml_is_map(node))
        return at_error(node,output?"'outputs' must be a map":"'inputs' must be a map",error);
    result=json_create_object();
    if(!result) {
        if(error) error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;
        return SALTS_ENOMEM;
    }
    for(i=0;i<praktor_yaml_count(node);++i) {
        praktor_yaml_node child=praktor_yaml_at(node,i);
        tstr key=NULL;
        status=praktor_yaml_field_decode(child,true,&key,error);
        if(status!=SALTS_OK)goto fail;
        if(tstr_len(key)==0u) {
            tstr_free(key);status=at_error(child,"workflow contract field name cannot be empty",error);goto fail;
        }
        status=parse_field(child,output,key,&field,error);
        if(status!=SALTS_OK){tstr_free(key);goto fail;}
        /* Existing std::map::emplace semantics: first duplicate field wins. */
        if(json_object_get(result,key)==NULL) {
            if(!json_object_add_n(result,key,tstr_len(key),field)) {
                tstr_free(key);json_free(field);field=NULL;status=SALTS_ENOMEM;goto fail;
            }
            field=NULL;
        } else {json_free(field);field=NULL;}
        tstr_free(key);
    }
    *out=result;return SALTS_OK;
fail:
    json_free(field);json_free(result);
    if(status==SALTS_ENOMEM && error && error->origin==PRAKTOR_YAML_FAILURE_NONE)
        error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}
