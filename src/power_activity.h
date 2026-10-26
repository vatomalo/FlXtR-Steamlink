#ifndef FLXTR_POWER_ACTIVITY_H
#define FLXTR_POWER_ACTIVITY_H
/* Keep firmware awake during video sessions without blocking the UI. */
static pid_t power_pid;
#if (!defined(_WIN32) && !defined(FLXTR_DESKTOP)) || defined(FLXTR_POWER_TEST)
static int power_active;
static Uint32 power_started;
static void power_activity_tick(int active,Uint32 now){
    active=!!active;
    if(power_pid){
        pid_t done=waitpid(power_pid,NULL,WNOHANG);
        if(done==power_pid||(done<0&&errno==ECHILD))power_pid=0;
        else{
            if(now-power_started>2000)kill(power_pid,SIGKILL);
            return;
        }
    }
    if(active==power_active&&(!active||now-power_started<10000))return;
    if(access("./power-activity.sh",R_OK))return;
    const char *args[]={"sh","./power-activity.sh",active?"--once":"--idle",NULL};
    power_pid=shell_spawn("/bin/sh",args,"power-activity.log","power-activity.log");
    power_started=now;
    if(power_pid>0)power_active=active;else power_pid=0;
}
#else
static void power_activity_tick(int active,Uint32 now){(void)active;(void)now;}
#endif
#endif
