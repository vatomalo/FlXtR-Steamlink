/* Small native audio browser. Workers exist only during lookup or playback. */
#ifndef FLXTR_MUSIC_UI_H
#define FLXTR_MUSIC_UI_H
typedef struct {char kind[8],key[221],title[151],artist[151];} MusicRow;
static MusicRow music_rows[100];
static MusicRow music_queue[100],music_track;
static int music_queue_count,music_advancing,music_tab,music_loaded,music_lookup_job;
static int music_discover_current; /* 0 = weighted genres; 1 = playing track */
static pid_t music_lookup_pid;
static Uint32 music_lookup_started;
enum { MUSIC_RECENTS, MUSIC_DISCOVER, MUSIC_LIBRARY, MUSIC_SEARCH };
#include "mtv_weights.h"
/* The existing MTV fusion builder is defined in youtube_video.h. */
static void mtv_fusion_query(int anchor,char *out,size_t size);
static void mtv_weights_open(void);
static void music_spawn(int job);
static int music_on,music_edit,music_key,music_count,music_selected,music_job,music_paused,music_play_index,music_phone_play;
static pid_t music_pid;
static Uint32 music_started;
static size_t music_cursor;
static char music_query[221],music_message[181]="START SEARCH / PASTE A PUBLIC SPOTIFY PLAYLIST URL",music_playing[151];
static int music_load_rows(const char *path,MusicRow *rows){
    int count=0;
    FILE *f=fopen(path,"r");if(!f)return 0;
    char line[700];
    while(count<100&&fgets(line,sizeof(line),f)){
        MusicRow row={0};char *parts[4],*p=line;int n=0;
        while(n<4){parts[n++]=p;char *tab=strchr(p,'\t');if(!tab)break;*tab=0;p=tab+1;}
        if(n!=4||strlen(parts[0])>=sizeof(row.kind)||strlen(parts[1])>=sizeof(row.key))continue;
        if(strcmp(parts[0],"video")&&strcmp(parts[0],"query")&&strcmp(parts[0],"local"))continue;
        strcpy(row.kind,parts[0]);strcpy(row.key,parts[1]);
        snprintf(row.title,sizeof(row.title),"%s",parts[2]);parts[3][strcspn(parts[3],"\r\n")]=0;
        snprintf(row.artist,sizeof(row.artist),"%s",parts[3]);rows[count++]=row;
    }
    fclose(f);return count;
}
static void music_read_rows(const char *path){music_count=music_load_rows(path,music_rows);music_selected=0;}
static void music_save_selected(void){
    MusicRow row={0};
    if(music_count&&music_selected>=0&&music_selected<music_count)
        row=music_rows[music_selected];
    else if((music_job==2||music_job==3)&&music_track.key[0])row=music_track;
    else{strcpy(music_message,"NO SONG SELECTED / PLAY OR HIGHLIGHT A TRACK");return;}
    int count=music_load_rows("music-cache/saved.tsv",saved),found=-1;
    for(int i=0;i<count;i++)if(!strcmp(saved[i].kind,row.kind)&&!strcmp(saved[i].key,row.key)){found=i;break;}
    if(found<0&&count==100){strcpy(music_message,"SAVED TRACKS FULL / UNSAVE A TRACK FIRST");return;}
    mkdir("music-cache",0700);FILE *f=fopen("music-cache/saved.next","w");
    if(!f){strcpy(music_message,"COULD NOT SAVE TRACK / CHECK STORAGE");return;}
    int failed=0,written=0;
    if(found<0){if(fprintf(f,"%s\t%s\t%s\t%s\n",row.kind,row.key,row.title,row.artist)<0)failed=1;written++;}
    for(int i=0;i<count&&written<100;i++)if(i!=found){
        if(fprintf(f,"%s\t%s\t%s\t%s\n",saved[i].kind,saved[i].key,saved[i].title,saved[i].artist)<0)failed=1;
        written++;
    }
    if(fclose(f))failed=1;
    if(failed||rename("music-cache/saved.next","music-cache/saved.tsv")){
        unlink("music-cache/saved.next");strcpy(music_message,"COULD NOT SAVE TRACK / CHECK STORAGE");return;
    }
    strcpy(music_message,found<0?"SAVED TO LIBRARY / OPEN LIBRARY TAB":"REMOVED FROM SAVED TRACKS");
}
static void music_lookup_stop(void){
    if(music_lookup_pid){kill(-music_lookup_pid,SIGKILL);kill(music_lookup_pid,SIGKILL);waitpid(music_lookup_pid,NULL,0);}
    music_lookup_pid=0;music_lookup_job=0;
}
/* Browsing never takes ownership of the currently playing audio worker. */
static void music_lookup(int library){
    music_lookup_stop();mkdir("music-cache",0700);unlink("music-cache/browse-error.txt");
#ifdef _WIN32
    char runtime[PATH_MAX],python[PATH_MAX],script[PATH_MAX],cert[PATH_MAX];
    snprintf(runtime,sizeof(runtime),"%s/music-runtime",flxtr_win_binary_dir());
    snprintf(python,sizeof(python),"%s/bin/python.exe",runtime);
    snprintf(script,sizeof(script),"%s/tools/music.py",flxtr_win_binary_dir());
    snprintf(cert,sizeof(cert),"%s/certs/cacert.pem",flxtr_win_binary_dir());
    setenv("PYTHONHOME",runtime,1);setenv("PYTHONDONTWRITEBYTECODE","1",1);setenv("SSL_CERT_FILE",cert,1);
    const char *args[]={"python","-I",script,library?"library":music_tab==MUSIC_DISCOVER?(music_discover_current?"discover-current":"discover"):"search",library?NULL:music_tab==MUSIC_DISCOVER&&music_discover_current?(strcmp(music_track.kind,"video")?"current":music_track.key):music_query,NULL};
    music_lookup_pid=flxtr_win_spawn(python,args,"music-cache/browse.tsv","music-browse.log",NULL,0,1);
    unsetenv("PYTHONHOME");
#else
    music_lookup_pid=shell_fork();
    if(!music_lookup_pid){
        setpgid(0,0);setenv("FLXTR_MUSIC_BROWSE","1",1);
        int out=open("music-cache/browse.tsv",O_CREAT|O_WRONLY|O_TRUNC,0600);
        int err=open("music-browse.log",O_CREAT|O_WRONLY|O_TRUNC,0600);
        if(out<0||err<0)_exit(126);
        dup2(out,1);dup2(err,2);close(out);close(err);
        if(library)execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py","library",(char*)NULL);
        else execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py",music_tab==MUSIC_DISCOVER?(music_discover_current?"discover-current":"discover"):"search",music_tab==MUSIC_DISCOVER&&music_discover_current?(strcmp(music_track.kind,"video")?"current":music_track.key):music_query,(char*)NULL);
        _exit(127);
    }
    if(music_lookup_pid>0)setpgid(music_lookup_pid,music_lookup_pid);
#endif
    music_lookup_job=library?5:1;music_lookup_started=SDL_GetTicks();
    if(music_lookup_pid<0){music_lookup_pid=0;music_lookup_job=0;}
    snprintf(music_message,sizeof(music_message),"%s",music_lookup_pid?(library?"FINDING LOCAL AND DOWNLOADED AUDIO...":"FINDING MUSIC... / B CANCEL"):"COULD NOT START MUSIC LOOKUP");
}
static void music_open_tab(int tab){
    music_lookup_stop();music_tab=tab;music_loaded=1;music_count=music_selected=0;
    if(tab==MUSIC_RECENTS){
        music_read_rows("music-cache/recents.tsv");
        strcpy(music_message,"RECENTLY PLAYED / NEWEST FIRST");
    }else if(tab==MUSIC_LIBRARY)music_lookup(1);
    else if(tab==MUSIC_DISCOVER){
        if(music_discover_current){
            if(!music_track.key[0]){
                strcpy(music_message,"PLAY A SONG FIRST / Y SWITCH TO GENRES");return;
            }
        }else{
            int genre=mtv_weighted_genre((unsigned int)time(NULL)+(unsigned int)SDL_GetTicks());
            if(genre==11||genre==12)mtv_fusion_query(genre,music_query,sizeof(music_query));
            else snprintf(music_query,sizeof(music_query),"%s",mtv_genre_queries[genre]);
        }
        music_lookup(0);
    }else{
        music_query[0]=0;music_cursor=0;
        strcpy(music_message,"START SEARCH / ENTER SONG, ARTIST OR PASTE A LINK");
    }
}
static void music_open(void){music_on=1;music_edit=0;music_open_tab(MUSIC_RECENTS);}
static int music_lookup_poll(void){
    if(!music_lookup_pid)return 0;
    int code;pid_t done=waitpid(music_lookup_pid,&code,WNOHANG);
    if(done!=music_lookup_pid){
        if(SDL_GetTicks()-music_lookup_started<120000)return 0;
        music_lookup_stop();strcpy(music_message,"LOOKUP TIMED OUT / Y RETRY");return 1;
    }
    music_lookup_pid=0;music_lookup_job=0;
    if(!WIFEXITED(code)||WEXITSTATUS(code)){
        music_phone_play=0;strcpy(music_message,"LOOKUP FAILED / Y RETRY / DETAILS IN MUSIC-BROWSE.LOG");return 1;
    }
    music_read_rows("music-cache/browse.tsv");
    snprintf(music_message,sizeof(music_message),"%d TRACKS / A PLAY / START SEARCH",music_count);
    if(music_phone_play){music_phone_play=0;if(music_count)music_spawn(2);}
    return 1;
}
static void music_stop(void){
    if(music_pid){kill(-music_pid,SIGKILL);kill(music_pid,SIGKILL);waitpid(music_pid,NULL,0);music_pid=0;}
    music_job=music_paused=0;
}
/* The launcher supervises this worker while the UI releases its video device. */
static int music_rom_handoff(void){
#if defined(_WIN32) || defined(FLXTR_DESKTOP)
    return 0;
#else
    if(music_job!=3||music_pid<1||music_paused||access("tools/rom_music.py",R_OK))return 0;
    FILE *f=fopen("game-music-request.next","w");if(!f)return 0;
    fprintf(f,"%ld\t%d\n",(long)music_pid,music_autoplay);
    for(int i=music_play_index+1;i<music_queue_count;i++)
        fprintf(f,"%s\t%s\n",music_queue[i].kind,music_queue[i].key);
    if(fclose(f)||rename("game-music-request.next","game-music-request"))return 0;
    music_pid=0;music_job=0;return 1;
#endif
}
static void music_spawn(int job){
    if(job==1){music_tab=MUSIC_SEARCH;music_loaded=1;if(!music_query[0]){strcpy(music_message,"TYPE A SONG OR ARTIST FIRST");return;}music_lookup(0);return;}
    music_stop();mkdir("music-cache",0700);unlink("music-cache/error.txt");
    if(job==2||job==4){unlink("music-cache/stream.txt");unlink("music-cache/playing.txt");}
    if(job==2){
        if(!music_advancing){
            music_queue_count=music_count;music_play_index=music_selected;
            memcpy(music_queue,music_rows,(size_t)music_count*sizeof(MusicRow));
        }
        music_track=music_queue[music_play_index];
    }
    music_job=job;music_started=SDL_GetTicks();
#ifdef _WIN32
    if(job==3){
        const char *args[]={"greenlink-audio","music-cache/stream.txt",NULL};
        music_pid=flxtr_win_spawn("greenlink-audio",args,"music.log","music.log",NULL,0,1);
    }else{
        char runtime[PATH_MAX],python[PATH_MAX],script[PATH_MAX],cert[PATH_MAX];
        snprintf(runtime,sizeof(runtime),"%s/music-runtime",flxtr_win_binary_dir());
        snprintf(python,sizeof(python),"%s/bin/python.exe",runtime);
        snprintf(script,sizeof(script),"%s/tools/music.py",flxtr_win_binary_dir());
        snprintf(cert,sizeof(cert),"%s/certs/cacert.pem",flxtr_win_binary_dir());
        setenv("PYTHONHOME",runtime,1);setenv("PYTHONDONTWRITEBYTECODE","1",1);setenv("SSL_CERT_FILE",cert,1);
        const char *args[]={"python","-I",script,job==4?"next":"resolve",job==4?NULL:music_track.kind,job==4?NULL:music_track.key,NULL};
        music_pid=flxtr_win_spawn(python,args,job==1?"music-cache/results.tsv":"music.log","music.log",NULL,0,1);
        unsetenv("PYTHONHOME");
    }
#else
    music_pid=shell_fork();
    if(!music_pid){
        setpgid(0,0);
        int out=open(job==1?"music-cache/results.tsv":"music.log",O_CREAT|O_WRONLY|O_TRUNC,0600);
        int err=open("music.log",O_CREAT|O_WRONLY|O_TRUNC,0600);
        if(out<0||err<0)_exit(126);
        dup2(out,1);dup2(err,2);close(out);close(err);
        if(job==3)execl("./greenlink-audio","greenlink-audio","music-cache/stream.txt",(char*)NULL);
        else if(job==4)execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py","next",(char*)NULL);
        else if(job==1)execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py","search",music_query,(char*)NULL);
        else execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py","resolve",music_track.kind,music_track.key,(char*)NULL);
        _exit(127);
    }
#endif
    if(music_pid<0){music_pid=0;music_job=0;snprintf(music_message,sizeof(music_message),"COULD NOT START MUSIC WORKER");return;}
#ifndef _WIN32
    setpgid(music_pid,music_pid);
#endif
    snprintf(music_message,sizeof(music_message),"%s",job==1?"SEARCHING... B CANCEL":(job==2||job==4)?"FINDING YOUTUBE AUDIO... B CANCEL":"PLAYING / X PAUSE / A PLAY SELECTED / B EXIT");
}
static int music_poll(void){
    int browsing=music_lookup_poll();
    if(!music_pid)return browsing;
    int code;pid_t done=waitpid(music_pid,&code,WNOHANG);
    if(done!=music_pid){
        if(music_job!=3&&SDL_GetTicks()-music_started>120000){music_stop();strcpy(music_message,"LOOKUP TIMED OUT / START TO TRY AGAIN");return 1;}
        return browsing;
    }
    FILE *bglog=fopen("music-background.log","a");
    if(bglog){fprintf(bglog,"WORKER EXIT job=%d status=%d\n",music_job,WIFEXITED(code)?WEXITSTATUS(code):-1);fclose(bglog);}
    music_pid=0;int job=music_job;music_job=0;music_paused=0;
    if(!WIFEXITED(code)||WEXITSTATUS(code)){
        music_phone_play=0;
        strcpy(music_message,"MUSIC UNAVAILABLE / START RETRY / DETAILS IN MUSIC.LOG");
        FILE *f=fopen("music-cache/error.txt","r");if(f){if(!fgets(music_message,sizeof(music_message),f))music_message[0]=0;fclose(f);}
        return 1;
    }
    if(job==2||job==4){
        FILE *f=fopen("music-cache/playing.txt","r");music_playing[0]=0;
        if(f){if(!fgets(music_playing,sizeof(music_playing),f))music_playing[0]=0;fclose(f);music_playing[strcspn(music_playing,"\r\n")]=0;}
        music_spawn(3);
    }else if(job==3&&music_autoplay){
        if(music_play_index+1<music_queue_count){music_play_index++;music_advancing=1;music_spawn(2);music_advancing=0;}
        else if(strcmp(music_track.kind,"local"))music_spawn(4);
        else strcpy(music_message,"LOCAL QUEUE FINISHED / A PLAY");
    }else if(job==3){
        snprintf(music_message,sizeof(music_message),"TRACK FINISHED / A PLAY / START SEARCH");
    }
    return 1;
}
/* Video owns the audio device. Resume only if music was playing before it. */
static int music_paused_for_video;
static void music_video_pause(int resume){
    if(resume){
        if(music_paused_for_video&&music_job==3&&music_pid>0){
            kill(music_pid,SIGUSR1);music_paused=0;
        }
        music_paused_for_video=0;
    }else{
        if(music_paused_for_video&&music_job==3&&music_pid>0&&music_paused)return;
        music_paused_for_video=(music_job==3&&music_pid>0&&!music_paused);
        if(music_paused_for_video){kill(music_pid,SIGUSR1);music_paused=1;}
    }
}
static void music_backspace(void){
    flxtr_cursor_backspace(music_query,&music_cursor);
}
static void music_text(const char *s){
    while(*s){
        if((unsigned char)*s>=32){
            char value[2]={*s,0};
            if(!flxtr_cursor_insert(music_query,sizeof(music_query),&music_cursor,value))return;
        }
        s++;
    }
}
static const char *music_completion(void);
static void music_action(SDL_Keycode key){
    if(music_edit){
        size_t n=strlen(music_query);
        if(key==SDLK_ESCAPE){music_edit=0;SDL_StopTextInput();return;}
        if(key==SDLK_BACKSPACE){if(music_cursor)music_backspace();else if(!n){music_edit=0;SDL_StopTextInput();}}
        else if(key==SDLK_v&&(SDL_GetModState()&KMOD_CTRL)){char *p=SDL_GetClipboardText();if(p){music_text(p);SDL_free(p);}}
        else if(key==SDLK_TAB){
            const char *completion=music_completion();
            if(completion){snprintf(music_query,sizeof(music_query),"%s",completion);music_cursor=strlen(music_query);}
        }
        else if(key==SDLK_LEFT&&music_key>0)music_key--;
        else if(key==SDLK_RIGHT&&music_key<39)music_key++;
        else if(key==SDLK_PAGEUP)flxtr_cursor_left(music_query,&music_cursor);
        else if(key==SDLK_PAGEDOWN)flxtr_cursor_right(music_query,&music_cursor);
        else if(key==SDLK_UP&&music_key>=10)music_key-=10;
        else if(key==SDLK_DOWN&&music_key<30)music_key+=10;
        else if(key==SDLK_F4){char c=search_keys[music_key];if(c=='<')flxtr_cursor_left(music_query,&music_cursor);else if(c=='>')flxtr_cursor_right(music_query,&music_cursor);else{char value[2]={c,0};music_text(value);}}
        else if(key==SDLK_INSERT){music_text(" ");}
        else if(key==SDLK_RETURN||key==SDLK_F3){music_edit=0;SDL_StopTextInput();music_spawn(1);}
        return;
    }
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){
        if(music_lookup_pid){music_lookup_stop();strcpy(music_message,"LOOKUP CANCELLED / Y RETRY");return;}
        /* Exit browser without touching the audio worker; retain diagnostics. */
        FILE *log=fopen("music-background.log","a");
        if(log){fprintf(log,"EXIT job=%d pid=%ld paused=%d\n",music_job,(long)music_pid,music_paused);fclose(log);}
        music_on=0;return;
    }
    if(key==SDLK_F3||key==SDLK_SLASH){if(music_tab!=MUSIC_SEARCH)music_query[0]=0;music_tab=MUSIC_SEARCH;music_edit=1;music_key=0;music_cursor=strlen(music_query);SDL_StartTextInput();return;}
    if(key==SDLK_F5){music_lookup_stop();music_on=0;mtv_weights_open();return;}
    if(key==SDLK_F7||key==SDLK_f){music_save_selected();if(music_tab==MUSIC_LIBRARY){char saved_message[sizeof(music_message)];snprintf(saved_message,sizeof(saved_message),"%s",music_message);music_lookup(1);snprintf(music_message,sizeof(music_message),"%s",saved_message);}return;}
    if(key==SDLK_HOME||key==SDLK_END){music_open_tab((music_tab+(key==SDLK_HOME?3:1))%4);return;}
    if(key==SDLK_y){
        if(music_tab==MUSIC_DISCOVER){music_discover_current=!music_discover_current;music_open_tab(MUSIC_DISCOVER);}
        else music_open_tab(music_tab);
        return;
    }
    if(music_job==1||music_job==2||music_job==4)return;
    if(key==SDLK_UP&&music_selected>0)music_selected--;
    if(key==SDLK_DOWN&&music_selected+1<music_count)music_selected++;
    if(key==SDLK_RETURN&&music_count)music_spawn(2);
    if(key==SDLK_F2&&music_job==3){kill(music_pid,SIGUSR1);music_paused=!music_paused;snprintf(music_message,sizeof(music_message),"%s / X TOGGLE / B EXIT",music_paused?"PAUSED":"PLAYING");}
}
/* Scrolling title is rendered with bounded text; no texture allocation. */
static void music_now_playing(int x,int y,int columns,SDL_Color color){
    if(music_job!=3&&music_job!=2&&music_paused==0)return;
    const char *title=music_playing[0]?music_playing:music_track.title;
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
static const char *music_completion(void){
    size_t n=strlen(music_query);
    if(n<3||strstr(music_query,"://"))return NULL;
    for(int i=music_count-1;i>=0;i--)
        if(!strncasecmp(music_rows[i].title,music_query,n)&&strlen(music_rows[i].title)>n)
            return music_rows[i].title;
    for(size_t i=search_dict_count;i>0;i--)
        if(!strncasecmp(search_dict[i-1],music_query,n)&&strlen(search_dict[i-1])>n)
            return search_dict[i-1];
    return NULL;
}
static void music_draw(void){
    if(!music_loaded)music_open_tab(MUSIC_RECENTS);
    SDL_Color accent=green,muted=dim;
    SDL_SetRenderDrawColor(renderer,theme_canvas.r,theme_canvas.g,theme_canvas.b,255);SDL_RenderClear(renderer);
    if((background&&!monochrome_menu)||menu_theme>=3){
        if(background)SDL_RenderCopy(renderer,background,NULL,NULL);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        rect(20,16,920,53,theme_pane(215),1);
        rect(20,78,920,404,theme_pane(225),1);
        rect(20,484,920,42,theme_pane(235),1);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
    }
    text(28,24,"FLXTR / MUSIC",3,accent,45);
    if(music_edit){
        text(30,85,"SEARCH OR PASTE PUBLIC SPOTIFY PLAYLIST URL",2,accent,70);
        const char *completion=music_completion();
        if(completion){text(30,105,completion,1,accent,110);text(800,105,"SQUARE ACCEPT",1,accent,18);}
        text(30,125,music_query,1,white,145);
        text(30+(int)(music_cursor>137?137:music_cursor)*6,143,"^",1,accent,1);
        for(int i=0;i<40;i++){
            int x=70+(i%10)*80,y=185+(i/10)*53;char c[2]={search_keys[i],0};
            rect(x-5,y-5,46,36,i==music_key?accent:muted,0);text(x+8,y,c,3,accent,1);
        }
        text(30,425,"A TYPE / TRIANGLE SPACE / SQUARE PREDICT / L1 R1 CURSOR",1,muted,110);
        text(30,460,"L1/R1 MOVE CURSOR / CTRL+V PASTES PLAYLIST LINK",1,muted,90);
    }else{
        static const char *const tabs[]={"RECENTS","DISCOVER","LIBRARY","SEARCH"};
        for(int tab=0;tab<4;tab++){
            int x=30+tab*230;
            if(tab==music_tab)rect(x-6,73,216,31,accent,0);
            text(x,80,tabs[tab],2,tab==music_tab?accent:muted,18);
        }
        const char *subtitle=music_tab==MUSIC_RECENTS?"PICK UP WHERE YOU LEFT OFF / NEWEST FIRST":
            music_tab==MUSIC_DISCOVER?(music_discover_current?"CURRENT SONG RADIO / Y SWITCH TO GENRE DISCOVERY":"WEIGHTED GENRE DISCOVERY / Y SWITCH TO CURRENT SONG"):
            music_tab==MUSIC_LIBRARY?"SAVED TRACK LINKS + LOCAL AUDIO / L3 SAVE OR UNSAVE":"FIND A SONG, ARTIST OR PLAYLIST / START TO TYPE";
        text(30,116,subtitle,1,muted,145);
        int first=music_selected/6*6;
        for(int i=first;i<music_count&&i<first+6;i++){
            int y=150+(i-first)*40;
            if(i==music_selected)rect(24,y-5,912,38,accent,0);
            if(!strcmp(music_rows[i].kind,music_track.kind)&&!strcmp(music_rows[i].key,music_track.key)&&(music_job==3||music_job==2))text(887,y,"[>]",2,accent,4);
            text(38,y,music_rows[i].title,2,accent,73);text(38,y+22,music_rows[i].artist,1,muted,130);
        }
        if(!music_count&&!music_lookup_pid){
            text(38,180,music_tab==MUSIC_RECENTS?"YOUR RECENT MUSIC WILL APPEAR HERE":music_tab==MUSIC_LIBRARY?"SAVE TRACK LINKS WITH L3 / ADD LOCAL MUSIC FILES":"FIND YOUR NEXT FAVORITE",2,accent,73);
            text(38,220,"L1/R1 CHANGE TABS / START TO SEARCH",1,muted,110);
        }
        char counter[80];snprintf(counter,sizeof(counter),"%d / %d",music_count?music_selected+1:0,music_count);
        text(820,397,counter,1,muted,15);
        if(*music_playing){text(30,421,music_paused?"PAUSED":"NOW PLAYING",1,muted,20);music_now_playing(30,440,140,accent);}
        text(30,466,music_message,1,accent,145);
        text(30,503,ui_hint("L1/R1 TABS / A PLAY / X PAUSE / L3 SAVE / Y DISCOVERY MODE / START SEARCH / B BACK",
            "HOME/END TABS / ENTER PLAY / F SAVE / Y DISCOVERY MODE / F3 SEARCH / ESC BACK"),1,muted,145);
    }
}
#endif
