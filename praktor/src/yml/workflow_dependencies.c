#include "yml/workflow_dependencies.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct dependency_name {
    char *bytes;
    size_t length, first_outgoing;
} dependency_name;
typedef struct dependency_edge {
    size_t predecessor, dependent, next_outgoing;
} dependency_edge;
struct praktor_workflow_dependencies {
    dependency_name *tasks;
    dependency_edge *edges;
    size_t *name_slots;
    size_t task_count, edge_count, edge_capacity, slot_count;
};
static int fail(char *error,size_t capacity,int status,const char *reason) {
    if(error&&capacity)(void)snprintf(error,capacity,"%s",reason);
    return status;
}
static int formatted_error(char *error,size_t capacity,const char *format,
                           const char *first,const char *second,
                           const char *third,const char *fourth) {
    if(error&&capacity)
        (void)snprintf(error,capacity,format,first,second,third,fourth);
    return SALTS_EINVAL;
}
static uint64_t name_hash(const char *name,size_t length) {
    uint64_t result=UINT64_C(14695981039346656037);
    size_t i;
    for(i=0;i<length;++i)
        result=(result^(unsigned char)name[i])*UINT64_C(1099511628211);
    return result;
}
static size_t find_slot(const praktor_workflow_dependencies *graph,
                        const char *name,size_t length) {
    size_t index=(size_t)(name_hash(name,length)&(graph->slot_count-1u));
    while(graph->name_slots[index]!=SIZE_MAX) {
        const dependency_name *existing=&graph->tasks[graph->name_slots[index]];
        if(existing->length==length&&memcmp(existing->bytes,name,length)==0)
            break;
        index=(index+1u)&(graph->slot_count-1u);
    }
    return index;
}
static size_t name_index(const praktor_workflow_dependencies *graph,
                         const char *name,size_t length) {
    const size_t slot=find_slot(graph,name,length);
    return graph->name_slots[slot];
}
static bool has_dynamic_marker(const char *name,size_t length) {
    size_t i;
    for(i=1;i<length;++i)
        if((name[i-1]=='{'&&name[i]=='{')||
           (name[i-1]=='}'&&name[i]=='}'))return true;
    return false;
}
static int check_trigger_name(const praktor_workflow_dependencies *graph,
                              size_t task,const char *category,
                              const char *target,size_t length,
                              char *error,size_t capacity) {
    if(!length||has_dynamic_marker(target,length))return SALTS_OK;
    if(name_index(graph,target,length)!=SIZE_MAX)return SALTS_OK;
    return formatted_error(error,capacity,
        "Task '%s' trigger '%s' references unknown task '%s'%s",
        graph->tasks[task].bytes,category,target,"");
}
static int check_trigger_list(const praktor_workflow_dependencies *graph,
                              size_t task,const json_value_t *triggers,
                              const char *category,char *error,size_t capacity) {
    const json_value_t *list=triggers?json_object_get(triggers,category):NULL;
    size_t i;
    if(!list)return SALTS_OK;
    for(i=0;i<json_array_size(list);++i) {
        const json_value_t *value=json_array_get(list,i);
        const char *name=json_string(value);
        const size_t length=json_string_len(value);
        int status;
        if(!name)return fail(error,capacity,SALTS_EINVAL,
            "Invalid native workflow trigger metadata");
        status=check_trigger_name(graph,task,category,name,length,error,capacity);
        if(status!=SALTS_OK)return status;
    }
    return SALTS_OK;
}
static int append_edge(praktor_workflow_dependencies *graph,size_t predecessor,
                       size_t dependent,char *error,size_t capacity) {
    if(graph->edge_count==PRAKTOR_WORKFLOW_MAX_EDGES)
        return fail(error,capacity,SALTS_EINVAL,
            "Workflow dependency edge limit exceeded");
    if(graph->edge_count==graph->edge_capacity) {
        size_t new_capacity=graph->edge_capacity?graph->edge_capacity*2u:8u;
        dependency_edge *resized;
        if(new_capacity>PRAKTOR_WORKFLOW_MAX_EDGES)
            new_capacity=PRAKTOR_WORKFLOW_MAX_EDGES;
        resized=(dependency_edge *)realloc(graph->edges,new_capacity*sizeof(*resized));
        if(!resized)return fail(error,capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow edges");
        graph->edges=resized;
        graph->edge_capacity=new_capacity;
    }
    graph->edges[graph->edge_count]=(dependency_edge){
        predecessor,dependent,graph->tasks[predecessor].first_outgoing
    };
    graph->tasks[predecessor].first_outgoing=graph->edge_count++;
    return SALTS_OK;
}
static int check_cycles(const praktor_workflow_dependencies *graph,
                        char *error,size_t capacity) {
    size_t *degrees=(size_t *)calloc(graph->task_count,sizeof(*degrees));
    size_t *queue=(size_t *)malloc(graph->task_count*sizeof(*queue));
    size_t i,front=0,back=0,visited=0;
    if(!degrees||!queue) {
        free(degrees);free(queue);
        return fail(error,capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow cycle scratch");
    }
    for(i=0;i<graph->edge_count;++i)
        ++degrees[graph->edges[i].dependent];
    for(i=0;i<graph->task_count;++i)
        if(!degrees[i])queue[back++]=i;
    while(front<back) {
        const size_t task=queue[front++];
        size_t edge=graph->tasks[task].first_outgoing;
        ++visited;
        while(edge!=SIZE_MAX) {
            const size_t dependent=graph->edges[edge].dependent;
            if(--degrees[dependent]==0u)queue[back++]=dependent;
            edge=graph->edges[edge].next_outgoing;
        }
    }
    free(degrees);free(queue);
    return visited==graph->task_count?SALTS_OK:
        fail(error,capacity,SALTS_EINVAL,"Cycle detected in workflow dependencies");
}
void praktor_workflow_dependencies_destroy(praktor_workflow_dependencies *graph) {
    size_t i;
    if(!graph)return;
    if(graph->tasks)
        for(i=0;i<graph->task_count;++i)free(graph->tasks[i].bytes);
    free(graph->tasks);free(graph->edges);free(graph->name_slots);free(graph);
}
int praktor_workflow_dependencies_create(const json_value_t *const *task_metadata,
    size_t count,praktor_workflow_dependencies **out,
    char *error,size_t error_capacity) {
    praktor_workflow_dependencies *graph=NULL;
    size_t i,j,slot,name_bytes=0;
    int status=SALTS_OK;
    if(error&&error_capacity)error[0]='\0';
    if(!task_metadata||!out||(!error&&error_capacity)||
       !count||count>PRAKTOR_WORKFLOW_MAX_TASKS)
        return fail(error,error_capacity,SALTS_EINVAL,
            "Invalid native workflow task metadata or task limit");
    graph=(praktor_workflow_dependencies *)calloc(1,sizeof(*graph));
    if(!graph)return fail(error,error_capacity,SALTS_ENOMEM,
        "Cannot allocate native workflow projection");
    graph->task_count=count;
    graph->slot_count=8u;
    while(graph->slot_count<count*2u)graph->slot_count*=2u;
    graph->tasks=(dependency_name *)calloc(count,sizeof(*graph->tasks));
    graph->name_slots=(size_t *)malloc(graph->slot_count*sizeof(*graph->name_slots));
    if(!graph->tasks||!graph->name_slots) {
        status=fail(error,error_capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow task names");
        goto done;
    }
    for(i=0;i<graph->slot_count;++i)graph->name_slots[i]=SIZE_MAX;
    for(i=0;i<count;++i)graph->tasks[i].first_outgoing=SIZE_MAX;
    /* Phase one exactly precedes all target and edge validation. */
    for(i=0;i<count;++i) {
        const json_value_t *value=task_metadata[i]?
            json_object_get(task_metadata[i],"name"):NULL;
        const char *name=value?json_string(value):NULL;
        size_t length=value?json_string_len(value):0u;
        if(!name||!length) {
            status=fail(error,error_capacity,SALTS_EINVAL,
                "Invalid native workflow task name");
            goto done;
        }
        slot=find_slot(graph,name,length);
        if(graph->name_slots[slot]!=SIZE_MAX) {
            status=formatted_error(error,error_capacity,
                "Duplicate task name: '%s'%s%s%s",name,"","","");
            goto done;
        }
        if(length+1u>PRAKTOR_WORKFLOW_MAX_NAME_BYTES-name_bytes) {
            status=fail(error,error_capacity,SALTS_EINVAL,
                "Native workflow name byte limit exceeded");
            goto done;
        }
        graph->tasks[i].bytes=(char *)malloc(length+1u);
        if(!graph->tasks[i].bytes) {
            status=fail(error,error_capacity,SALTS_ENOMEM,
                "Cannot copy native workflow task name");
            goto done;
        }
        memcpy(graph->tasks[i].bytes,name,length);
        graph->tasks[i].bytes[length]='\0';
        graph->tasks[i].length=length;
        name_bytes+=length+1u;
        graph->name_slots[slot]=i;
    }
    /* Phase two: validate all static trigger references before any depends_on. */
    for(i=0;i<count;++i) {
        const json_value_t *triggers=json_object_get(task_metadata[i],"triggers");
        static const char *const groups[]={"on_success","on_failure","on_complete"};
        const json_value_t *finally_node=json_object_get(task_metadata[i],"finally");
        for(j=0;j<sizeof(groups)/sizeof(groups[0]);++j) {
            status=check_trigger_list(graph,i,triggers,groups[j],error,error_capacity);
            if(status!=SALTS_OK)goto done;
        }
        if(finally_node) {
            const char *target=json_string(finally_node);
            if(!target) {
                status=fail(error,error_capacity,SALTS_EINVAL,
                    "Invalid native workflow finally metadata");
                goto done;
            }
            status=check_trigger_name(graph,i,"on_complete",target,
                json_string_len(finally_node),error,error_capacity);
            if(status!=SALTS_OK)goto done;
        }
    }
    /* Phase three: preserve duplicate edge cardinality and declaration order. */
    for(i=0;i<count;++i) {
        const json_value_t *deps=json_object_get(task_metadata[i],"depends_on");
        const size_t dep_count=deps?json_array_size(deps):0u;
        for(j=0;j<dep_count;++j) {
            const json_value_t *value=json_array_get(deps,j);
            const char *name=value?json_string(value):NULL;
            const size_t length=value?json_string_len(value):0u;
            size_t from;
            if(!name) {
                status=fail(error,error_capacity,SALTS_EINVAL,
                    "Invalid native workflow dependency metadata");
                goto done;
            }
            from=name_index(graph,name,length);
            if(from==SIZE_MAX) {
                status=formatted_error(error,error_capacity,
                    "Task '%s' depends on unknown task '%s'%s%s",
                    graph->tasks[i].bytes,name,"","");
                goto done;
            }
            status=append_edge(graph,from,i,error,error_capacity);
            if(status!=SALTS_OK)goto done;
        }
    }
    /* Phase four: Kahn's algorithm is iterative and bounded, unlike a deeply
     * recursive DFS. The existence of a cycle matches legacy graph refusal. */
    status=check_cycles(graph,error,error_capacity);
    if(status!=SALTS_OK)goto done;
    *out=graph;
    return SALTS_OK;
done:
    praktor_workflow_dependencies_destroy(graph);
    return status;
}
size_t praktor_workflow_dependencies_task_count(const praktor_workflow_dependencies *graph) {
    return graph?graph->task_count:0u;
}
size_t praktor_workflow_dependencies_edge_count(const praktor_workflow_dependencies *graph) {
    return graph?graph->edge_count:0u;
}
int praktor_workflow_dependencies_task_name(const praktor_workflow_dependencies *graph,
    size_t index,vstr *out) {
    if(!graph||!out)return SALTS_EINVAL;
    if(index>=graph->task_count)return SALTS_ENOENT;
    *out=vstr_from_buf(graph->tasks[index].bytes,graph->tasks[index].length);
    return SALTS_OK;
}
int praktor_workflow_dependencies_edge_at(const praktor_workflow_dependencies *graph,
    size_t index,size_t *predecessor,size_t *dependent) {
    if(!graph||!predecessor||!dependent)return SALTS_EINVAL;
    if(index>=graph->edge_count)return SALTS_ENOENT;
    *predecessor=graph->edges[index].predecessor;
    *dependent=graph->edges[index].dependent;
    return SALTS_OK;
}
int praktor_workflow_dependencies_has_edge(const praktor_workflow_dependencies *graph,
    size_t predecessor,size_t dependent,bool *out) {
    size_t edge;
    if(!graph||!out)return SALTS_EINVAL;
    if(predecessor>=graph->task_count||dependent>=graph->task_count)
        return SALTS_ENOENT;
    edge=graph->tasks[predecessor].first_outgoing;
    while(edge!=SIZE_MAX) {
        if(graph->edges[edge].dependent==dependent){*out=true;return SALTS_OK;}
        edge=graph->edges[edge].next_outgoing;
    }
    *out=false;
    return SALTS_OK;
}
