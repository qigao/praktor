#include "projection.h"
#include <cmeta_fs.h>
#include <salts/error_codes.h>
#include <stdio.h>

int main(int argc, char **argv) {
    cmeta_fs_buf_t idl = {0}, policy = {0}, output = {0};
    json_value_t *editor = NULL, *schema = NULL;
    char error[512] = {0};
    char *serialized = NULL;
    size_t length = 0;
    int status = 1;
    if (argc != 4) {
        fputs("Usage: praktor-schema workflow.schema workflow.editor.json grammar.schema.json\n", stderr);
        return 2;
    }
    if (cmeta_fs_read_file(argv[1], &idl) || cmeta_fs_read_file(argv[2], &policy)) {
        fputs("Cannot read schema source\n", stderr); goto finished;
    }
    editor = json_parse(policy.base, policy.len);
    if (!editor) { fputs("Invalid editor JSON\n", stderr); goto finished; }
    if (praktor_schema_project(idl.base, idl.len, editor, &schema, error, sizeof(error)) != SALTS_OK) {
        fprintf(stderr, "%s\n", error); goto finished;
    }
    serialized = json_serialize_pretty(schema, &length);
    if (!serialized || length == SIZE_MAX) { fputs("Cannot encode generated schema\n", stderr); goto finished; }
    /* The serializer owns length+1 bytes. Write a trailing LF in place of its
     * terminator; no C-string consumer observes the buffer after this point. */
    serialized[length] = '\n'; output.base = serialized; output.len = length + 1;
    if (cmeta_fs_write_file(argv[3], &output)) { fputs("Cannot write generated schema\n", stderr); goto finished; }
    status = 0;
finished:
    json_serialize_free(serialized); json_free(schema); json_free(editor);
    cmeta_fs_buf_free(&policy); cmeta_fs_buf_free(&idl);
    return status;
}
