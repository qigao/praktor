#include "yml/yaml_task_metadata.h"
#include <string.h>
#include <tinytest.h>
static praktor_yaml_document *doc_for(const char *yaml){praktor_yaml_document *d=NULL;check(praktor_yaml_document_create(vstr_from_cstr(yaml),&d,NULL)==SALTS_OK);return d;}
static void json_eq(const json_value_t *v,const char *expected){char *s=json_serialize(v,NULL);check(s&&strcmp(s,expected)==0);json_serialize_free(s);}
spec("C task metadata parser"){
 it("projects scalar, list and map metadata"){praktor_yaml_document *d=doc_for("name: build\ndescription: Compile\ndepends_on: [configure]\nvars: {MODE: fast}\nenv: {CI: '1'}\ndotEnv: .env\nwhen: ready\nworking_dir: src\nsources: [a.c, b.c]\ngenerates: out.o\nfinally: cleanup\n");json_value_t *v=NULL;praktor_yaml_field_error e={0};check(praktor_yaml_task_metadata_create(praktor_yaml_root(d),&v,&e)==SALTS_OK);json_eq(v,"{\"name\":\"build\",\"description\":\"Compile\",\"depends_on\":[\"configure\"],\"vars\":{\"MODE\":\"fast\"},\"env\":{\"CI\":\"1\"},\"dot_env\":[\".env\"],\"when\":\"ready\",\"working_dir\":\"src\",\"sources\":[\"a.c\",\"b.c\"],\"generates\":[\"out.o\"],\"finally\":\"cleanup\"}");json_free(v);praktor_yaml_field_error_dispose(&e);praktor_yaml_document_destroy(d);}
 it("rejects missing or empty names and unknown keys"){const char *bad[]={"description: no name\n","name: ''\n","name: ok\nunknown: 1\n"};size_t i;for(i=0;i<3;++i){praktor_yaml_document *d=doc_for(bad[i]);json_value_t *v=NULL;praktor_yaml_field_error e={0};check(praktor_yaml_task_metadata_create(praktor_yaml_root(d),&v,&e)!=SALTS_OK);check(v==NULL);praktor_yaml_field_error_dispose(&e);praktor_yaml_document_destroy(d);}}
 it("rejects with without tool"){praktor_yaml_document *d=doc_for("name: build\nwith: {x: 1}\n");json_value_t *v=NULL;praktor_yaml_field_error e={0};check(praktor_yaml_task_metadata_create(praktor_yaml_root(d),&v,&e)!=SALTS_OK);check(v==NULL);praktor_yaml_field_error_dispose(&e);praktor_yaml_document_destroy(d);}
}
