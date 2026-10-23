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
        praktor_yaml_document *doc=doc_for("name: demo\ndescription: sample\ninput_policy: strict\ndefaults: {timeout: 30s}\ntasks: []\n");
        json_value_t *result=NULL;praktor_yaml_field_error error={0};
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,&error)==SALTS_OK);
        json_eq(result,"{\"name\":\"demo\",\"description\":\"sample\",\"strict_inputs\":true,\"defaults\":{\"timeout\":\"30s\"}}");
        json_free(result);praktor_yaml_field_error_dispose(&error);praktor_yaml_document_destroy(doc);
    }
    it("preserves absent policy and allow_extra") {
        praktor_yaml_document *doc=doc_for("tasks: []\ninput_policy: allow_extra\n");json_value_t *result=NULL;
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,NULL)==SALTS_OK);
        json_eq(result,"{\"strict_inputs\":false}");json_free(result);praktor_yaml_document_destroy(doc);
    }
    it("rejects unknown keys and malformed policies/defaults") {
        const char *bad[]={"tasks: []\nunknown: true\n","tasks: []\ninput_policy: loose\n","tasks: []\ndefaults: []\n","tasks: []\ndefaults: {timeout: 1, extra: 2}\n"};
        size_t i;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            praktor_yaml_document *doc=doc_for(bad[i]);json_value_t *result=NULL;praktor_yaml_field_error error={0};
            check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&result,&error)!=SALTS_OK);
            check(result==NULL&&error.text!=NULL);praktor_yaml_field_error_dispose(&error);praktor_yaml_document_destroy(doc);
        }
    }
}
