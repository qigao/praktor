#include "praktor.h"
#include "praktor/runtime.h"
#include "yml/yaml_workflow_metadata.h"
#include <dotenv.h>
#include <string.h>
#define PRAKTOR_MAX_DOTENV_FILES 32u
static int load_environment(const char *source, size_t size) {
  praktor_yaml_document *document = NULL;
  json_value_t *metadata = NULL;
  int result = -1;
  if (praktor_yaml_document_create(vstr_from_buf(source, size), &document, NULL) != SALTS_OK)
    goto done;
  if (praktor_yaml_workflow_metadata_create(praktor_yaml_root(document), &metadata, NULL) !=
      SALTS_OK)
    goto done;
  const json_value_t *files = json_object_get(metadata, "dot_env");
  size_t count = files ? json_array_size(files) : 0u;
  if (count > PRAKTOR_MAX_DOTENV_FILES)
    goto done;
  for (size_t i = 0u; i < count; ++i) {
    const json_value_t *file = json_array_get(files, i);
    const char *path = json_string(file);
    size_t length = json_string_len(file);
    if (!path || !length || strlen(path) != length || dotenv_load(path, true) != 0)
      goto done;
  }
  result = 0;
done:
  json_free(metadata);
  praktor_yaml_document_destroy(document);
  return result;
}

praktor_runtime_status praktor_load_environment(const char *yaml,size_t size) {
    if(!yaml||!size||size>1024u*1024u)return PRAKTOR_RUNTIME_INVALID;
    return load_environment(yaml,size)==0?PRAKTOR_RUNTIME_OK:PRAKTOR_RUNTIME_INVALID;
}
praktor_runtime_status praktor_execute(
    const praktor_execute_request *request,praktor_runtime_result *out) {
    if(!request||!out)return PRAKTOR_RUNTIME_INVALID;
    return praktor_runtime_execute_yaml(
        request->yaml,request->yaml_size,&request->limits,
        request->is_cancelled,request->cancel_context,out);
}
void praktor_release_result(praktor_runtime_result *out) {
    praktor_runtime_result_destroy(out);
}
praktor_runtime_status praktor_execute_task(
    const praktor_execute_request *request,const char *task,praktor_runtime_result *out) {
    if(!request||!out||!task||!*task)return PRAKTOR_RUNTIME_INVALID;
    return praktor_runtime_execute_task_yaml(request->yaml,request->yaml_size,&request->limits,
        request->is_cancelled,request->cancel_context,task,out);
}
praktor_runtime_limits praktor_default_limits(void) {
    return praktor_runtime_limits_default();
}
const cmeta_data_desc *praktor_config_reflection(void) {
    return praktor_runtime_limits_reflection();
}
