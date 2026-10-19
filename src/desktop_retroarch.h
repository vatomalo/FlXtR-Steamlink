#ifndef FLXTR_DESKTOP_RETROARCH_H
#define FLXTR_DESKTOP_RETROARCH_H
#ifdef FLXTR_DESKTOP
#include "platform_process.h"
static int desktop_find_program(const char *name,char *out,size_t cap){
#ifdef _WIN32
    const char *found=flxtr_win_executable(name);
    if(!found||flxtr_path_access(found,0))return 0;
    int n=snprintf(out,cap,"%s",found);return n>0&&n<(int)cap;
#else
    const char *path=getenv("PATH");if(!path)return 0;
    while(*path){
        const char *end=strchr(path,':');size_t n=end?(size_t)(end-path):strlen(path);
        if(n&&n+strlen(name)+2<cap){
            memcpy(out,path,n);out[n]='/';strcpy(out+n+1,name);
            if(!access(out,X_OK))return 1;
        }
        if(!end)break;
        path=end+1;
    }return 0;
#endif
}
static int desktop_retroarch(char *out,size_t cap){
    const char *explicit=getenv("FLXTR_RETROARCH");
    if(explicit&&*explicit){int n=snprintf(out,cap,"%s",explicit);return n>0&&n<(int)cap&&!access(out,X_OK)?1:0;}
    const char *home=getenv("FLXTR_RETROARCH_HOME");
    if(home&&*home){
        int n=snprintf(out,cap,"%s/retroarch"
#ifdef _WIN32
            ".exe"
#endif
            ,home);
        if(n>0&&n<(int)cap&&!access(out,X_OK))return 1;
    }
    if(desktop_find_program("retroarch",out,cap))return 1;
#ifdef _WIN32
    const char *variables[]={"ProgramFiles","ProgramFiles(x86)","LOCALAPPDATA"};
    const char *suffixes[]={"/RetroArch/retroarch.exe","/Steam/steamapps/common/RetroArch/retroarch.exe","/Programs/RetroArch/retroarch.exe"};
    for(size_t i=0;i<3;i++){
        const char *base=getenv(variables[i]);if(!base)continue;
        for(size_t j=0;j<3;j++){int n=snprintf(out,cap,"%s%s",base,suffixes[j]);if(n>0&&n<(int)cap&&!access(out,X_OK))return 1;}
    }
#else
    if(desktop_find_program("flatpak",out,cap)){
        const char *args[]={"flatpak","info","org.libretro.RetroArch",NULL};
        FlxtrProcess child={0};FlxtrSpawnOptions options={.stdout_path="/dev/null",.stderr_path="/dev/null"};
        if(!flxtr_process_start(&child,out,args,&options)){
            int code=1,result=flxtr_process_wait(&child,3000,&code);
            if(result==0){flxtr_process_stop(&child,1);flxtr_process_wait(&child,3000,NULL);}
            if(result==1&&!code)return 2;
        }
    }
#endif
    return 0;
}
static int desktop_run_game(const char *core,const char *rom,const char *extra){
    char runtime[PATH_MAX],data[PATH_MAX];int style=desktop_retroarch(runtime,sizeof(runtime));
    if(!style){
        FILE *error=fopen("game-error.txt","w");
        if(error){fputs("INSTALL RETROARCH OR SET FLXTR_RETROARCH TO ITS EXECUTABLE",error);fclose(error);}
        fprintf(stderr,"RetroArch not found: install native RetroArch, its Flatpak, or set FLXTR_RETROARCH\n");return 2;
    }
    char *args[32];int n=0;args[n++]=runtime;
#ifndef _WIN32
    char grant_data[PATH_MAX+32],grant_rom[PATH_MAX+32],parent[PATH_MAX];
    if(style==2){
        if(!getcwd(data,sizeof(data)))return 2;
        snprintf(parent,sizeof(parent),"%s",rom);char *slash=strrchr(parent,'/');if(!slash)return 2;if(slash==parent)parent[1]=0;else *slash=0;
        int a=snprintf(grant_data,sizeof(grant_data),"--filesystem=%s",data);
        int b=snprintf(grant_rom,sizeof(grant_rom),"--filesystem=%s:ro",parent);
        if(a<0||a>=(int)sizeof(grant_data)||b<0||b>=(int)sizeof(grant_rom))return 2;
        args[n++]="run";args[n++]=grant_data;args[n++]=grant_rom;args[n++]="org.libretro.RetroArch";
    }
#else
    (void)data;
#endif
    args[n++]="--verbose";args[n++]="-f";args[n++]="--appendconfig";args[n++]=(char*)extra;
    const char *frames=getenv("FLXTR_GAME_TEST_FRAMES");
    if(frames&&positive(frames,0)>0&&positive(frames,0)<=3600){
        args[n++]="--max-frames";args[n++]=(char*)frames;
    }
    args[n++]="--libretro";args[n++]=(char*)core;args[n++]=(char*)rom;args[n]=NULL;
    execv(runtime,args);perror("RetroArch");return 1;
}
#endif
#endif
