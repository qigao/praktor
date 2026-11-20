#include "actions/condition.h"
#include "actions/variable_reference.h"
#include <core/error_codes.h>
#include <stdio.h>
#include <string.h>

static size_t parameter_size(const praktor_session_node *node,
                             const praktor_leaf_parameter *parameter) {
    return node->text_sizes?node->text_sizes[parameter-node->parameters]:
           strlen(parameter->text);
}

static int invalid(char *error,size_t capacity,const char *reason) {
    if(error&&capacity)(void)snprintf(error,capacity,"%s",reason);
    return SALTS_EINVAL;
}

static bool selector_keyword(vstr word) {
    return vstr_eq(word,vstr_from_cstr("has"))||vstr_eq(word,vstr_from_cstr("capability"))||
           vstr_eq(word,vstr_from_cstr("in"))||vstr_eq(word,vstr_from_cstr("not"));
}

/* Selector consumes bare field tokens. Strip only the DSL sigil and keep
 * source offsets, quoted strings, and Selector's operators intact. */
static int selector_source(vstr source,char *text,char *error,size_t capacity) {
    bool quoted=false;
    memcpy(text,source.data,source.len);
    text[source.len]='\0';
    for(size_t i=0u;i<source.len;) {
        const char ch=source.data[i++];
        if(ch=='"')quoted=!quoted;
        else if(quoted) {
            if(ch=='\\'&&i<source.len)++i;
        } else if(ch=='$') {
            vstr name={source.data+i,source.len-i};
            name.len=praktor_variable_name_size(name,true);
            if(!name.len||praktor_retired_variable_namespace(name)||selector_keyword(name))
                return invalid(error,capacity,"guard match requires a $name reference");
            text[i-1u]=' ';
            i+=name.len;
        } else if(praktor_identifier_start(ch)) {
            vstr word={source.data+i-1u,source.len-i+1u};
            word.len=praktor_variable_name_size(word,true);
            if(!selector_keyword(word))
                return invalid(error,capacity,"guard match requires $name for field references");
            i+=word.len-1u;
        }
    }
    return SALTS_OK;
}

static int compile_match(vstr source,const vstr *fields,size_t count,
                         selector_program_t **out,char *error,size_t capacity) {
    const char *names[SALTS_SELECTOR_MAX_SCHEMA_FIELDS_V1];
    char storage[SALTS_SELECTOR_MAX_SCHEMA_FIELDS_V1 *
                 (SALTS_SELECTOR_MAX_STRING_BYTES_V1+1u)];
    char text[SALTS_SELECTOR_MAX_SOURCE_BYTES_V1+1u];
    char *name=storage;
    for(size_t i=0u;i<count;++i) {
        memcpy(name,fields[i].data,fields[i].len);
        name[fields[i].len]='\0';
        names[i]=name;
        name+=fields[i].len+1u;
    }
    const selector_schema_v1_t schema={SALTS_SELECTOR_SCHEMA_V1_SIZE,names,count,0};
    selector_diagnostic_v1_t detail={.size=SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE};
    if(source.len>=sizeof text)return SALTS_ENOBUFS;
    const int rc=selector_source(source,text,error,capacity);
    if(rc!=SALTS_OK)return rc;
    const int status=selector_compile_v1(text,source.len,&schema,out,&detail);
    if(status==SALTS_SELECTOR_OK)return SALTS_OK;
    if(error&&capacity)(void)snprintf(error,capacity,"guard match at %u:%u: %s",
                                     detail.line,detail.column,detail.message);
    if(status==SALTS_SELECTOR_NO_MEMORY)return SALTS_ENOMEM;
    if(status==SALTS_SELECTOR_RESOURCE_LIMIT)return SALTS_ENOBUFS;
    return SALTS_EINVAL;
}

int praktor_condition_compile(const praktor_session_node *node,
                              praktor_condition *out,char *error,size_t capacity) {
    const praktor_leaf_parameter *source=NULL,*fields=NULL,*kind=NULL,*actual=NULL;
    vstr names[SALTS_SELECTOR_MAX_SCHEMA_FIELDS_V1];
    size_t count=0u,start=0u;
    for(size_t i=0u;i<node->parameter_count;++i) {
        const praktor_leaf_parameter *parameter=&node->parameters[i];
        if(strcmp(parameter->name,"condition")==0)source=parameter;
        else if(strcmp(parameter->name,"guard_fields")==0)fields=parameter;
        else if(strcmp(parameter->name,"guard_kind")==0)kind=parameter;
        else if(node->guard_leaf==PRAKTOR_GUARD_LEAF_ASSERT) {
            if(strcmp(parameter->name,"actual")==0)actual=parameter;
            if(strcmp(parameter->name,"message")==0&&
               (parameter->kind!=PRAKTOR_LEAF_STRING||
                !parameter_size(node,parameter)))
                return invalid(error,capacity,"assert message must be a nonempty string");
        }
    }
    if(!fields&&!kind&&node->guard_leaf==PRAKTOR_GUARD_LEAF_NONE)return SALTS_OK;
    const bool guard_leaf=node->tree.kind==SALTS_BT_NODE_LEAF&&
        node->guard_leaf!=PRAKTOR_GUARD_LEAF_NONE;
    if((!node->tree.condition_callback&&!guard_leaf)||!source||!fields||
       source->kind!=PRAKTOR_LEAF_STRING||fields->kind!=PRAKTOR_LEAF_STRING)
        return invalid(error,capacity,"guard requires a string condition and fields");
    if(kind&&(kind->kind!=PRAKTOR_LEAF_STRING||
       !vstr_eq(vstr_from_buf(kind->text,parameter_size(node,kind)),vstr_from_cstr("expr"))))
        return invalid(error,capacity,"Unknown guard kind");
    const size_t length=parameter_size(node,fields);
    if((!length&&!kind)||memchr(fields->text,'\0',length))
        return invalid(error,capacity,"Invalid guard field list");
    for(size_t i=0u;length&&i<=length;++i) {
        if(i!=length&&fields->text[i]!='\n')continue;
        if(count==SALTS_SELECTOR_MAX_SCHEMA_FIELDS_V1||i==start||
           i-start>SALTS_SELECTOR_MAX_STRING_BYTES_V1)
            return invalid(error,capacity,"Invalid guard field list");
        names[count++]=vstr_from_buf(fields->text+start,i-start);
        start=i+1u;
    }
    if(actual) {
        if(actual->kind!=PRAKTOR_LEAF_STRING)
            return invalid(error,capacity,"assert actual must be a $name reference");
        const vstr name=praktor_variable_reference(
            vstr_from_buf(actual->text,parameter_size(node,actual)));
        if(!name.data)return invalid(error,capacity,"assert actual must be a $name reference");
        bool declared=false;
        for(size_t i=0u;i<count;++i)
            if(vstr_eq(names[i],name)) { declared=true; break; }
        if(!declared)return invalid(error,capacity,"assert actual must name a field declared in the guard");
    }
    vstr text=vstr_from_buf(source->text,parameter_size(node,source));
    if(kind)return praktor_condition_expr_compile(text,names,count,&out->expr,error,capacity);
    return compile_match(text,names,count,&out->match,error,capacity);
}

void praktor_condition_destroy(praktor_condition *condition) {
    selector_program_destroy(condition->match);
    praktor_condition_expr_destroy(condition->expr);
    *condition=(praktor_condition){0};
}
