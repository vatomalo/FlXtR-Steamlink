#define FLXTR_DESKTOP
#define FLXTR_POWER_TEST
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void reap(void){assert(power_pid>0);assert(waitpid(power_pid,NULL,0)==power_pid);power_pid=0;}
static void mode(const char *expected){
    char value[32]={0};FILE *f=fopen("mode.txt","r");assert(f);assert(fgets(value,sizeof(value),f));fclose(f);assert(!strcmp(value,expected));
}
int main(void){
    char dir[]="/tmp/flxtr-power-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    FILE *f=fopen("power-activity.sh","w");assert(f);fputs("#!/bin/sh\nprintf '%s' \"$1\" >mode.txt\n",f);fclose(f);
    power_activity_tick(0,1);assert(!power_pid&&access("mode.txt",F_OK));
    power_activity_tick(1,2);reap();mode("--once");
    power_activity_tick(1,10001);assert(!power_pid);
    power_activity_tick(1,10002);reap();mode("--once");
    power_activity_tick(0,10003);reap();mode("--idle");
    power_activity_tick(0,30000);assert(!power_pid);
    /* A stalled helper cannot block playback or create overlapping workers. */
    f=fopen("power-activity.sh","w");assert(f);fputs("#!/bin/sh\nexec sleep 20\n",f);fclose(f);
    power_activity_tick(1,40000);pid_t child=power_pid;assert(child>0);
    power_activity_tick(1,40001);assert(power_pid==child);
    power_activity_tick(1,42001);assert(waitpid(child,NULL,0)==child);power_pid=0;
    puts("PASS: menu idle, video heartbeat, bounded refresh rate, release, and stalled-worker cleanup");
}
