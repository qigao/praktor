/* Qualification of native DAG readiness, not a second executor/queue.
 * Every transition is on the test owner, mirroring the future CFlow owner. */
#include "yml/workflow_dag_state.h"
#include <json_parser.h>
#include <string.h>
#include <stdint.h>
#include <tinytest.h>

static praktor_workflow_dependencies *diamond_graph(void) {
    static const char *rows[] = {
        "{\"name\":\"a\"}",
        "{\"name\":\"b\",\"depends_on\":[\"a\"]}",
        "{\"name\":\"c\",\"depends_on\":[\"a\",\"a\"]}",
        "{\"name\":\"d\",\"depends_on\":[\"b\",\"c\"]}",
        "{\"name\":\"e\"}"
    };
    json_value_t *owned[5]={0};
    const json_value_t *views[5]={0};
    praktor_workflow_dependencies *graph=NULL;
    size_t i;
    for(i=0u;i<5u;++i) {
        owned[i]=json_parse(rows[i],strlen(rows[i]));
        check(owned[i]!=NULL);
        views[i]=owned[i];
    }
    if(views[0]&&views[1]&&views[2]&&views[3]&&views[4])
        check(praktor_workflow_dependencies_create(views,5u,&graph,NULL,0u)==SALTS_OK);
    for(i=0u;i<5u;++i)if(owned[i])json_free(owned[i]);
    return graph;
}

static void require_stats(praktor_workflow_dag_state *state,
    size_t active,size_t completed,size_t ready,
    praktor_workflow_dag_terminal terminal,bool quiescent,bool succeeded) {
    praktor_workflow_dag_stats s={0};
    check(praktor_workflow_dag_get_stats(state,&s)==SALTS_OK);
    check(s.task_count==5u);
    check(s.active==active);
    check(s.completed==completed);
    check(s.ready==ready);
    check(s.terminal==terminal);
    check(s.quiescent==quiescent);
    check(s.all_succeeded==succeeded);
}

