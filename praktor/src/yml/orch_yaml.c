#include "yml/orch_yaml.h"
#include <ctype.h>
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
static bool runtime_scalar(tstr value) {
    size_t i;
    for(i=0;i<tstr_len(value);++i)
        if(value[i]=='{'||value[i]=='}')return true;
    return false;
}
static bool key_is(tstr key,const char *expected) {
    size_t i=0,j=0;
    while(key[i]&&expected[j]) {
        if(key[i]=='_'){++i;continue;}
        if(expected[j]=='_'){++j;continue;}
        if(tolower((unsigned char)key[i])!=tolower((unsigned char)expected[j]))return false;
        ++i;++j;
    }
    while(key[i]=='_')++i;
    while(expected[j]=='_')++j;
    return key[i]=='\0'&&expected[j]=='\0';
}
static int validate_param(named_build *b,tstr key,tstr value) {
    static const char *const integer_keys[]={"capture_group","expected","timeout",
        "num_attempts","num_cycles","timeout_ms","delay_ms","max_iterations"};
    static const char *const boolean_keys[]={"stream_output","fail_if_missing"};
    size_t i,j,n=tstr_len(value),start=0;
    bool integer=false,boolean=false;
    if(runtime_scalar(value))return SALTS_OK;
    for(i=0;i<sizeof(integer_keys)/sizeof(integer_keys[0]);++i)
        if(key_is(key,integer_keys[i])){integer=true;break;}
    for(i=0;i<sizeof(boolean_keys)/sizeof(boolean_keys[0]);++i)
        if(key_is(key,boolean_keys[i])){boolean=true;break;}
    if(integer) {
        if(n&&(value[0]=='+'||value[0]=='-'))start=1u;
        if(start==n) {
            tstr_free(key);tstr_free(value);
            return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
        }
        for(j=start;j<n;++j)if(value[j]<'0'||value[j]>'9') {
            tstr_free(key);tstr_free(value);
            return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
        }
    }
    if(boolean&&!(n==4u&&tolower((unsigned char)value[0])=='t'&&
       tolower((unsigned char)value[1])=='r'&&tolower((unsigned char)value[2])=='u'&&
       tolower((unsigned char)value[3])=='e')&&
       !(n==5u&&tolower((unsigned char)value[0])=='f'&&
       tolower((unsigned char)value[1])=='a'&&tolower((unsigned char)value[2])=='l'&&
       tolower((unsigned char)value[3])=='s'&&tolower((unsigned char)value[4])=='e')) {
        tstr_free(key);tstr_free(value);
        return fail(b,SALTS_EINVAL,"Orchestration boolean parameter must be true or false");
    }
    return SALTS_OK;
}
static int param(named_build *b,size_t index,tstr key,tstr value) {
    named_row *row=&b->rows[index];
    size_t p=row->node.param_count;
    int validation=validate_param(b,key,value);
    if(validation!=SALTS_OK)return validation;
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
static int flat_parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth,const char *kind,bool allow_metadata);
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
    if(praktor_yaml_is_map(map)) {
        if(praktor_yaml_has(map,"if"))return flat_parse(b,map,parent,depth,"if",false);
        if(praktor_yaml_has(map,"while"))return flat_parse(b,map,parent,depth,"while",false);
        if(praktor_yaml_has(map,"switch"))return flat_parse(b,map,parent,depth,"switch",false);
        if(praktor_yaml_has(map,"set_variable"))return flat_parse(b,map,parent,depth,"set_variable",false);
    }
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

