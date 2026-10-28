#include "yml/orch_tree.h"
#include <ctype.h>
#include <fmt.h>
#include <salts/error_codes.h>
#include <stdlib.h>
#include <string.h>

#define ORCH_PARAMS(...) {__VA_ARGS__, NULL}
static const praktor_orch_spec catalog[] = {
#define PRAKTOR_ORCH_NODE(id, name, control, children, required, optional) {name, control, children, required, optional},
#include "yml/orch_nodes.def"
#undef PRAKTOR_ORCH_NODE
};
#undef ORCH_PARAMS
_Static_assert(sizeof(catalog) / sizeof(catalog[0]) == PRAKTOR_ORCH_TYPE_COUNT, "Orchestration catalog enum mismatch");
const praktor_orch_spec *praktor_orch_spec_for(praktor_orch_type type) {
    return type >= 0 && type < PRAKTOR_ORCH_TYPE_COUNT ? &catalog[type] : NULL;
}
static bool orch_name_is(vstr name, const char *canonical) {
    size_t position = 0;
    while (name.len) {
        unsigned char byte = (unsigned char)*name.data++;
        --name.len;
        if (byte == '_') continue;
        while (canonical[position] == '_') ++position;
        if (!canonical[position] || tolower(byte) != tolower((unsigned char)canonical[position++])) return false;
    }
    while (canonical[position] == '_') ++position;
    return !canonical[position];
}
int praktor_orch_find_type(vstr name, praktor_orch_type *out) {
    size_t index;
    if (!out || !vstr_is_valid(name)) return SALTS_EINVAL;
    for (index = 0; index < PRAKTOR_ORCH_TYPE_COUNT; ++index) if (orch_name_is(name, catalog[index].name)) {
        *out = (praktor_orch_type)index; return SALTS_OK;
    }
    return SALTS_ENOENT;
}

typedef struct orch_owned_param { const char *key; tstr value; } orch_owned_param;
typedef struct orch_param_items {
    orch_owned_param *values;
    size_t count, capacity;
} orch_param_items;
typedef struct orch_node_items {
    struct praktor_orch_node **values;
    size_t count, capacity;
} orch_node_items;
struct praktor_orch_node {
    praktor_orch_type type;
    orch_param_items params;
    orch_node_items children;
};
/* Canonical Orch owns node pointers; child edges borrow them. No generic STL
 * provider or second container engine survives this private C11 representation. */
struct praktor_orch_tree { orch_node_items nodes; size_t retained_bytes; };