spec("C11 DAG owner-turn readiness and settlement") {
    it("claims in declaration order, honors capacity and retires duplicate edges") {
        praktor_workflow_dependencies *graph=diamond_graph();
        praktor_workflow_dag_state *run=NULL;
        size_t chosen[3]={SIZE_MAX,SIZE_MAX,SIZE_MAX},count=99u;
        check(graph!=NULL);
        check(praktor_workflow_dag_state_create(graph,2u,&run)==SALTS_OK);
        require_stats(run,0u,0u,2u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false,false);
        check(praktor_workflow_dag_claim_ready(run,chosen,3u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==0u&&chosen[1]==4u);
        require_stats(run,2u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false,false);
        check(praktor_workflow_dag_claim_ready(run,chosen,3u,&count)==SALTS_OK);
        check(count==0u);
        check(praktor_workflow_dag_finish(run,4u,true)==SALTS_OK);
        check(praktor_workflow_dag_finish(run,4u,true)==SALTS_EINVAL);
        require_stats(run,1u,1u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false,false);
        check(praktor_workflow_dag_finish(run,0u,true)==SALTS_OK);
        require_stats(run,0u,2u,2u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false,false);
        check(praktor_workflow_dag_claim_ready(run,chosen,1u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==1u);
        check(praktor_workflow_dag_claim_ready(run,chosen,1u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==2u);
        check(praktor_workflow_dag_finish(run,1u,true)==SALTS_OK);
        require_stats(run,1u,3u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false,false);
        check(praktor_workflow_dag_finish(run,2u,true)==SALTS_OK);
        require_stats(run,0u,4u,1u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false,false);
        check(praktor_workflow_dag_claim_ready(run,chosen,3u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==3u);
        check(praktor_workflow_dag_finish(run,3u,true)==SALTS_OK);
        require_stats(run,0u,5u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,true,true);
        praktor_workflow_dag_state_destroy(run);
        praktor_workflow_dependencies_destroy(graph);
    }
    it("closes admission on branch failure while accepted sibling settles") {
        praktor_workflow_dependencies *graph=diamond_graph();
        praktor_workflow_dag_state *run=NULL;
        size_t chosen[3]={0},count=0u;
        check(praktor_workflow_dag_state_create(graph,3u,&run)==SALTS_OK);
        check(praktor_workflow_dag_claim_ready(run,chosen,3u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==0u&&chosen[1]==4u);
        check(praktor_workflow_dag_finish(run,0u,true)==SALTS_OK);
        check(praktor_workflow_dag_claim_ready(run,chosen,3u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==1u&&chosen[1]==2u);
        check(praktor_workflow_dag_finish(run,1u,false)==SALTS_OK);
        require_stats(run,2u,2u,0u,PRAKTOR_WORKFLOW_DAG_FAILED,false,false);
        count=111u;
        check(praktor_workflow_dag_claim_ready(run,chosen,3u,&count)==SALTS_EBUSY);
        check(count==0u);
        check(praktor_workflow_dag_finish(run,2u,true)==SALTS_OK);
        check(praktor_workflow_dag_finish(run,4u,true)==SALTS_OK);
        require_stats(run,0u,4u,0u,PRAKTOR_WORKFLOW_DAG_FAILED,true,false);
        praktor_workflow_dag_state_destroy(run);
        praktor_workflow_dependencies_destroy(graph);
    }
    it("honors cancellation precedence and never schedules descendants") {
        praktor_workflow_dependencies *graph=diamond_graph();
        praktor_workflow_dag_state *run=NULL;
        size_t chosen[2]={0},count=0u;
        check(praktor_workflow_dag_state_create(graph,2u,&run)==SALTS_OK);
        check(praktor_workflow_dag_claim_ready(run,chosen,2u,&count)==SALTS_OK);
        check(count==2u);
        check(praktor_workflow_dag_stop(run,PRAKTOR_WORKFLOW_DAG_CANCELLED)==SALTS_OK);
        check(praktor_workflow_dag_stop(run,PRAKTOR_WORKFLOW_DAG_FAILED)==SALTS_OK);
        check(praktor_workflow_dag_finish(run,0u,false)==SALTS_OK);
        check(praktor_workflow_dag_finish(run,4u,true)==SALTS_OK);
        require_stats(run,0u,2u,0u,PRAKTOR_WORKFLOW_DAG_CANCELLED,true,false);
        check(praktor_workflow_dag_claim_ready(run,chosen,2u,&count)==SALTS_EBUSY);
        praktor_workflow_dag_state_destroy(run);
        praktor_workflow_dependencies_destroy(graph);
    }
    it("refuses unadmitted work without imagining callbacks or publication") {
        praktor_workflow_dependencies *graph=diamond_graph();
        praktor_workflow_dag_state *run=NULL,*sentinel=NULL;
        size_t chosen[2]={0},count=0u;
        check(praktor_workflow_dag_state_create(graph,0u,&sentinel)==SALTS_EINVAL);
        check(sentinel==NULL);
        check(praktor_workflow_dag_state_create(graph,2u,&run)==SALTS_OK);
        check(praktor_workflow_dag_claim_ready(run,chosen,2u,&count)==SALTS_OK);
        check(count==2u);
        check(praktor_workflow_dag_refuse(run,chosen[0])==SALTS_OK);
        check(praktor_workflow_dag_refuse(run,chosen[0])==SALTS_EINVAL);
        check(praktor_workflow_dag_finish(run,chosen[0],true)==SALTS_EINVAL);
        require_stats(run,1u,0u,0u,PRAKTOR_WORKFLOW_DAG_ADMISSION_REFUSED,false,false);
        check(praktor_workflow_dag_finish(run,chosen[1],true)==SALTS_OK);
        require_stats(run,0u,1u,0u,PRAKTOR_WORKFLOW_DAG_ADMISSION_REFUSED,true,false);
        praktor_workflow_dag_state_destroy(run);
        praktor_workflow_dependencies_destroy(graph);
    }
}
