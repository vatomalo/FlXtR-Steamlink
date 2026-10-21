/* Verify the native UI/game restart protocol without opening emulator windows. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "../src/windows_runtime.h"
static int fixture_run(const char *,const char *const *,const char *,const char *);
#define flxtr_win_run fixture_run
#include "../src/shell.c"
#undef flxtr_win_run
static int phase,restart_only;
static int fixture_run(const char *file,const char *const *args,const char *out,const char *err){
    (void)out;(void)err;
    if(restart_only){assert(!strcmp(file,"FlXtR"));assert(!strcmp(args[1],"--ui-worker"));return phase++?0:42;}
    if(phase==0){assert(!strcmp(file,"FlXtR")&&!strcmp(args[1],"--ui-worker"));phase++;return 43;}
    if(phase==1){assert(!strcmp(file,"greenlink-catalog")&&!strcmp(args[1],"--run-game"));phase++;return 7;}
    assert(phase==2&&!strcmp(file,"FlXtR")&&!strcmp(args[1],"--ui-worker"));
    assert(!strcmp(args[2],"--library")&&!strcmp(args[3],"games")&&!args[4]);phase++;return 0;
}
int wmain(void){
    char *argv[]={"FlXtR",NULL};
    assert(windows_supervisor(1,argv)==0&&phase==3);
    FILE *f=fopen("game-exit-status","r");int code=0;assert(f&&fscanf(f,"%d",&code)==1&&code==7);fclose(f);
    phase=0;restart_only=1;assert(windows_supervisor(1,argv)==0&&phase==2);
    puts("PASS: Windows game handoff, exit status and nonrecursive UI restart");return 0;
}
