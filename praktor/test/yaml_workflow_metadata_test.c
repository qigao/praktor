#include "yml/yaml_workflow_metadata.h"
#include <string.h>
#include <tinytest.h>
static praktor_yaml_document *doc_for(const char *yaml) {
    praktor_yaml_document *doc=NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(yaml),&doc,NULL)==SALTS_OK);
    return doc;
}
static void json_eq(const json_value_t *value,const char *expected) {
    char *text=json_serialize(value,NULL);check(text&&strcmp(text,expected)==0);json_serialize_free(text);
}
spec("C workflow root metadata parser") {
    it("parses scalar metadata and defaults") {
        praktor_yaml_document *doc=doc_for("name: demo\ndescription: sample\ndefaults: {timeout: 30000, stream_output: false}\ntasks: []\n");
        json_value_t *result=NULL;praktor_yaml_field_error error={0};
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,&error)==SALTS_OK);
        json_eq(result,"{\"name\":\"demo\",\"description\":\"sample\",\"defaults\":{\"timeout\":\"30000\",\"stream_output\":\"false\"}}");
        json_free(result);praktor_yaml_field_error_dispose(&error);praktor_yaml_document_destroy(doc);
    }
    it("projects variables, env and dotEnv into owned JSON containers") {
        praktor_yaml_document *doc=doc_for(
            "variables: {A: one, B: two}\nenv: {MODE: test}\ndotEnv: [.env, .env.local]\ntasks: []\n");
        json_value_t *result=NULL;praktor_yaml_field_error error={0};
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,&error)==SALTS_OK);
        json_eq(result,"{\"variables\":{\"A\":\"one\",\"B\":\"two\"},\"env\":{\"MODE\":\"test\"},\"dot_env\":[\".env\",\".env.local\"]}");
        json_free(result);praktor_yaml_field_error_dispose(&error);praktor_yaml_document_destroy(doc);
    }
    it("overlays environment then document values one field at a time") {
        const char *yaml[]={"tasks: []\n","defaults: {timeout: 1500}\ntasks: []\n",
            "defaults: {stream_output: false}\ntasks: []\n"};
        for(size_t i=0u;i<3u;++i) {
            praktor_yaml_document *doc=doc_for(yaml[i]);json_value_t *metadata=NULL;
            int timeout=30000;bool stream=true;const char *error=NULL;
            check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&metadata,NULL)==SALTS_OK);
            check(praktor_yaml_workflow_resolve_defaults(metadata,NULL,NULL,&timeout,&stream,&error)==SALTS_OK);
            check_equal(timeout,i==1u?1500:30000);check_equal(stream,i!=2u);check_null(error);
            check(praktor_yaml_workflow_resolve_defaults(metadata,"750","true",&timeout,&stream,&error)==SALTS_OK);
            check_equal(timeout,i==1u?1500:750);check_equal(stream,i!=2u);
            check(praktor_yaml_workflow_resolve_defaults(metadata,"2147483647","false",&timeout,&stream,&error)==SALTS_OK);
            check_equal(timeout,i==1u?1500:2147483647);check_false(stream);
            json_free(metadata);praktor_yaml_document_destroy(doc);
        }
    }
    it("rejects invalid environment even when a document would override it without committing partial defaults") {
        praktor_yaml_document *doc=doc_for("defaults: {timeout: 1500, stream_output: false}\ntasks: []\n");
        json_value_t *metadata=NULL;const char *error=NULL;
        const char *bad[]={"","0","-1","1.5","30s"," 10","+10","2147483648"};
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&metadata,NULL)==SALTS_OK);
        for(size_t i=0u;i<sizeof bad/sizeof bad[0];++i) {
            int timeout=30000;bool stream=true;
            check(praktor_yaml_workflow_resolve_defaults(metadata,bad[i],"false",&timeout,&stream,&error)==SALTS_EINVAL);
            check_equal(timeout,30000);check_true(stream);check_contains(error,"PRAKTOR_TIMEOUT_MS");
        }
        const char *bad_stream[]={"","0","yes","FALSE","true "};
        for(size_t i=0u;i<sizeof bad_stream/sizeof bad_stream[0];++i) {
            int timeout=30000;bool stream=true;
            check(praktor_yaml_workflow_resolve_defaults(metadata,"750",bad_stream[i],&timeout,&stream,&error)==SALTS_EINVAL);
            check_equal(timeout,30000);check_true(stream);check_contains(error,"PRAKTOR_STREAM_OUTPUT");
        }
        json_free(metadata);praktor_yaml_document_destroy(doc);
    }
    it("rejects malformed variable and env containers") {
        const char *bad[]={
            "variables: scalar\ntasks: []\n",
            "env: [not, a, map]\ntasks: []\n"
        };
        size_t i;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            praktor_yaml_document *doc=doc_for(bad[i]);json_value_t *result=NULL;praktor_yaml_field_error error={0};
            check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,&error)!=SALTS_OK);
            check(result==NULL);praktor_yaml_field_error_dispose(&error);praktor_yaml_document_destroy(doc);
        }
    }
    it("preserves legacy scalar dotEnv conversion") {
        praktor_yaml_document *doc=doc_for("dotEnv: .env\ntasks: []\n");
        json_value_t *result=NULL;
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,NULL)==SALTS_OK);
        json_eq(result,"{\"dot_env\":[\".env\"]}");
        json_free(result);praktor_yaml_document_destroy(doc);
    }
    it("rejects inputs, input_policy, unknown keys and malformed defaults") {
        const char *bad[]={"tasks: []\nunknown: true\n","tasks: []\ninputs: {}\n","tasks: []\ninput_policy: strict\n","tasks: []\ndefaults: []\n","tasks: []\ndefaults: {timeout: 1, extra: 2}\n",
            "tasks: []\ndefaults: {timeout: 30s}\n","tasks: []\ndefaults: {timeout: 0}\n",
            "tasks: []\ndefaults: {timeout: 2147483648}\n","tasks: []\ndefaults: {timeout: null}\n",
            "tasks: []\ndefaults: {stream_output: yes}\n","tasks: []\ndefaults: {stream_output: null}\n"};
        size_t i;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            praktor_yaml_document *doc=doc_for(bad[i]);json_value_t *result=NULL;praktor_yaml_field_error error={0};
            check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,&error)!=SALTS_OK);
            check(result==NULL&&error.text!=NULL);praktor_yaml_field_error_dispose(&error);praktor_yaml_document_destroy(doc);
        }
    }
}