static bool orch_reserve(void **storage,size_t *capacity,
                         size_t need,size_t element,size_t limit) {
    size_t next;
    void *fresh;
    if (need>limit || !element || need>SIZE_MAX/element) return false;
    if (need<=*capacity) return true;
    next=*capacity?*capacity:4u;
    while (next<need) {
        if (next>limit/2u) { next=limit; break; }
        next*=2u;
    }
    if (next>limit) next=limit;
    if (next<need || next>SIZE_MAX/element) return false;
    fresh=realloc(*storage,next*element);
    if (!fresh) return false;
    *storage=fresh;
    *capacity=next;
    return true;
}
static bool orch_param_append(orch_param_items *items,orch_owned_param value) {
    if (!orch_reserve((void **)&items->values,&items->capacity,
                      items->count+1u,sizeof(*items->values),
                      PRAKTOR_ORCH_MAX_PARAMS)) return false;
    items->values[items->count++]=value;
    return true;
}
static bool orch_node_append(orch_node_items *items,
                             struct praktor_orch_node *value,size_t limit) {
    if (!orch_reserve((void **)&items->values,&items->capacity,
                      items->count+1u,sizeof(*items->values),limit))
        return false;
    items->values[items->count++]=value;
    return true;
}
typedef struct orch_build { praktor_orch_tree *tree; char *error; size_t capacity; } orch_build;
static int orch_fail(orch_build *build, int status, const char *message) {
    if (build->error && build->capacity) (void)fmt(build->error, build->capacity, "{}", message);
    return status;
}
static int orch_oom(orch_build *build) { return orch_fail(build, SALTS_ENOMEM, "Failed to construct orchestration tree"); }
static void orch_node_destroy(praktor_orch_node *node) {
    size_t index;
    if (!node) return;
    for (index = 0; index < node->params.count; ++index)
        tstr_free(node->params.values[index].value);
    free(node->params.values); free(node->children.values); free(node);
}
void praktor_orch_tree_destroy(praktor_orch_tree *tree) {
    size_t index;
    if (!tree) return;
    for (index = 0; index < tree->nodes.count; ++index)
        orch_node_destroy(tree->nodes.values[index]);
    free(tree->nodes.values); free(tree);
}
static int orch_add_node(orch_build *build, praktor_orch_type type, praktor_orch_node **out) {
    praktor_orch_node *node;
    if (build->tree->nodes.count == PRAKTOR_ORCH_MAX_NODES)
        return orch_fail(build, SALTS_EINVAL, "Orchestration tree exceeds maximum node count");
    node = (praktor_orch_node *)calloc(1, sizeof(*node));
    if (!node) return orch_oom(build);
    node->type = type;
    if (!orch_node_append(&build->tree->nodes,node,PRAKTOR_ORCH_MAX_NODES)) { orch_node_destroy(node); return orch_oom(build); }
    *out = node; return SALTS_OK;
}
static int orch_add_param(orch_build *build, praktor_orch_node *node, const char *key, vstr value) {
    orch_owned_param param = {key, NULL};
    if (!vstr_is_valid(value)) return orch_fail(build, SALTS_EINVAL, "Invalid orchestration parameter view");
    if (value.len >= PRAKTOR_ORCH_MAX_BYTES - build->tree->retained_bytes)
        return orch_fail(build, SALTS_EINVAL, "Orchestration retained text exceeds maximum of 1048576 bytes");
    if (node->params.count == PRAKTOR_ORCH_MAX_PARAMS)
        return orch_fail(build, SALTS_EINVAL, "Orchestration node exceeds maximum parameter count");
    param.value = tstr_from_v(value);
    if (!param.value) return orch_oom(build);
    if (!orch_param_append(&node->params,param)) { tstr_free(param.value); return orch_oom(build); }
    build->tree->retained_bytes += value.len + 1u; return SALTS_OK;
}
static int orch_literal(orch_build *build, praktor_orch_node *node, const char *key, const char *value) {
    return orch_add_param(build, node, key, vstr_from_cstr(value));
}
static int orch_append(orch_build *build, praktor_orch_node *parent, praktor_orch_node *child) {
    return orch_node_append(&parent->children,child,PRAKTOR_ORCH_MAX_COMMANDS+1u) ? SALTS_OK : orch_oom(build);
}
static int orch_postprocessor(orch_build *build, const praktor_orch_command *input, praktor_orch_node *root) {
    praktor_orch_node *node = NULL;
    praktor_orch_type type;
    int status;
    if (input->regex.present) type = PRAKTOR_ORCH_PARSE_REGEX;
    else if (input->json.present) type = PRAKTOR_ORCH_PARSE_JSON;
    else if (input->lines.present) type = PRAKTOR_ORCH_PARSE_LINES;
    else if (input->keyvalue.present) type = PRAKTOR_ORCH_PARSE_KEY_VALUE;
    else if (input->output_format == PRAKTOR_YAML_OUTPUT_JSON) type = PRAKTOR_ORCH_PARSE_JSON;
    else return SALTS_OK;
    status = orch_add_node(build, type, &node);
    if (status != SALTS_OK) return status;
    status = orch_literal(build, node, "input_key", "stdout");
    if (status != SALTS_OK) return status;
    if (input->regex.present) {
        tstr capture;
        status = orch_add_param(build, node, "pattern", input->regex.pattern);
        if (status != SALTS_OK) return status;
        capture = tstr_format("{}", input->regex.capture_group);
        if (!capture) return orch_oom(build);
        status = orch_add_param(build, node, "capture_group", tstr_to_v(capture));
        tstr_free(capture);
        if (status == SALTS_OK) status = orch_add_param(build, node, "output_key", input->regex.output_key);
    } else if (input->json.present) {
        status = orch_add_param(build, node, "path", input->json.path);
        if (status == SALTS_OK) status = orch_add_param(build, node, "output_key", input->json.output_key);
    } else if (input->lines.present) {
        if (input->lines.filter.len) status = orch_add_param(build, node, "filter", input->lines.filter);
        if (status == SALTS_OK) status = orch_add_param(build, node, "output_key", input->lines.output_key);
    } else if (input->keyvalue.present) {
        status = orch_add_param(build, node, "delimiter", input->keyvalue.delimiter);
        if (status == SALTS_OK) status = orch_add_param(build, node, "line_separator", input->keyvalue.line_separator);
        if (status == SALTS_OK) status = orch_add_param(build, node, "output_key", input->keyvalue.output_key);
    } else {
        status = orch_literal(build, node, "path", "$");
        if (status == SALTS_OK) status = orch_literal(build, node, "output_key", "data");
    }
    return status == SALTS_OK ? orch_append(build, root, node) : status;
}
int praktor_orch_from_command(const praktor_orch_command *input, praktor_orch_tree **out, char *error, size_t error_capacity) {
    orch_build build = {NULL, error, error_capacity};
    praktor_orch_node *root = NULL;
    size_t index, count;
    int status;
    if (error && error_capacity) error[0] = '\0';
    if (!input || !out || (!error && error_capacity)) return orch_fail(&build, SALTS_EINVAL, "Invalid orchestration constructor argument");
    if (input->sequence && (input->command_count > PRAKTOR_ORCH_MAX_COMMANDS || (!input->commands && input->command_count)))
        return orch_fail(&build, SALTS_EINVAL, "Invalid orchestration command list or maximum of 4096 exceeded");
    build.tree = (praktor_orch_tree *)calloc(1, sizeof(*build.tree));
    if (!build.tree) return orch_oom(&build);
    status = orch_add_node(&build, PRAKTOR_ORCH_SEQUENCE, &root);
    count = input->sequence ? input->command_count : 1u;
    for (index = 0; status == SALTS_OK && index < count; ++index) {
        praktor_orch_node *child = NULL;
        status = orch_add_node(&build, PRAKTOR_ORCH_SHELL, &child);
        if (status == SALTS_OK) status = orch_add_param(&build, child, "cmd", input->sequence ? input->commands[index] : input->command);
        if (status == SALTS_OK && index == count - 1u) status = orch_literal(&build, child, "output_key", "stdout");
        if (status == SALTS_OK) status = orch_append(&build, root, child);
    }
    if (status == SALTS_OK) status = orch_postprocessor(&build, input, root);
    if (status != SALTS_OK) { praktor_orch_tree_destroy(build.tree); return status; }
    *out = build.tree; return SALTS_OK;
}

