#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void finish_worker(pid_t *worker){
    int status=0;assert(*worker>0);
    assert(waitpid(*worker,&status,0)==*worker);
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);*worker=0;
}
int main(void){
    assert(SDL_Init(SDL_INIT_TIMER)==0);
    char folder[]="/tmp/flxtr-spawn-test-XXXXXX";
    char *run=mkdtemp(folder);assert(run);
    assert(!chdir(run));
    FILE *helper=fopen("greenlink-catalog","w");assert(helper);
    fputs("#!/bin/sh\nprintf '%s\\n' \"$@\"\nprintf 'worker log\\n' >&2\n",helper);
    fclose(helper);assert(!chmod("greenlink-catalog",0700));
    Browse next={1,2,7,1,2,0,"",""};
    strcpy(next.query,"a title $(touch INJECTED); & quoted");
    request_catalog(next,0,0);finish_worker(&catalog_pid);
    FILE *result=fopen("catalog-cache/result.tsv","r");assert(result);
    char contents[512]={0};
    assert(fread(contents,1,sizeof(contents)-1,result)>0);fclose(result);
    assert(!strcmp(contents,"movie\n2\na title $(touch INJECTED); & quoted\n7\n1\n2\n\n"));
    assert(access("INJECTED",F_OK)!=0);
    result=fopen("catalog.log","r");assert(result);char log[64]={0};
    assert(fgets(log,sizeof(log),result));fclose(result);assert(!strcmp(log,"worker log\n"));
    helper=fopen("controller-idle.sh","w");assert(helper);fputs("printf 'idle ran\\n'\n",helper);fclose(helper);
    controller_activity=SDL_GetTicks()-300001;controller_idle();finish_worker(&idle_pid);
    helper=fopen("update.sh","w");assert(helper);fputs("printf 'update ran\\n'\n",helper);fclose(helper);
    start_update();finish_worker(&update_pid);
    unlink("greenlink-catalog");unlink("controller-idle.sh");unlink("update.sh");
    unlink("controller.log");unlink("update.log");unlink("catalog.log");unlink("catalog-cache/result.tsv");rmdir("catalog-cache");
    assert(!chdir("/"));assert(!rmdir(run));SDL_Quit();
    puts("PASS: catalog argv/redirection, controller helper and updater use the portable spawn layer");
    return 0;
}
