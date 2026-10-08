#include "app.h"
#include "tilefinch/media_backend.h"
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
PSP_MODULE_INFO("FlXtR",0,0,1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(20*1024);
atomic_bool quitting;
char status_text[256]="Beta: direct MP4 streams; hardware testing required";
typedef struct {char title[96],url[2048];} Entry;
static Entry library[64];static int count;
void status(const char *fmt,...){
    va_list ap;va_start(ap,fmt);vsnprintf(status_text,sizeof(status_text),fmt,ap);va_end(ap);
    FILE *log=fopen("app.log","a");if(log){fprintf(log,"%s\n",status_text);fclose(log);}
    pspDebugScreenSetXY(0,24);pspDebugScreenPrintf("%-68.68s\n%-68.68s",status_text,status_text+ (strlen(status_text)>68?68:strlen(status_text)));
}
static int exit_cb(int a,int b,void *p){(void)a;(void)b;(void)p;atomic_store(&quitting,true);return 0;}
static int callbacks(SceSize n,void *p){(void)n;(void)p;int cb=sceKernelCreateCallback("exit",exit_cb,NULL);sceKernelRegisterExitCallback(cb);sceKernelSleepThreadCB();return 0;}
static void load_library(void){
    count=0;FILE *f=fopen("streams.tsv","r");if(!f)return;
    char line[2200];while(count<64 && fgets(line,sizeof(line),f)){
        if(line[0]=='#')continue;
        char *tab=strchr(line,'\t');if(!tab)continue;*tab++=0;
        tab[strcspn(tab,"\r\n")]=0;
        if(strncmp(tab,"https://",8) && strncmp(tab,"http://",7))continue;
        if(strlen(tab)>=sizeof(library[0].url) || strlen(line)>=sizeof(library[0].title))continue;
        strcpy(library[count].title,line);strcpy(library[count++].url,tab);
    }fclose(f);
}
int main(int argc,char **argv){
    if(argc && argv[0]){char dir[512];snprintf(dir,sizeof(dir),"%s",argv[0]);char *s=strrchr(dir,'/');if(s){*s=0;chdir(dir);}}
    atomic_init(&quitting,false);
    FILE *log=fopen("app.log","w");if(log){fprintf(log,"FlXtR PSP %s\n",FLXTR_VERSION);fclose(log);}
    int thread=sceKernelCreateThread("callbacks",callbacks,0x11,4096,0,NULL);if(thread>=0)sceKernelStartThread(thread,0,NULL);
    scePowerSetClockFrequency(333,333,166);
    pspDebugScreenInit();pspDebugScreenSetBackColor(0);pspDebugScreenSetTextColor(0x0000ff60);
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    media_psp_backend_reserve_pool();load_library();
    int selected=0,profile=1;unsigned prev=0;bool redraw=true,update_checked=false,updated=false;
    while(!atomic_load(&quitting)){
        if(redraw){
            pspDebugScreenClear();pspDebugScreenPrintf("FlXtR PSP BETA  %.8s\n\n",FLXTR_VERSION);
            pspDebugScreenPrintf("X Play   Circle Stop   Triangle Reload   [] Update\nL/R Wi-Fi profile: %d   HOME Exit\n\n",profile);
            if(!count)pspDebugScreenPrintf("Add Title<TAB>https://...mp4 to streams.tsv\n");
            int first=selected/15*15;
            for(int i=first;i<count && i<first+15;i++)pspDebugScreenPrintf("%c %.62s\n",i==selected?'>':' ',library[i].title);
            char saved[256];strcpy(saved,status_text);status("%s",saved);redraw=false;
        }
        SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);unsigned press=pad.Buttons&~prev;prev=pad.Buttons;
        if(press&PSP_CTRL_UP){if(selected>0)selected--;redraw=true;}
        if(press&PSP_CTRL_DOWN){if(selected+1<count)selected++;redraw=true;}
        if(press&PSP_CTRL_LTRIGGER){if(profile>1)profile--;redraw=true;}
        if(press&PSP_CTRL_RTRIGGER){if(profile<10)profile++;redraw=true;}
        if(press&PSP_CTRL_TRIANGLE){load_library();selected=0;redraw=true;}
        if((press&PSP_CTRL_SQUARE) && !updated){
            status("Connecting for updates...");
            if(connect_wifi(profile)==0){updated=check_update()==1;update_checked=true;}
            redraw=true;
        }
        if((press&PSP_CTRL_CROSS) && count && !updated){
            status("Connecting to saved Wi-Fi profile %d...",profile);
            if(connect_wifi(profile)==0){
                if(!update_checked){updated=check_update()==1;update_checked=true;}
                if(!updated)play_url(library[selected].url);
            }
            redraw=true;
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitGame();return 0;
}