static const char *orch_param_key(const praktor_orch_spec *spec, vstr key) {
    size_t i;
    for(i=0;i<sizeof(spec->required)/sizeof(spec->required[0]);++i)
        if(spec->required[i]&&orch_name_is(key,spec->required[i])) return spec->required[i];
    for(i=0;i<sizeof(spec->optional)/sizeof(spec->optional[0]);++i)
        if(spec->optional[i]&&orch_name_is(key,spec->optional[i])) return spec->optional[i];
    return NULL;
}
static bool orch_has_param(const praktor_orch_node *node,const char *key) {
    size_t i;
    for(i=0;i<node->params.count;++i) {
        const orch_owned_param *param=&node->params.values[i];
        if(strcmp(param->key,key)==0) return true;
    }
    return false;
}
static bool orch_single_child(praktor_orch_type type) {
    switch(type) {
        case PRAKTOR_ORCH_INVERTER: case PRAKTOR_ORCH_FORCE_SUCCESS:
        case PRAKTOR_ORCH_FORCE_FAILURE: case PRAKTOR_ORCH_REPEAT:
        case PRAKTOR_ORCH_TIMEOUT: case PRAKTOR_ORCH_DELAY:
        case PRAKTOR_ORCH_KEEP_RUNNING_UNTIL_FAILURE: case PRAKTOR_ORCH_RUN_ONCE:
        case PRAKTOR_ORCH_CONSUME_QUEUE: case PRAKTOR_ORCH_PRECONDITION:
        case PRAKTOR_ORCH_ENTRY_UPDATED: return true;
        default: return false;
    }
}
int praktor_orch_from_flat_nodes(const praktor_orch_flat_node *inputs,size_t count,
    praktor_orch_tree **out,char *error,size_t error_capacity) {
    orch_build build={NULL,error,error_capacity};
    size_t i,j;
    int status=SALTS_OK;
    if(error&&error_capacity) error[0]='\0';
    if(!inputs||!count||!out||(!error&&error_capacity)||count>PRAKTOR_ORCH_MAX_NODES)
        return orch_fail(&build,SALTS_EINVAL,"Invalid orchestration flat-node input or node limit");
    build.tree=(praktor_orch_tree *)calloc(1,sizeof(*build.tree));
    if(!build.tree) return orch_oom(&build);
    for(i=0;i<count;++i) {
        const praktor_orch_flat_node *input=&inputs[i];
        const praktor_orch_spec *spec=praktor_orch_spec_for(input->type);
        praktor_orch_node *node=NULL;
        if(!spec||(i==0?input->parent_index!=PRAKTOR_ORCH_ROOT_PARENT:
            input->parent_index>=i)||input->param_count>PRAKTOR_ORCH_MAX_PARAMS||
            (input->param_count&&!input->params)) {
            status=orch_fail(&build,SALTS_EINVAL,"Invalid orchestration node type, parent or params");
            break;
        }
        status=orch_add_node(&build,input->type,&node);
        if(status!=SALTS_OK) break;
        for(j=0;j<input->param_count;++j) {
            const praktor_orch_input_param *param=&input->params[j];
            const char *key;
            if(!vstr_is_valid(param->key)||!vstr_is_valid(param->value)) {
                status=orch_fail(&build,SALTS_EINVAL,"Invalid orchestration parameter view");break;
            }
            key=orch_param_key(spec,param->key);
            if(!key) {status=orch_fail(&build,SALTS_EINVAL,"Unknown orchestration parameter");break;}
            if(orch_has_param(node,key)) {
                status=orch_fail(&build,SALTS_EINVAL,"Duplicate orchestration parameter");break;
            }
            status=orch_add_param(&build,node,key,param->value);
            if(status!=SALTS_OK) break;
        }
        if(status!=SALTS_OK) break;
        if(i>0) {
            praktor_orch_node *parent=build.tree->nodes.values[input->parent_index];
            if(!praktor_orch_spec_for(parent->type)->allows_children) {
                status=orch_fail(&build,SALTS_EINVAL,"Leaf orchestration node cannot have children");break;
            }
            status=orch_append(&build,parent,node);
            if(status!=SALTS_OK) break;
        }
    }
    for(i=0;status==SALTS_OK&&i<count;++i) {
        const praktor_orch_node *node=build.tree->nodes.values[i];
        const praktor_orch_spec *spec=praktor_orch_spec_for(node->type);
        size_t children=node->children.count;
        for(j=0;j<sizeof(spec->required)/sizeof(spec->required[0]);++j)
            if(spec->required[j]&&!orch_has_param(node,spec->required[j])) {
                status=orch_fail(&build,SALTS_EINVAL,"Missing required orchestration parameter");break;
            }
        if(status!=SALTS_OK)break;
        /* Condition is either a scalar parameter or a leading child.  These
         * cardinalities are part of the legacy parser's AST contract, not
         * optional execution-time validation. */
        if(node->type==PRAKTOR_ORCH_IF_THEN_ELSE) {
            const bool scalar_condition=orch_has_param(node,"condition");
            const size_t minimum=scalar_condition?1u:2u;
            const size_t maximum=scalar_condition?2u:3u;
            if(children<minimum||children>maximum)
                status=orch_fail(&build,SALTS_EINVAL,"if requires a condition plus then/else branches");
        } else if(node->type==PRAKTOR_ORCH_WHILE_DO) {
            const bool scalar_condition=orch_has_param(node,"condition");
            if(children!=(scalar_condition?1u:2u))
                status=orch_fail(&build,SALTS_EINVAL,"while requires a condition and a do branch");
        } else if((spec->control&&!children)||(orch_single_child(node->type)&&children!=1u))
            status=orch_fail(&build,SALTS_EINVAL,"Invalid orchestration control child count");
        if(status!=SALTS_OK)break;
    }
    if(status!=SALTS_OK) {praktor_orch_tree_destroy(build.tree);return status;}
    *out=build.tree;
    return SALTS_OK;
}

