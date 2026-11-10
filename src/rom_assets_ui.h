#ifndef FLXTR_ROM_ASSETS_UI_H
#define FLXTR_ROM_ASSETS_UI_H
enum {ROM_ASSET_CORE,ROM_ASSET_COVER,ROM_ASSET_CHEATS,ROM_ASSET_STATES,ROM_ASSET_AUDIO,ROM_ASSET_TABS};
typedef struct {char token[20],label[192],detail[192];} RomAssetRow;
static RomAssetRow rom_asset_rows[2049];
static int rom_asset_tab,rom_asset_count,rom_asset_selected,rom_asset_first,rom_asset_files,rom_asset_cover_dirty;
static pid_t rom_asset_pid;
static Uint32 rom_asset_started;
static char rom_asset_job[16],rom_asset_message[160];
static char rom_asset_confirm[20];
static void rom_assets_stop(void){
    if(rom_asset_pid>0){
#ifndef _WIN32
        kill(-rom_asset_pid,SIGKILL);
#endif
        kill(rom_asset_pid,SIGKILL);waitpid(rom_asset_pid,NULL,0);rom_asset_pid=0;
    }
}
static void rom_assets_start(const char *job,const char *value){
    rom_assets_stop();
    snprintf(rom_asset_job,sizeof(rom_asset_job),"%s",job);
    snprintf(rom_asset_message,sizeof(rom_asset_message),"WORKING...");
    unlink("rom-assets.tsv");
#ifdef _WIN32
    char python[PATH_MAX];snprintf(python,sizeof(python),"%s/music-runtime/bin/python.exe",flxtr_win_binary_dir());
    const char *args[]={"python","-I","tools/rom_assets.py",job,rom_core_key,value,NULL};
    rom_asset_pid=flxtr_win_spawn(python,args,"rom-assets.log","rom-assets.log",NULL,0,1);
#else
    pid_t pid=shell_fork();
    if(pid==0){
        setpgid(0,0);int fd=open("rom-assets.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(fd>=0){dup2(fd,1);dup2(fd,2);close(fd);}
        execl("/bin/sh","sh","scripts/music-runtime.sh","tools/rom_assets.py",job,rom_core_key,value,(char*)NULL);
        _exit(127);
    }
    if(pid>0)setpgid(pid,pid);
    rom_asset_pid=pid;
#endif
    if(rom_asset_pid<1){rom_asset_pid=0;snprintf(rom_asset_message,sizeof(rom_asset_message),"COULD NOT START ROM WORKER");}
    rom_asset_started=SDL_GetTicks();
}
static void rom_assets_tab(int tab){
    rom_assets_stop();rom_asset_tab=tab;rom_asset_files=0;
    rom_asset_count=rom_asset_selected=rom_asset_first=0;rom_asset_message[0]=0;
    rom_asset_confirm[0]=0;
    if(tab==ROM_ASSET_COVER)rom_assets_start("covers",NULL);
    if(tab==ROM_ASSET_CHEATS)rom_assets_start("cheats",NULL);
    if(tab==ROM_ASSET_AUDIO)rom_assets_start("audio",NULL);
    if(tab==ROM_ASSET_STATES)rom_assets_start("states",NULL);
}
static void rom_assets_open(const Title *title){
    if(strcmp(title->kind,"game")||strncmp(title->url,"romid:",6)||!rom_key_valid(title->url+6))return;
    preview_stop();rom_options_menu=0;rom_options_title=*title;
    rom_core_open(title);rom_core_menu=0;rom_assets_on=1;rom_asset_cover_dirty=0;
    rom_assets_tab(ROM_ASSET_CORE);
}
static void rom_assets_read(void){
    rom_asset_count=0;
    FILE *f=fopen("rom-assets.tsv","r");if(!f)return;
    char line[640];
    while(rom_asset_count<2049&&fgets(line,sizeof(line),f)){
        char *a=strchr(line,'\t');if(!a)continue;*a++=0;
        char *b=strchr(a,'\t');if(!b)continue;*b++=0;b[strcspn(b,"\r\n")]=0;
        if(strlen(line)>=sizeof(rom_asset_rows[0].token))continue;
        RomAssetRow *r=&rom_asset_rows[rom_asset_count++];
        snprintf(r->token,sizeof(r->token),"%.19s",line);
        snprintf(r->label,sizeof(r->label),"%.191s",a);snprintf(r->detail,sizeof(r->detail),"%.191s",b);
    }
    fclose(f);
    if(rom_asset_selected>=rom_asset_count)rom_asset_selected=rom_asset_count?rom_asset_count-1:0;
    if(rom_asset_first>rom_asset_selected)rom_asset_first=rom_asset_selected;
}
static int rom_assets_tick(void){
    if(!rom_asset_pid)return 0;
    int code=0;pid_t child=waitpid(rom_asset_pid,&code,WNOHANG);
    if(child==rom_asset_pid){
        rom_asset_pid=0;
        if(WIFEXITED(code)&&WEXITSTATUS(code)==0){
            if(!strcmp(rom_asset_job,"search"))rom_asset_files=1;
            if(!strcmp(rom_asset_job,"install")){rom_asset_files=0;rom_asset_selected=rom_asset_first=0;}
            if(!strcmp(rom_asset_job,"cover-set"))rom_asset_cover_dirty=1;
            rom_assets_read();
            if(!strcmp(rom_asset_job,"covers"))for(int i=0;i<rom_asset_count;i++)
                if(!strcmp(rom_asset_rows[i].detail,"CURRENT")){rom_asset_selected=i;rom_asset_first=i>7?i-7:0;break;}
            snprintf(rom_asset_message,sizeof(rom_asset_message),"%s",rom_asset_files?"CHOOSE THE CORRECT GAME / REGION / REVISION":
                rom_asset_tab==ROM_ASSET_AUDIO?"A TOGGLE BACKGROUND MUSIC / GAME SOUND EFFECTS ARE KEPT":
                rom_asset_tab==ROM_ASSET_STATES?(!strcmp(rom_asset_job,"state-recover")?"SHADOW RESTORED / REPLACED FILE PRESERVED":"BACK+RB SAVE / BACK+LB LOAD / AUTO RESUME IS SEPARATE"):
                rom_asset_tab==ROM_ASSET_CHEATS?"CHEAT CHANGES APPLY ON NEXT GAME LAUNCH":"A SAVE COVER / FIRST ROW RESETS TO AUTOMATIC");
        }else{
            FILE *f=fopen("rom-assets.log","r");
            snprintf(rom_asset_message,sizeof(rom_asset_message),"ROM WORKER FAILED / SEE ROM-ASSETS.LOG");
            if(f){if(fgets(rom_asset_message,sizeof(rom_asset_message),f))rom_asset_message[strcspn(rom_asset_message,"\r\n")]=0;fclose(f);}
        }
        return 1;
    }
    if(SDL_GetTicks()-rom_asset_started>90000){rom_assets_stop();snprintf(rom_asset_message,sizeof(rom_asset_message),"DOWNLOAD TIMED OUT / A TO RETRY");return 1;}
    return 0;
}
static int rom_assets_action(SDL_Keycode key){
    if(!rom_assets_on||rom_core_menu)return 0;
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5){
        if(rom_asset_confirm[0]){rom_asset_confirm[0]=0;snprintf(rom_asset_message,sizeof(rom_asset_message),"RECOVERY CANCELLED");return 1;}
        if(rom_asset_files){rom_assets_tab(ROM_ASSET_CHEATS);return 1;}
        rom_assets_stop();rom_assets_on=0;
        if(rom_asset_cover_dirty){memset(library_pages,0,sizeof(library_pages));request_catalog(browse,0,0);}
        return 1;
    }
    if(key==SDLK_m||key==SDLK_TAB||key==SDLK_PAGEDOWN)rom_assets_tab((rom_asset_tab+1)%ROM_ASSET_TABS);
    else if(key==SDLK_v||key==SDLK_PAGEUP)rom_assets_tab((rom_asset_tab+ROM_ASSET_TABS-1)%ROM_ASSET_TABS);
    else if(!rom_asset_pid){
        if(key==SDLK_UP||key==SDLK_DOWN||key==SDLK_LEFT||key==SDLK_RIGHT)rom_asset_confirm[0]=0;
        if(rom_asset_tab==ROM_ASSET_STATES&&rom_asset_selected==0&&(key==SDLK_LEFT||key==SDLK_RIGHT)){
            rom_assets_start("state-slot",key==SDLK_LEFT?"-1":"1");return 1;
        }
        if(key==SDLK_UP&&rom_asset_selected>0)rom_asset_selected--;
        if(key==SDLK_DOWN&&rom_asset_selected+1<rom_asset_count)rom_asset_selected++;
        if(key==SDLK_LEFT&&rom_asset_tab!=ROM_ASSET_CORE){rom_asset_selected-=8;if(rom_asset_selected<0)rom_asset_selected=0;}
        if(key==SDLK_RIGHT&&rom_asset_tab!=ROM_ASSET_CORE){rom_asset_selected+=8;if(rom_asset_selected>=rom_asset_count)rom_asset_selected=rom_asset_count?rom_asset_count-1:0;}
        if(rom_asset_selected<rom_asset_first)rom_asset_first=rom_asset_selected;
        if(rom_asset_selected>=rom_asset_first+8)rom_asset_first=rom_asset_selected-7;
        if(key==SDLK_RETURN){
            if(rom_asset_tab==ROM_ASSET_CORE)rom_core_menu=1;
            else if(rom_asset_count){
                char selected[20];snprintf(selected,sizeof(selected),"%s",rom_asset_rows[rom_asset_selected].token);
                if(!strcmp(selected,"none"))return 1;
                if(rom_asset_tab==ROM_ASSET_AUDIO){rom_assets_start("audio-toggle",NULL);return 1;}
                if(rom_asset_tab==ROM_ASSET_STATES){
                    if(!strcmp(selected,"slot"))rom_assets_start("state-slot","1");
                    else if(!strcmp(selected,"backup"))rom_assets_start("states",NULL);
                    else if(!strcmp(selected,rom_asset_confirm)){
                        rom_asset_confirm[0]=0;rom_assets_start("state-recover",selected);
                    }else{
                        snprintf(rom_asset_confirm,sizeof(rom_asset_confirm),"%s",selected);
                        snprintf(rom_asset_message,sizeof(rom_asset_message),"RESTORE SELECTED SHADOW? A CONFIRM / B CANCEL / CURRENT FILE IS PRESERVED");
                    }
                    return 1;
                }
                const char *job=rom_asset_tab==ROM_ASSET_COVER?"cover-set":rom_asset_files?"install":!strcmp(selected,"download")?"search":"toggle";
                rom_assets_start(job,!strcmp(job,"search")?rom_options_title.title:selected);
            }else rom_assets_tab(rom_asset_tab);
        }
    }
    return 1;
}
static void rom_assets_draw(void){
    rect(184,80,746,399,(SDL_Color){0,0,0,240},1);
    text(200,94,"ROM SETTINGS",2,green,50);text(200,123,rom_options_title.title,1,white,100);
    const char *tabs[]={"CORE","COVER","CHEATS","STATES","AUDIO"};
    for(int i=0;i<ROM_ASSET_TABS;i++){
        int x=200+i*140;if(i==rom_asset_tab)rect(x-4,142,132,26,green,0);
        text(x,150,tabs[i],2,i==rom_asset_tab?green:dim,20);
    }
    if(rom_asset_tab==ROM_ASSET_CORE){
        text(205,202,"CORE FOR THIS ROM",2,white,50);
        text(205,240,rom_core_names[rom_core_selected],2,green,55);
        text(205,293,"AUTOMATIC USES THE CONSOLE DEFAULT",1,dim,90);
        text(205,320,"A CHOOSE CORE / PER-ROM CHOICE OVERRIDES CONSOLE",1,green,100);
    }else if(!rom_asset_pid){
        for(int i=rom_asset_first;i<rom_asset_count&&i<rom_asset_first+8;i++){
            int y=190+(i-rom_asset_first)*26;
            if(i==rom_asset_selected)rect(195,y-5,717,24,green,0);
            RomAssetRow *row=&rom_asset_rows[i];text(204,y,row->label,1,i==rom_asset_selected?green:white,95);
            if(rom_asset_tab==ROM_ASSET_CHEATS&&!rom_asset_files&&i>0)text(870,y,row->detail,1,green,6);
            if(rom_asset_tab==ROM_ASSET_COVER&&!strcmp(row->detail,"CURRENT"))text(855,y,"SET",1,green,6);
        }
        if(rom_asset_files&&rom_asset_count)text(200,393,rom_asset_rows[rom_asset_selected].detail,1,dim,105);
        if((rom_asset_tab==ROM_ASSET_STATES||rom_asset_tab==ROM_ASSET_AUDIO)&&rom_asset_count)text(200,393,rom_asset_rows[rom_asset_selected].detail,1,dim,105);
    }else text(205,225,"LOADING... B CANCEL",2,green,55);
    text(200,413,rom_asset_message,1,dim,108);
    text(200,443,"LB/RB OR TAB: TABS / A SELECT / B BACK",1,green,100);
    text(200,463,rom_asset_tab==ROM_ASSET_STATES?"LEFT/RIGHT: SLOT OR PAGE / A RECOVER SELECTED COPY":"UP/DOWN: ROW / LEFT/RIGHT: PAGE",1,dim,110);
    SDL_RenderPresent(renderer);
}
#endif
