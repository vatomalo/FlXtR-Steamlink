#define _POSIX_C_SOURCE 200809L
/* Native audio worker; video and audio share the same packaged libmpv. */
#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windows_runtime.h"
#ifndef _WIN32
#include <signal.h>
static volatile sig_atomic_t toggle_pause;
static void audio_pause(int sig){(void)sig;toggle_pause=1;}
#endif
static int audio_main(int argc,char **argv){
    if(argc!=2&&(argc!=3||strcmp(argv[2],"--headless")))return 2;
    FILE *f=fopen(argv[1],"rb");char url[16004],agent[512]="Mozilla/5.0";
    if(!f)return 2;
    int good=fgets(url,sizeof(url),f)!=NULL;
    if(!fgets(agent,sizeof(agent),f))strcpy(agent,"Mozilla/5.0");
    fclose(f);url[strcspn(url,"\r\n")]=0;agent[strcspn(agent,"\r\n")]=0;
    if(!good||(!strncmp(url,"http://",7)?0:strncmp(url,"https://",8)))return 2;
    mpv_handle *mpv=mpv_create();if(!mpv)return 1;
    mpv_set_option_string(mpv,"config","no");mpv_set_option_string(mpv,"video","no");
    mpv_set_option_string(mpv,"terminal","no");
    if(argc==3)mpv_set_option_string(mpv,"ao","null");
#ifdef _WIN32
    mpv_set_option_string(mpv,"tls-ca-file",FLXTR_CA_BUNDLE);
#endif
mpv_set_option_string(mpv,"user-agent",agent);
    #ifdef _WIN32
    wchar_t name[80];swprintf(name,80,L"Local\\FlXtR-audio-pause-%lu",GetCurrentProcessId());
    SECURITY_ATTRIBUTES security;if(flxtr_private_security(&security)){mpv_terminate_destroy(mpv);return 1;}
    HANDLE pause=CreateEventW(&security,FALSE,FALSE,name);LocalFree(security.lpSecurityDescriptor);
    if(!pause||mpv_initialize(mpv)<0){if(pause)CloseHandle(pause);mpv_terminate_destroy(mpv);return 1;}
    #else
    signal(SIGUSR1,audio_pause);
    if(mpv_initialize(mpv)<0){mpv_terminate_destroy(mpv);return 1;}
    #endif
    const char *load[]={"loadfile",url,NULL};int result=mpv_command(mpv,load)<0?1:0;
    while(!result){
        #ifdef _WIN32
        if(WaitForSingleObject(pause,0)==WAIT_OBJECT_0){const char *args[]={"cycle","pause",NULL};mpv_command(mpv,args);}
        #else
        if(toggle_pause){toggle_pause=0;const char *args[]={"cycle","pause",NULL};mpv_command(mpv,args);}
        #endif
        mpv_event *event=mpv_wait_event(mpv,0.05);
        if(event->event_id==MPV_EVENT_SHUTDOWN){result=1;break;}
        if(event->event_id==MPV_EVENT_END_FILE){
            mpv_event_end_file *end=event->data;result=end&&end->reason==MPV_END_FILE_REASON_EOF?0:1;break;
        }
    }
    #ifdef _WIN32
    CloseHandle(pause);
    #endif
    mpv_terminate_destroy(mpv);return result;
}
#ifdef _WIN32
FLXTR_WINDOWS_CONSOLE_ENTRY(audio_main)
#else
int main(int argc,char **argv){return audio_main(argc,argv);}
#endif
