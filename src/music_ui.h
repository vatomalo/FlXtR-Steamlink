/* Small native audio browser. Workers exist only during lookup or playback. */
#ifndef FLXTR_MUSIC_UI_H
#define FLXTR_MUSIC_UI_H
typedef struct {char kind[8],key[221],title[151],artist[151];} MusicRow;
static MusicRow music_rows[100];
static int music_on,music_edit,music_key,music_count,music_selected,music_job,music_paused,music_play_index;
static pid_t music_pid;
static Uint32 music_started;
static char music_query[221],music_message[181]="START SEARCH / PASTE A PUBLIC SPOTIFY PLAYLIST URL",music_playing[151];
static void music_stop(void){
    if(music_pid){kill(-music_pid,SIGKILL);kill(music_pid,SIGKILL);waitpid(music_pid,NULL,0);music_pid=0;}
    music_job=music_paused=0;
}
static void music_spawn(int job){
    music_stop();mkdir("music-cache",0700);unlink("music-cache/error.txt");
    if(job==2){unlink("music-cache/stream.txt");unlink("music-cache/playing.txt");}
    if(job==2)music_play_index=music_selected;
    music_job=job;music_started=SDL_GetTicks();music_pid=shell_fork();
    if(!music_pid){
        setpgid(0,0);
        int out=open(job==1?"music-cache/results.tsv":"music.log",O_CREAT|O_WRONLY|O_TRUNC,0600);
        int err=open("music.log",O_CREAT|O_WRONLY|O_TRUNC,0600);
        if(out<0||err<0)_exit(126);
        dup2(out,1);dup2(err,2);close(out);close(err);
        if(job==3)execl("./greenlink-audio","greenlink-audio","music-cache/stream.txt",(char*)NULL);
        else if(job==1)execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py","search",music_query,(char*)NULL);
        else execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py","resolve",music_rows[music_selected].kind,music_rows[music_selected].key,(char*)NULL);
        _exit(127);
    }
    if(music_pid<0){music_pid=0;music_job=0;snprintf(music_message,sizeof(music_message),"COULD NOT START MUSIC WORKER");return;}
    setpgid(music_pid,music_pid);
    snprintf(music_message,sizeof(music_message),"%s",job==1?"SEARCHING... B CANCEL":job==2?"FINDING YOUTUBE AUDIO... B CANCEL":"PLAYING / X PAUSE / A PLAY SELECTED / B EXIT");
}
static int music_poll(void){
    if(!music_pid)return 0;
    int code;pid_t done=waitpid(music_pid,&code,WNOHANG);
    if(done!=music_pid){
        if(music_job!=3&&SDL_GetTicks()-music_started>120000){music_stop();strcpy(music_message,"LOOKUP TIMED OUT / START TO TRY AGAIN");return 1;}
        return 0;
    }
    music_pid=0;int job=music_job;music_job=0;music_paused=0;
    if(!WIFEXITED(code)||WEXITSTATUS(code)){
        strcpy(music_message,"MUSIC UNAVAILABLE / START RETRY / DETAILS IN MUSIC.LOG");
        FILE *f=fopen("music-cache/error.txt","r");if(f){if(!fgets(music_message,sizeof(music_message),f))music_message[0]=0;fclose(f);}
        return 1;
    }
    if(job==1){
        music_count=music_selected=0;char line[700];FILE *f=fopen("music-cache/results.tsv","r");
        if(f){while(music_count<100&&fgets(line,sizeof(line),f)){
            MusicRow row={0};char *parts[4],*p=line;int n=0;
            while(n<4){parts[n++]=p;char *tab=strchr(p,'\t');if(!tab)break;*tab=0;p=tab+1;}
            if(n!=4||strlen(parts[0])>=sizeof(row.kind)||strlen(parts[1])>=sizeof(row.key))continue;
            if(strcmp(parts[0],"video")&&strcmp(parts[0],"query"))continue;
            strcpy(row.kind,parts[0]);strcpy(row.key,parts[1]);
            snprintf(row.title,sizeof(row.title),"%s",parts[2]);parts[3][strcspn(parts[3],"\r\n")]=0;
            snprintf(row.artist,sizeof(row.artist),"%s",parts[3]);music_rows[music_count++]=row;
        }fclose(f);}
        snprintf(music_message,sizeof(music_message),"%d TRACKS / A PLAY / START SEARCH / B EXIT",music_count);
    }else if(job==2){
        FILE *f=fopen("music-cache/playing.txt","r");music_playing[0]=0;
        if(f){if(!fgets(music_playing,sizeof(music_playing),f))music_playing[0]=0;fclose(f);music_playing[strcspn(music_playing,"\r\n")]=0;}
        music_spawn(3);
    }else if(job==3){
        if(music_play_index+1<music_count){music_selected=music_play_index+1;music_spawn(2);}
        else strcpy(music_message,"PLAYLIST FINISHED / START SEARCH / A PLAY AGAIN");
    }
    return 1;
}
static void music_text(const char *s){
    size_t n=strlen(music_query);
    while(*s&&n<sizeof(music_query)-1){if((unsigned char)*s>=32)music_query[n++]=*s;s++;}
    music_query[n]=0;
}
static void music_action(SDL_Keycode key){
    if(music_edit){
        size_t n=strlen(music_query);
        if(key==SDLK_ESCAPE){music_edit=0;SDL_StopTextInput();return;}
        if(key==SDLK_BACKSPACE){if(n)music_query[n-1]=0;else{music_edit=0;SDL_StopTextInput();}}
        else if(key==SDLK_v&&(SDL_GetModState()&KMOD_CTRL)){char *p=SDL_GetClipboardText();if(p){music_text(p);SDL_free(p);}}
        else if(key==SDLK_LEFT&&music_key>0)music_key--;
        else if(key==SDLK_RIGHT&&music_key<39)music_key++;
        else if(key==SDLK_UP&&music_key>=10)music_key-=10;
        else if(key==SDLK_DOWN&&music_key<30)music_key+=10;
        else if(key==SDLK_F4){char c=search_keys[music_key];if(c=='<'){if(n)music_query[n-1]=0;}else if(c=='>'){music_edit=0;SDL_StopTextInput();music_spawn(1);}else{char s[2]={c,0};music_text(s);}}
        else if(key==SDLK_RETURN||key==SDLK_F3){music_edit=0;SDL_StopTextInput();music_spawn(1);}
        return;
    }
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){music_on=0;return;}
    if(key==SDLK_F3||key==SDLK_SLASH){music_edit=1;music_key=0;SDL_StartTextInput();return;}
    if(music_job==1||music_job==2)return;
    if(key==SDLK_UP&&music_selected>0)music_selected--;
    if(key==SDLK_DOWN&&music_selected+1<music_count)music_selected++;
    if(key==SDLK_LEFT)music_selected=music_selected>=6?music_selected-6:0;
    if(key==SDLK_RIGHT&&music_count)music_selected=music_selected+6<music_count?music_selected+6:music_count-1;
    if(key==SDLK_RETURN&&music_count)music_spawn(2);
    if(key==SDLK_F2&&music_job==3){kill(music_pid,SIGUSR1);music_paused=!music_paused;snprintf(music_message,sizeof(music_message),"%s / X TOGGLE / B EXIT",music_paused?"PAUSED":"PLAYING");}
}
/* Scrolling title is rendered with bounded text; no texture allocation. */
static void music_now_playing(int x,int y,int columns,SDL_Color color){
    if(music_job!=3&&music_job!=2&&music_paused==0)return;
    const char *title=music_playing[0]?music_playing:(music_play_index<music_count?music_rows[music_play_index].title:"");
    if(!*title)return;
    char visible[160];size_t n=strlen(title),length=(size_t)columns;
    if(length>=sizeof(visible))length=sizeof(visible)-1;
    size_t start=0;
    if(n>length){
        /* Pause at the edges, then sweep from start to end. */
        size_t travel=n-length,period=2*travel+12;
        size_t tick=(SDL_GetTicks()/230)%period;
        if(tick>=3&&tick<3+travel)start=tick-3;
        else if(tick>=3+travel&&tick<9+travel)start=travel;
        else if(tick>=9+travel)start=travel-(tick-9-travel);
    }
    snprintf(visible,sizeof(visible),"%.*s",(int)length,title+start);
    text(x,y,visible,1,color,columns);
}
static void music_draw(void){
    SDL_Color bg={8,12,10,255},accent={116,255,132,255},muted={145,165,151,255};
    rect(0,0,W,H,bg,1);text(28,24,"MUSIC / YOUTUBE AUDIO",3,accent,45);
    if(music_edit){
        text(30,85,"SEARCH OR PASTE PUBLIC SPOTIFY PLAYLIST URL",2,accent,70);
        text(30,125,music_query,1,white,145);
        for(int i=0;i<40;i++){
            int x=70+(i%10)*80,y=185+(i/10)*53;char c[2]={search_keys[i],0};
            rect(x-5,y-5,46,36,i==music_key?accent:muted,0);text(x+8,y,c,3,accent,1);
        }
        text(30,425,"A TYPE / B DELETE / START SEARCH / ESC CANCEL",2,muted,70);
        text(30,460,"KEYBOARD: CTRL+V PASTES A PLAYLIST LINK",1,muted,90);
    }else{
        char counter[72];
        snprintf(counter,sizeof(counter),"TRACK %d / %d  %s",music_count?music_selected+1:0,music_count,music_job==3?(music_paused?"PAUSED":"PLAYING"):"LIBRARY");
        text(30,61,counter,1,muted,90);
        int first=music_selected/6*6;
        for(int i=first;i<music_count&&i<first+6;i++){
            int y=88+(i-first)*53;
            if(i==music_selected)rect(24,y-6,912,49,accent,0);
            if(i==music_play_index&&(music_job==3||music_job==2))text(887,y,"[>]",2,accent,4);
            text(38,y,music_rows[i].title,2,accent,73);text(38,y+22,music_rows[i].artist,1,muted,130);
        }
        if(!music_count)text(30,130,"PRESS START TO SEARCH YOUTUBE OR IMPORT A PLAYLIST",2,accent,73);
        if(*music_playing){text(30,421,"YOUTUBE MATCH:",1,muted,20);text(30,440,music_playing,1,accent,140);}
        text(30,466,music_message,1,accent,145);
        text(30,503,"A PLAY / X PAUSE / START SEARCH / B BACK (MUSIC CONTINUES)",1,muted,100);
    }
}
#endif
