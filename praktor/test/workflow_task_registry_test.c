#include "yml/workflow_task_registry.h"
#include <string.h>
#include <tinytest.h>

static praktor_workflow_snapshot *make_snapshot(void) {
    const char *yaml=
        "name: stateful test\n"
        "tasks:\n"
        "  - {name: prepare, command: echo ready}\n"
        "  - {name: deliver, command: echo done, depends_on: [prepare]}\n";
    praktor_workflow_snapshot *snapshot=NULL;
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&snapshot,NULL,0)==SALTS_OK);
    return snapshot;
}
static praktor_workflow_task_registry *make_registry(void) {
    praktor_workflow_snapshot *snapshot=make_snapshot();
    praktor_workflow_task_registry *owner=NULL;
    check(praktor_workflow_task_registry_create(snapshot,&owner)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    return owner;
}
static bool output_matches(const praktor_workflow_task_registry *owner,
                           size_t task,const char *key,const char *expected) {
    const json_value_t *out=NULL;
    char *encoded;
    bool result=false;
    if(praktor_workflow_task_registry_get_output(owner,task,
        vstr_from_cstr(key),&out)!=SALTS_OK)return false;
    encoded=json_serialize(out,NULL);
    if(encoded){result=strcmp(encoded,expected)==0;json_serialize_free(encoded);}
    return result;
}
spec("C11 structured task status and immutable outputs") {
    it("owns typed nested JSON independently from its workflow and caller") {
        praktor_workflow_task_registry *owner=make_registry();
        const char *source_text=
            "{\"records\":[{\"n\":9007199254740993,\"label\":\"中\"}],"
            "\"enabled\":true}";
        json_value_t *source=json_parse(source_text,strlen(source_text));
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_FAILED;
        const json_value_t *visible=NULL;
        vstr name={0};
        check(owner&&praktor_workflow_task_registry_count(owner)==2u);
        check(praktor_workflow_task_registry_name(owner,0u,&name)==SALTS_OK);
        check(vstr_eq(name,vstr_from_cstr("prepare")));
        check(praktor_workflow_task_registry_state(owner,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        check(source!=NULL);
        check(praktor_workflow_task_registry_set_output(owner,0u,
            vstr_from_cstr("payload"),source)==SALTS_OK);
        json_free(source);
        check(praktor_workflow_task_registry_state(owner,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_RUNNING);
        check(praktor_workflow_task_registry_get_output(owner,0u,
            vstr_from_cstr("payload"),&visible)==SALTS_EBUSY);
        check(praktor_workflow_task_registry_complete(owner,0u)==SALTS_OK);
        check(output_matches(owner,0u,"payload",
            "{\"records\":[{\"n\":9007199254740993,\"label\":\"中\"}],"
            "\"enabled\":true}"));
        check(praktor_workflow_task_registry_get_outputs(owner,0u,&visible)==SALTS_OK);
        check(visible&&json_object_size(visible)==1u);
        {
            json_value_t *after=json_create_null();
            check(after!=NULL);
            check(praktor_workflow_task_registry_set_output(owner,0u,
                vstr_from_cstr("after"),after)==SALTS_EBUSY);
            json_free(after);
        }
        check(praktor_workflow_task_registry_complete(owner,0u)==SALTS_EBUSY);
        praktor_workflow_task_registry_destroy(owner);
    }
    it("replaces outputs atomically while running and finalizes failure") {
        praktor_workflow_task_registry *owner=make_registry();
        const char *first_text="{\"epoch\":1}";
        const char *second_text="{\"epoch\":2,\"ok\":false}";
        json_value_t *first=json_parse(first_text,strlen(first_text));
        json_value_t *second=json_parse(second_text,strlen(second_text));
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_PENDING;
        vstr reason={0};
        check(first&&second);
        check(praktor_workflow_task_registry_start(owner,1u)==SALTS_OK);
        check(praktor_workflow_task_registry_set_output(owner,1u,
            vstr_from_cstr("record"),first)==SALTS_OK);
        check(praktor_workflow_task_registry_set_output(owner,1u,
            vstr_from_cstr("record"),second)==SALTS_OK);
        json_free(first);json_free(second);
        check(praktor_workflow_task_registry_fail(owner,1u,
            vstr_from_cstr("failed intentionally"))==SALTS_OK);
        check(output_matches(owner,1u,"record","{\"epoch\":2,\"ok\":false}"));
        check(praktor_workflow_task_registry_failure_reason(owner,1u,&reason)==SALTS_OK);
        check(vstr_eq(reason,vstr_from_cstr("failed intentionally")));
        check(praktor_workflow_task_registry_state(owner,1u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_FAILED);
        check(praktor_workflow_task_registry_fail(owner,1u,
            vstr_from_cstr("overwrite"))==SALTS_EBUSY);
        check(praktor_workflow_task_registry_start(owner,1u)==SALTS_OK);
        check(praktor_workflow_task_registry_failure_reason(owner,1u,&reason)==SALTS_ENOENT);
        check(praktor_workflow_task_registry_state(owner,1u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_RUNNING);
        check(praktor_workflow_task_registry_complete(owner,1u)==SALTS_OK);
        check(!output_matches(owner,1u,"record","{\"epoch\":2,\"ok\":false}"));
        praktor_workflow_task_registry_destroy(owner);
    }
    it("rejects oversized deep/binary input with no partial publication") {
        praktor_workflow_task_registry *owner=make_registry();
        json_value_t *valid=json_create_string("present");
        json_value_t *bad=NULL,*deep=NULL;
        const json_value_t *saved=NULL,*out=(const json_value_t *)owner;
        char nested[160];
        size_t i;
        check(praktor_workflow_task_registry_set_output(owner,0u,
            vstr_from_cstr("value"),valid)==SALTS_OK);
        json_free(valid);
        bad=json_create_string_n("x\0y",3u);
        check(bad!=NULL);
        /* Binary payload is legitimately supported by typed JSON; the
         * workflow value store's text-only limitation is separate. */
        check(praktor_workflow_task_registry_set_output(owner,0u,
            vstr_from_cstr("binary"),bad)==SALTS_OK);
        json_free(bad);
        for(i=0;i<70u;++i)nested[i]='[';
        nested[70]='0';
        for(i=0;i<70u;++i)nested[71u+i]=']';
        nested[141u]='\0';
        deep=json_parse(nested,141u);
        check(deep!=NULL);
        check(praktor_workflow_task_registry_set_output(owner,0u,
            vstr_from_cstr("deep"),deep)==SALTS_EINVAL);
        json_free(deep);
        check(praktor_workflow_task_registry_get_output(owner,0u,
            vstr_from_cstr("deep"),&out)==SALTS_EBUSY);
        check(out==(const json_value_t *)owner);
        check(praktor_workflow_task_registry_complete(owner,0u)==SALTS_OK);
        check(praktor_workflow_task_registry_get_output(owner,0u,
            vstr_from_cstr("value"),&saved)==SALTS_OK);
        check(json_string(saved)&&strcmp(json_string(saved),"present")==0);
        check(praktor_workflow_task_registry_get_output(owner,0u,
            vstr_from_cstr("deep"),&out)==SALTS_ENOENT);
        check(out==(const json_value_t *)owner);
        praktor_workflow_task_registry_destroy(owner);
    }
    it("keeps output handles unchanged for malformed indices and finalized tasks") {
        praktor_workflow_task_registry *owner=make_registry();
        praktor_workflow_task_registry *existing=owner,*out=existing;
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_FAILED;
        const json_value_t *sentinel=(const json_value_t *)owner,*borrow=sentinel;
        check(praktor_workflow_task_registry_create(NULL,&out)==SALTS_EINVAL);
        check(out==existing);
        check(praktor_workflow_task_registry_state(owner,7u,&state)==SALTS_ENOENT);
        check(state==PRAKTOR_WORKFLOW_TASK_FAILED);
        check(praktor_workflow_task_registry_get_output(owner,7u,
            vstr_from_cstr("x"),&borrow)==SALTS_ENOENT);
        check(borrow==sentinel);
        check(praktor_workflow_task_registry_skip(owner,0u)==SALTS_OK);
        check(praktor_workflow_task_registry_get_outputs(owner,0u,&borrow)==SALTS_EBUSY);
        {
            json_value_t *value=json_create_bool(true);
            check(value!=NULL);
            check(praktor_workflow_task_registry_set_output(owner,0u,
                vstr_from_cstr("x"),value)==SALTS_EBUSY);
            json_free(value);
        }
        check(praktor_workflow_task_registry_start(owner,0u)==SALTS_OK);
        check(praktor_workflow_task_registry_complete(owner,0u)==SALTS_OK);
        check(praktor_workflow_task_registry_get_outputs(owner,0u,&borrow)==SALTS_ENOENT);
        check(borrow==sentinel);
        praktor_workflow_task_registry_destroy(owner);
    }
    it("publishes typed multi-output updates all-or-nothing") {
        praktor_workflow_task_registry *owner=make_registry();
        const char *initial_text=
            "{\"first\":{\"count\":2},\"second\":[true,null]}";
        const char *patch_text=
            "{\"first\":{\"count\":3},\"third\":\"new\"}";
        const char *bad_text="{\"fourth\":1,\"\":2}";
        json_value_t *initial=json_parse(initial_text,strlen(initial_text));
        json_value_t *patch=json_parse(patch_text,strlen(patch_text));
        json_value_t *bad=json_parse(bad_text,strlen(bad_text));
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_PENDING;
        const json_value_t *out=NULL;
        check(initial&&patch&&bad);
        check(praktor_workflow_task_registry_merge_outputs(owner,0u,initial)==SALTS_OK);
        check(praktor_workflow_task_registry_state(owner,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_RUNNING);
        check(praktor_workflow_task_registry_merge_outputs(owner,0u,bad)==SALTS_EINVAL);
        check(praktor_workflow_task_registry_merge_outputs(owner,0u,patch)==SALTS_OK);
        json_free(initial);json_free(patch);json_free(bad);
        check(praktor_workflow_task_registry_complete(owner,0u)==SALTS_OK);
        check(output_matches(owner,0u,"first","{\"count\":3}"));
        check(output_matches(owner,0u,"second","[true,null]"));
        check(output_matches(owner,0u,"third","\"new\""));
        check(praktor_workflow_task_registry_get_output(owner,0u,
            vstr_from_cstr("fourth"),&out)==SALTS_ENOENT);
        check(praktor_workflow_task_registry_merge_outputs(owner,0u,NULL)==SALTS_EINVAL);
        praktor_workflow_task_registry_destroy(owner);
    }

}
