#include "actions/json_selection.h"
#include <core/error_codes.h>
#include <json_parser.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { JSON_SELECTION_PATH_BYTES = 4096 };

int praktor_json_result_parse(const char *text,praktor_json_result *out) {
    if(strcmp(text,"first")==0)*out=PRAKTOR_JSON_FIRST;
    else if(strcmp(text,"one")==0)*out=PRAKTOR_JSON_ONE;
    else if(strcmp(text,"count")==0)*out=PRAKTOR_JSON_COUNT;
    else return SALTS_EINVAL;
    return SALTS_OK;
}

int praktor_json_path_normalize(char *path,size_t capacity) {
    const char *start=path;
    while(*start&&isspace((unsigned char)*start))++start;
    size_t length=strlen(start);
    while(length&&isspace((unsigned char)start[length-1u]))--length;
    const size_t prefix=!length||start[0]=='['?1u:start[0]=='$'?0u:2u;
    if(prefix>=capacity||length>=capacity-prefix)return SALTS_ENOBUFS;
    memmove(path+prefix,start,length);
    if(prefix)path[0]='$';
    if(prefix==2u)path[1]='.';
    path[prefix+length]='\0';
    return SALTS_OK;
}

static int fail(char *error,size_t capacity,int status,const char *message) {
    if(error&&capacity)(void)snprintf(error,capacity,"ParseJson: %s",message);
    return status;
}

static int stream_open(const char *path,const json_path_stream_handler_t *handler,void *user,
                       json_path_program_t **out_program,json_path_stream_t **out_stream,
                       char *error,size_t capacity) {
    char normalized[JSON_SELECTION_PATH_BYTES+1u];
    const size_t length=strlen(path);
    if(length>=sizeof normalized)
        return fail(error,capacity,SALTS_ENOBUFS,"JSONPath exceeds 4096 bytes");
    memcpy(normalized,path,length+1u);
    if(praktor_json_path_normalize(normalized,sizeof normalized)!=SALTS_OK)
        return fail(error,capacity,SALTS_ENOBUFS,"Normalized JSONPath exceeds 4096 bytes");

    json_path_program_t *program=json_path_compile(normalized);
    if(!program) {
        const char *detail=json_path_get_error();
        return fail(error,capacity,SALTS_EINVAL,detail?detail:"Invalid JSONPath");
    }
    json_path_stream_t *stream=json_path_stream_create(program,handler,user);
    if(!stream) {
        const char *detail=json_path_get_error();
        const int status=fail(error,capacity,SALTS_ENOTSUP,
                              detail?detail:"JSONPath cannot be evaluated as a stream");
        json_path_program_free(program);
        return status;
    }
    *out_program=program;
    *out_stream=stream;
    return SALTS_OK;
}

int praktor_json_selection_validate(const char *path,char *error,size_t capacity) {
    const json_path_stream_handler_t handler={0};
    json_path_program_t *program=NULL;
    json_path_stream_t *stream=NULL;
    int status=stream_open(path,&handler,NULL,&program,&stream,error,capacity);
    json_path_stream_destroy(stream);
    json_path_program_free(program);
    return status;
}

typedef struct scalar_match {
    char *output;
    size_t capacity;
    int status;
    bool composite;
} scalar_match;

static int match_start(void *user,json_type_t type) {
    scalar_match *match=user;
    if(type==JSON_ARRAY||type==JSON_OBJECT)match->composite=true;
    return 0;
}

static int capture_text(void *user,const char *text,size_t length) {
    scalar_match *match=user;
    if(match->composite)return 0;
    if(length>=match->capacity)match->status=SALTS_ENOBUFS;
    else if(length&&memchr(text,'\0',length))match->status=SALTS_ENOTSUP;
    if(match->status!=SALTS_OK)return -1;
    if(length)memcpy(match->output,text,length);
    match->output[length]='\0';
    return 0;
}

static int capture_null(void *user) {
    return capture_text(user,"null",4u);
}

static int capture_bool(void *user,bool value) {
    return value?capture_text(user,"true",4u):capture_text(user,"false",5u);
}

int praktor_json_selection_run(praktor_json_result mode,const char *path,
                               const char *input,size_t length,char *output,
                               size_t output_capacity,char *error,size_t error_capacity) {
    const json_path_stream_handler_t scalar_handler={
        .on_match_start=match_start,
        .events={.on_null=capture_null,.on_bool=capture_bool,
                 .on_number=capture_text,.on_string=capture_text}
    };
    const json_path_stream_handler_t count_handler={0};
    scalar_match match={.output=output,.capacity=output_capacity};
    json_path_program_t *program=NULL;
    json_path_stream_t *stream=NULL;
    int status=stream_open(path,mode==PRAKTOR_JSON_ONE?&scalar_handler:&count_handler,
                           &match,&program,&stream,error,error_capacity);
    if(status!=SALTS_OK)return status;
    if(json_path_stream_feed(stream,input,length)!=0||json_path_stream_finish(stream)!=0) {
        if(match.status==SALTS_ENOBUFS)
            status=fail(error,error_capacity,match.status,"Selected scalar exceeds the value capacity");
        else if(match.status==SALTS_ENOTSUP)
            status=fail(error,error_capacity,match.status,"Selected string contains NUL");
        else {
            const char *detail=json_path_stream_error(stream);
            status=fail(error,error_capacity,SALTS_EINVAL,detail?detail:"Invalid JSON input");
        }
    } else {
        const size_t matches=json_path_stream_match_count(stream);
        if(mode==PRAKTOR_JSON_ONE&&(matches!=1u||match.composite)) {
            if(error&&error_capacity)
                (void)snprintf(error,error_capacity,
                    "ParseJson: result 'one' requires one scalar (matched %zu values%s)",
                    matches,match.composite?", including an array or object":"");
            status=SALTS_EINVAL;
        } else if(mode==PRAKTOR_JSON_COUNT) {
            const int size=snprintf(output,output_capacity,"%zu",matches);
            if(size<0||(size_t)size>=output_capacity)
                status=fail(error,error_capacity,SALTS_ENOBUFS,"Match count exceeds the value capacity");
        }
    }
    json_path_stream_destroy(stream);
    json_path_program_free(program);
    return status;
}
