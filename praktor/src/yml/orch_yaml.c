#include "yml/orch_yaml.h"
#include <salts/error_codes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NAMED_YAML_MAX_DEPTH 100u
typedef struct named_row {
    praktor_orch_flat_node node;
    praktor_orch_input_param params[PRAKTOR_ORCH_MAX_PARAMS];
    tstr keys[PRAKTOR_ORCH_MAX_PARAMS],values[PRAKTOR_ORCH_MAX_PARAMS];
} named_row;
typedef struct named_build {
    named_row *rows;
    size_t count;
    char *error;
    size_t capacity;
} named_build;
static int fail(named_build *b,int status,const char *reason) {
    if(b->error&&b->capacity)(void)snprintf(b->error,b->capacity,"%s",reason);
    return status;
}
static void cleanup(named_build *b) {
    size_t i,j;
    for(i=0;i<b->count;++i)
        for(j=0;j<PRAKTOR_ORCH_MAX_PARAMS;++j) {
            tstr_free(b->rows[i].keys[j]);
            tstr_free(b->rows[i].values[j]);
        }
    free(b->rows);
}
static int param(named_build *b,size_t index,tstr key,tstr value) {
    named_row *row=&b->rows[index];
    size_t p=row->node.param_count;
    if(p>=PRAKTOR_ORCH_MAX_PARAMS) {
        tstr_free(key);tstr_free(value);
        return fail(b,SALTS_EINVAL,"Named orchestration parameter limit");
    }
    row->keys[p]=key;row->values[p]=value;
    row->params[p]=(praktor_orch_input_param){tstr_to_v(key),tstr_to_v(value)};
    row->node.param_count=p+1u;
    return SALTS_OK;
}
static int parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth);
static int children(named_build *b,praktor_yaml_node seq,size_t parent,unsigned depth) {
    size_t i;
    if(!praktor_yaml_is_seq(seq))
        return fail(b,SALTS_EINVAL,"Named orchestration children must be a sequence");
    for(i=0;i<praktor_yaml_count(seq);++i) {
        int status=parse(b,praktor_yaml_at(seq,i),parent,depth+1u);
        if(status!=SALTS_OK)return status;
    }
    return SALTS_OK;
}
static int parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth) {
    praktor_yaml_node value;
    praktor_orch_type type=PRAKTOR_ORCH_TYPE_COUNT;
    const praktor_orch_spec *spec;
    tstr type_name=NULL;
    size_t index,i;
    bool saw_child=false;
    int status;
    if(depth>NAMED_YAML_MAX_DEPTH)return fail(b,SALTS_EINVAL,"Named orchestration nesting limit");
    if(!praktor_yaml_is_map(map)||praktor_yaml_count(map)!=1u)
        return fail(b,SALTS_EINVAL,"Named node requires exactly one type key");
    if(b->count>=PRAKTOR_ORCH_MAX_NODES)
        return fail(b,SALTS_EINVAL,"Named orchestration node limit");
    value=praktor_yaml_at(map,0);
    status=praktor_yaml_key(value,&type_name,NULL);
    if(status!=SALTS_OK)return fail(b,status,"Cannot decode orchestration type key");
    status=praktor_orch_find_type(tstr_to_v(type_name),&type);
    tstr_free(type_name);
    if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Unknown named orchestration type");
    spec=praktor_orch_spec_for(type);
    index=b->count++;
    b->rows[index].node.type=type;
    b->rows[index].node.parent_index=parent;
    if(praktor_yaml_is_seq(value)) {
        if(!spec->allows_children)return fail(b,SALTS_EINVAL,"Leaf cannot have children");
        return children(b,value,index,depth);
    }
    if(praktor_yaml_is_map(value)) {
        for(i=0;i<praktor_yaml_count(value);++i) {
            praktor_yaml_node field=praktor_yaml_at(value,i);
            tstr key=NULL;
            status=praktor_yaml_key(field,&key,NULL);
            if(status!=SALTS_OK)return fail(b,status,"Cannot decode orchestration parameter key");
            if(strcmp(key,"child")==0||strcmp(key,"children")==0) {
                bool multiple=strcmp(key,"children")==0;
                tstr_free(key);
                if(saw_child)return fail(b,SALTS_EINVAL,"Duplicate child/children");
                saw_child=true;
                if(!spec->allows_children)return fail(b,SALTS_EINVAL,"Leaf cannot have children");
                status=multiple?children(b,field,index,depth):
                    parse(b,field,index,depth+1u);
                if(status!=SALTS_OK)return status;
            } else {
                tstr text=NULL;
                status=praktor_yaml_scalar(field,&text,NULL);
                if(status!=SALTS_OK) {
                    tstr_free(key);
                    return fail(b,status,"Orchestration parameter must be scalar");
                }
                status=param(b,index,key,text);
                if(status!=SALTS_OK)return status;
            }
        }
        return SALTS_OK;
    }
    {
        const char *required=NULL;
        tstr key=NULL,text=NULL;
        size_t count=0;
        for(i=0;i<sizeof(spec->required)/sizeof(spec->required[0]);++i)
            if(spec->required[i]) {required=spec->required[i];++count;}
        if(count!=1u)return fail(b,SALTS_EINVAL,"Named shorthand requires one parameter");
        status=praktor_yaml_scalar(value,&text,NULL);
        if(status!=SALTS_OK)return fail(b,status,"Named shorthand must be scalar");
        key=tstr_dup(required);
        if(!key) {tstr_free(text);return fail(b,SALTS_ENOMEM,"Cannot allocate param key");}
        return param(b,index,key,text);
    }
}
int praktor_orch_from_named_yaml(praktor_yaml_node node,praktor_orch_tree **out,
                                 char *error,size_t error_capacity) {
    named_build b={0};
    praktor_orch_flat_node *input=NULL;
    size_t i;
    int status;
    if(error&&error_capacity)error[0]='\0';
    b.error=error;b.capacity=error_capacity;
    if(!out||(!error&&error_capacity))
        return fail(&b,SALTS_EINVAL,"Invalid YAML orchestration output");
    b.rows=(named_row *)calloc(PRAKTOR_ORCH_MAX_NODES,sizeof(named_row));
    if(!b.rows)return fail(&b,SALTS_ENOMEM,"Cannot allocate orchestration scratch");
    status=parse(&b,node,PRAKTOR_ORCH_ROOT_PARENT,0u);
    if(status!=SALTS_OK)goto done;
    input=(praktor_orch_flat_node *)malloc(b.count*sizeof(*input));
    if(!input) {
        status=fail(&b,SALTS_ENOMEM,"Cannot allocate flat descriptors");
        goto done;
    }
    for(i=0;i<b.count;++i) {
        input[i]=b.rows[i].node;
        input[i].params=input[i].param_count?b.rows[i].params:NULL;
    }
    status=praktor_orch_from_flat_nodes(input,b.count,out,error,error_capacity);
done:
    free(input);
    cleanup(&b);
    return status;
}
