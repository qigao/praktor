#include "praktor.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if (argc!=2) {
        fputs("usage: praktor workflow.yml\nC11-only CLI: no legacy modes\n",stderr);
        return 2;
    }
    FILE *file=fopen(argv[1],"rb");
    if(!file){perror(argv[1]);return 2;}
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return 2;}
    long len=ftell(file);
    if(len<=0 || len>1024*1024 || fseek(file,0,SEEK_SET)!=0){
        fclose(file);return 2;
    }
    char *yaml=(char *)malloc((size_t)len+1u);
    if(!yaml){fclose(file);return 2;}
    if(fread(yaml,1u,(size_t)len,file)!=(size_t)len) {
        free(yaml);fclose(file);return 2;
    }
    fclose(file);yaml[len]='\0';
    praktor_execute_request request={0};
    praktor_runtime_result result={0};
    request.yaml=yaml;request.yaml_size=(size_t)len;
    request.limits=praktor_default_limits();
    praktor_runtime_status status=praktor_execute(&request,&result);
    if(result.json){fwrite(result.json,1u,result.json_size,stdout);fputc('\n',stdout);}
    if(status!=PRAKTOR_RUNTIME_OK && result.error[0])
        fprintf(stderr,"praktor: %s\n",result.error);
    praktor_release_result(&result);free(yaml);
    return status==PRAKTOR_RUNTIME_OK?0:status>=0?1:2;
}