const praktor_orch_node *praktor_orch_tree_root(const praktor_orch_tree *tree) {
    return tree && tree->nodes.count ? tree->nodes.values[0] : NULL;
}
size_t praktor_orch_tree_size(const praktor_orch_tree *tree) { return tree ? tree->nodes.count : 0u; }
praktor_orch_type praktor_orch_node_type(const praktor_orch_node *node) { return node ? node->type : PRAKTOR_ORCH_TYPE_COUNT; }
size_t praktor_orch_child_count(const praktor_orch_node *node) { return node ? node->children.count : 0u; }
size_t praktor_orch_param_count(const praktor_orch_node *node) { return node ? node->params.count : 0u; }
int praktor_orch_child_at(const praktor_orch_node *node, size_t index, const praktor_orch_node **out) {
    if (!node || !out) return SALTS_EINVAL;
    if (index >= node->children.count) return SALTS_ENOENT;
    *out = node->children.values[index]; return SALTS_OK;
}
int praktor_orch_param_at(const praktor_orch_node *node, size_t index, praktor_orch_param *out) {
    const orch_owned_param *param;
    if (!node || !out) return SALTS_EINVAL;
    if (index >= node->params.count) return SALTS_ENOENT;
    param = &node->params.values[index];
    *out = (praktor_orch_param){vstr_from_cstr(param->key), tstr_to_v(param->value)}; return SALTS_OK;
}