static bool flat_key_matches(const char *actual,const char *expected) {
    size_t a=0,e=0;
    if(!actual||!expected)return false;
    for(;;) {
        while(actual[a]=='_')++a;
        while(expected[e]=='_')++e;
        if(!actual[a]||!expected[e])return actual[a]==expected[e];
        if(tolower((unsigned char)actual[a])!=tolower((unsigned char)expected[e]))return false;
        ++a;++e;
    }
}
static bool flat_find_field(praktor_yaml_node map,const char *name,praktor_yaml_node *out) {
    size_t i;
    if(!praktor_yaml_is_map(map)||!out)return false;
    for(i=0;i<praktor_yaml_count(map);++i) {
        praktor_yaml_node item=praktor_yaml_at(map,i);
        tstr key=NULL;
        bool match;
        if(praktor_yaml_key(item,&key,NULL)!=SALTS_OK)continue;
        match=flat_key_matches(key,name);
        tstr_free(key);
        if(match){*out=item;return true;}
    }
    return false;
}
static bool flat_task_metadata_key(const char *key) {
    static const char *const keys[]={
        "name","description","depends_on","vars","env","dot_env","fallback","selector",
        "when","each","timeout","triggers","working_dir","silent","sources","generates",
        "finally","command","uses","dynamic_tasks","output_format","parse_regex","parse_json",
        "parse_lines","parse_keyvalue","script","actions"
    };
    size_t i;
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i)
        if(flat_key_matches(key,keys[i]))return true;
    return false;
}
static int flat_append_node(named_build *b,praktor_orch_type type,size_t parent,
                            unsigned depth,size_t *index) {
    if(depth>NAMED_YAML_MAX_DEPTH)return fail(b,SALTS_EINVAL,"Orchestration nesting limit");
    if(b->count>=PRAKTOR_ORCH_MAX_NODES)return fail(b,SALTS_EINVAL,"Orchestration node limit");
    *index=b->count++;
    b->rows[*index].node.type=type;
    b->rows[*index].node.parent_index=parent;
    return SALTS_OK;
}
static int flat_add_param(named_build *b,size_t index,const char *key,tstr value) {
    tstr owned_key=tstr_dup(key);
    if(!owned_key) {
        tstr_free(value);
        return fail(b,SALTS_ENOMEM,"Cannot allocate orchestration parameter key");
    }
    return param(b,index,owned_key,value);
}
static int flat_branch(named_build *b,praktor_yaml_node branch,size_t parent,unsigned depth) {
    size_t i,index;
    int status;
    if(praktor_yaml_is_seq(branch)) {
        status=flat_append_node(b,PRAKTOR_ORCH_SEQUENCE,parent,depth,&index);
        if(status!=SALTS_OK)return status;
        for(i=0;i<praktor_yaml_count(branch);++i) {
            status=parse(b,praktor_yaml_at(branch,i),index,depth+1u);
            if(status!=SALTS_OK)return status;
        }
        return SALTS_OK;
    }
    return parse(b,branch,parent,depth+1u);
}
static bool flat_key_allowed(const char *key,const char *kind) {
    if(flat_key_matches(key,"if"))return flat_key_matches(kind,"if");
    if(flat_key_matches(key,"then")||flat_key_matches(key,"else"))return flat_key_matches(kind,"if");
    if(flat_key_matches(key,"while")||flat_key_matches(key,"do")||
       flat_key_matches(key,"max_iterations"))return flat_key_matches(kind,"while");
    if(flat_key_matches(key,"switch")||flat_key_matches(key,"cases"))return flat_key_matches(kind,"switch");
    if(flat_key_matches(key,"set_variable")||flat_key_matches(key,"value")||
       flat_key_matches(key,"from"))return flat_key_matches(kind,"set_variable");
    return false;
}
static int flat_validate_keys(named_build *b,praktor_yaml_node map,const char *kind,
                              bool allow_metadata) {
    size_t i;
    for(i=0;i<praktor_yaml_count(map);++i) {
        tstr key=NULL;
        bool allowed;
        int status=praktor_yaml_key(praktor_yaml_at(map,i),&key,NULL);
        if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Cannot decode flat orchestration key");
        allowed=flat_key_allowed(key,kind)||(allow_metadata&&flat_task_metadata_key(key));
        tstr_free(key);
        if(!allowed)return fail(b,SALTS_EINVAL,"Unknown orchestration parameter in flat control form");
    }
    return SALTS_OK;
}
static int flat_validate_integer(named_build *b,tstr text) {
    size_t i=0,n=tstr_len(text),j;
    /* Preserve runtime-substituted scalar admission: any brace bypasses
     * compile-time decimal validation, exactly as the legacy parser does. */
    for(j=0;j<n;++j)if(text[j]=='{'||text[j]=='}')return SALTS_OK;
    if(n&&(text[0]=='+'||text[0]=='-'))i=1u;
    if(i==n) {
        tstr_free(text);
        return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
    }
    for(;i<n;++i)if(text[i]<'0'||text[i]>'9') {
        tstr_free(text);
        return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
    }
    return SALTS_OK;
}
static int flat_parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth,
                      const char *kind,bool allow_metadata) {
    praktor_orch_type type;
    praktor_yaml_node field={0},branch={0};
    size_t index,i;
    int status;
    if(!praktor_yaml_is_map(map))return fail(b,SALTS_EINVAL,"Flat orchestration form must be a mapping");
    status=flat_validate_keys(b,map,kind,allow_metadata);
    if(status!=SALTS_OK)return status;
    if(flat_key_matches(kind,"if"))type=PRAKTOR_ORCH_IF_THEN_ELSE;
    else if(flat_key_matches(kind,"while"))type=PRAKTOR_ORCH_WHILE_DO;
    else if(flat_key_matches(kind,"switch"))type=PRAKTOR_ORCH_SWITCH;
    else if(flat_key_matches(kind,"set_variable"))type=PRAKTOR_ORCH_SET_VARIABLE;
    else return fail(b,SALTS_EINVAL,"Unsupported flat orchestration form");
    status=flat_append_node(b,type,parent,depth,&index);
    if(status!=SALTS_OK)return status;
    if(type==PRAKTOR_ORCH_IF_THEN_ELSE) {
        if(!flat_find_field(map,"if",&field))return fail(b,SALTS_EINVAL,"if requires an 'if' condition");
        if(praktor_yaml_has_val(field)) {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration if condition must be scalar, node, or sequence");
            status=flat_add_param(b,index,"condition",value);
            if(status!=SALTS_OK)return status;
        } else {
            status=flat_branch(b,field,index,depth);
            if(status!=SALTS_OK)return status;
        }
        if(!flat_find_field(map,"then",&branch))return fail(b,SALTS_EINVAL,"if requires a then branch");
        status=flat_branch(b,branch,index,depth);
        if(status!=SALTS_OK)return status;
        if(flat_find_field(map,"else",&branch))return flat_branch(b,branch,index,depth);
        return SALTS_OK;
    }
    if(type==PRAKTOR_ORCH_WHILE_DO) {
        if(!flat_find_field(map,"while",&field))return fail(b,SALTS_EINVAL,"while requires a 'while' condition");
        if(praktor_yaml_has_val(field)) {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration while condition must be scalar, node, or sequence");
            status=flat_add_param(b,index,"condition",value);
            if(status!=SALTS_OK)return status;
        } else {
            status=flat_branch(b,field,index,depth);
            if(status!=SALTS_OK)return status;
        }
        if(flat_find_field(map,"max_iterations",&field)) {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration max_iterations must be an integer scalar");
            status=flat_validate_integer(b,value);
            if(status!=SALTS_OK)return status;
            status=flat_add_param(b,index,"max_iterations",value);
            if(status!=SALTS_OK)return status;
        }
        if(!flat_find_field(map,"do",&branch))return fail(b,SALTS_EINVAL,"while requires a do branch");
        return flat_branch(b,branch,index,depth);
    }
    if(type==PRAKTOR_ORCH_SWITCH) {
        tstr value=NULL;
        if(!flat_find_field(map,"switch",&field))return fail(b,SALTS_EINVAL,"switch requires a selector");
        status=praktor_yaml_scalar(field,&value,NULL);
        if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration switch selector must be scalar");
        status=flat_add_param(b,index,"variable",value);
        if(status!=SALTS_OK)return status;
        if(!flat_find_field(map,"cases",&branch))return fail(b,SALTS_EINVAL,"switch requires a cases branch");
        if(!praktor_yaml_is_seq(branch))return fail(b,SALTS_EINVAL,"switch cases must be a sequence");
        for(i=0;i<praktor_yaml_count(branch);++i) {
            status=flat_branch(b,praktor_yaml_at(branch,i),index,depth);
            if(status!=SALTS_OK)return status;
        }
        return SALTS_OK;
    }
    if(!flat_find_field(map,"set_variable",&field))return fail(b,SALTS_EINVAL,"set_variable requires a key");
    {
        tstr value=NULL;
        status=praktor_yaml_scalar(field,&value,NULL);
        if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"set_variable key must be scalar");
        status=flat_add_param(b,index,"key",value);
        if(status!=SALTS_OK)return status;
    }
    {
        praktor_yaml_node value_node={0},from_node={0};
        bool has_value=flat_find_field(map,"value",&value_node);
        bool has_from=flat_find_field(map,"from",&from_node);
        if(has_value==has_from)return fail(b,SALTS_EINVAL,"set_variable requires exactly one of value or from");
        field=has_value?value_node:from_node;
        {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"set_variable value/from must be scalar");
            return flat_add_param(b,index,has_value?"value":"from",value);
        }
    }
}
static int flat_publish(named_build *b,praktor_orch_tree **out) {
    praktor_orch_flat_node *input=NULL;
    size_t i;
    int status;
    if(!b->count)return fail(b,SALTS_EINVAL,"Empty orchestration tree");
    input=(praktor_orch_flat_node *)malloc(b->count*sizeof(*input));
    if(!input)return fail(b,SALTS_ENOMEM,"Cannot allocate flat descriptors");
    for(i=0;i<b->count;++i) {
        input[i]=b->rows[i].node;
        input[i].params=input[i].param_count?b->rows[i].params:NULL;
    }
    status=praktor_orch_from_flat_nodes(input,b->count,out,b->error,b->capacity);
    free(input);
    return status;
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

int praktor_orch_from_flat_yaml(praktor_yaml_node node,const char *node_type,
                                praktor_orch_tree **out,char *error,size_t error_capacity) {
    named_build b={0};
    int status;
    if(error&&error_capacity)error[0]='\0';
    b.error=error;b.capacity=error_capacity;
    if(!out||(!error&&error_capacity)||!node_type)
        return fail(&b,SALTS_EINVAL,"Invalid flat orchestration input or output");
    b.rows=(named_row *)calloc(PRAKTOR_ORCH_MAX_NODES,sizeof(named_row));
    if(!b.rows)return fail(&b,SALTS_ENOMEM,"Cannot allocate orchestration scratch");
    status=flat_parse(&b,node,PRAKTOR_ORCH_ROOT_PARENT,0u,node_type,true);
    if(status==SALTS_OK)status=flat_publish(&b,out);
    cleanup(&b);
    return status;
}
