/* Short-lived YouTube video resolver; native player retains transport/control. */
#ifndef FLXTR_YOUTUBE_VIDEO_H
#define FLXTR_YOUTUBE_VIDEO_H
static pid_t youtube_pid;
static Uint32 youtube_started;
static int youtube_for_tv,mtv_override,youtube_height,youtube_preparing;
static Uint32 youtube_retry_at;
static double youtube_resume;
static char youtube_audio[16004],youtube_agent[502],youtube_next_audio[16004],youtube_next_agent[502];
static void youtube_stop(void){
    if(youtube_pid){kill(-youtube_pid,SIGKILL);kill(youtube_pid,SIGKILL);waitpid(youtube_pid,NULL,0);}
    youtube_pid=0;
}
static int youtube_spawn(const char *query,int television){
    if(youtube_pid||(!youtube_preparing&&player_pid)||catalog_pid||update_pid)return -1;
    mkdir("music-cache",0700);unlink("music-cache/video.txt");unlink("music-cache/error.txt");
    youtube_for_tv=television;youtube_started=SDL_GetTicks();youtube_resume=0;
    char height[12];youtube_height=quality_height();snprintf(height,sizeof(height),"%d",youtube_height);
    const char *mode=television?"tv-video":"video";
#ifdef _WIN32
    char runtime[PATH_MAX],python[PATH_MAX],script[PATH_MAX],cert[PATH_MAX];
    snprintf(runtime,sizeof(runtime),"%s/music-runtime",flxtr_win_binary_dir());
    snprintf(python,sizeof(python),"%s/bin/python.exe",runtime);
    snprintf(script,sizeof(script),"%s/tools/music.py",flxtr_win_binary_dir());
    snprintf(cert,sizeof(cert),"%s/certs/cacert.pem",flxtr_win_binary_dir());
    setenv("PYTHONHOME",runtime,1);setenv("SSL_CERT_FILE",cert,1);
    const char *args[]={"python","-I",script,mode,query,height,NULL};
    youtube_pid=flxtr_win_spawn(python,args,"youtube.log","youtube.log",NULL,0,1);unsetenv("PYTHONHOME");
#else
    youtube_pid=shell_fork();
    if(!youtube_pid){
        setpgid(0,0);
        int log=open("youtube.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log<0)_exit(126);
        dup2(log,1);dup2(log,2);close(log);
        execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py",mode,query,height,(char*)NULL);_exit(127);
    }
    if(youtube_pid>0)setpgid(youtube_pid,youtube_pid);
#endif
    if(youtube_pid<=0){youtube_pid=0;return -1;}
    if(!youtube_preparing)snprintf(status,sizeof(status),television?"MTV: FINDING NEXT MUSIC VIDEO / ESC CANCEL":"RESOLVING YOUTUBE VIDEO / ESC CANCEL");
    return 0;
}
static int youtube_line(FILE *f,char *value,size_t size){
    if(!fgets(value,(int)size,f)||!strchr(value,'\n'))return -1;
    value[strcspn(value,"\r\n")]=0;return 0;
}
static int youtube_poll(void){
    if(!youtube_pid)return 0;
    int code;pid_t done=waitpid(youtube_pid,&code,WNOHANG);
    if(done!=youtube_pid){
        if(SDL_GetTicks()-youtube_started<=120000)return 0;
        youtube_stop();
        if(youtube_preparing){youtube_preparing=0;youtube_retry_at=SDL_GetTicks()+15000;return 0;}
        if(adaptive_lookup_fallback())return 1;
        tv_active=0;music_video_pause(1);snprintf(status,sizeof(status),"YOUTUBE LOOKUP TIMED OUT / A RETRY");return 1;
    }
    youtube_pid=0;
    int preparing=youtube_preparing;youtube_preparing=0;
    if(!WIFEXITED(code)||WEXITSTATUS(code)){
        if(preparing){youtube_retry_at=SDL_GetTicks()+15000;return 0;}
        if(adaptive_lookup_fallback())return 1;
        if(youtube_for_tv){tv_next_at=SDL_GetTicks()+2500;snprintf(status,sizeof(status),"MTV: SEARCH FAILED / TRYING ANOTHER");return 1;}
        snprintf(status,sizeof(status),"YOUTUBE UNAVAILABLE / SEE YOUTUBE.LOG");
        return 1;
    }
    char previous_audio[16004],previous_agent[502];
    if(preparing||adaptive_pending){strcpy(previous_audio,youtube_audio);strcpy(previous_agent,youtube_agent);}
    Title title={0};title.height=youtube_height;char name[152];strcpy(title.kind,"youtube");
    FILE *f=fopen("music-cache/video.txt","r");
    int good=f&&!youtube_line(f,name,sizeof(name))&&!youtube_line(f,title.url,sizeof(title.url))&&
        !youtube_line(f,youtube_audio,sizeof(youtube_audio))&&!youtube_line(f,youtube_agent,sizeof(youtube_agent));
    if(f){
        /* Optional origin URL lets a quality change resolve the SAME clip. */
        if(youtube_line(f,title.meta,sizeof(title.meta)))title.meta[0]=0;
        else if(title.meta[0]){
            const char *prefix="https://www.youtube.com/watch?v=";
            size_t n=strlen(prefix);
            if(strncmp(title.meta,prefix,n)||strlen(title.meta+n)!=11||
               strspn(title.meta+n,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=11)title.meta[0]=0;
        }
        fclose(f);
    }
    if(!good||strncmp(title.url,"https://",8)||(youtube_audio[0]&&strncmp(youtube_audio,"https://",8))){
        if(preparing){strcpy(youtube_audio,previous_audio);strcpy(youtube_agent,previous_agent);youtube_retry_at=SDL_GetTicks()+15000;return 0;}
        if(adaptive_pending){strcpy(youtube_audio,previous_audio);strcpy(youtube_agent,previous_agent);if(adaptive_lookup_fallback())return 1;}
        if(youtube_for_tv){tv_active=0;music_video_pause(1);}
        snprintf(status,sizeof(status),"INVALID YOUTUBE VIDEO RESULT");return 1;
    }
    snprintf(title.title,sizeof(title.title),"%.79s",name);
    if(preparing){
        if(!tv_active){return 0;}
        /* Keep the current player and its control pipe while a second process
         * demuxes compressed packets behind a gate. It never opens SLVideo. */
        pid_t current=player_pid;PlayerPipe control=player_control;Title playing=playing_title;
        double resume=restart_position;int active=auto_active;
        tv_preloading=1;restart_position=0;auto_active=0;
        launch_player(&title);tv_preloading=0;
        strcpy(youtube_next_audio,youtube_audio);strcpy(youtube_next_agent,youtube_agent);
        strcpy(youtube_audio,previous_audio);strcpy(youtube_agent,previous_agent);
        tv_buffer_pid=player_pid;tv_buffer_control=player_control;tv_next_title=title;
        tv_prepared=tv_buffer_pid>0;
        player_pid=current;player_control=control;playing_title=playing;
        restart_position=resume;auto_active=active;
        if(!tv_prepared)youtube_retry_at=SDL_GetTicks()+15000;
        if(tv_prepared&&!player_pid)tv_begin_prepared();
        fprintf(stderr,"MTV next video %s: %s\n",tv_prepared?"preloading":"unavailable",title.title);
        return 0;
    }
    auto_active=0;restart_position=youtube_resume;youtube_resume=0;launch_player(&title);
    if(!player_pid&&youtube_for_tv){tv_next_at=SDL_GetTicks()+2500;}
    return 1;
}
static void mtv_query(char *out,size_t size){
    snprintf(out,size,"official music videos 80s 90s pop rock");
    FILE *f=fopen("mtv-query.txt","r");
    if(f){char line[221];if(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;if(line[0])snprintf(out,size,"%s",line);}fclose(f);}
}
static void youtube_prefetch(void){
    if(!tv_active||!player_pid||strcmp(playing_title.kind,"youtube"))return;
    if(tv_prepared&&tv_buffer_pid){
        int code;pid_t done=waitpid(tv_buffer_pid,&code,WNOHANG);
        if(done==tv_buffer_pid){tv_buffer_pid=0;tv_cancel_prepared();youtube_retry_at=SDL_GetTicks()+15000;}
    }
    if(tv_prepared||youtube_pid||(Sint32)(SDL_GetTicks()-youtube_retry_at)<0)return;
    char query[221];mtv_query(query,sizeof(query));youtube_preparing=1;
    if(youtube_spawn(query,1)){youtube_preparing=0;youtube_retry_at=SDL_GetTicks()+15000;}
}
static int youtube_link(const char *url){
    return !strncmp(url,"https://www.youtube.com/",24)||!strncmp(url,"https://youtube.com/",20)||
        !strncmp(url,"https://music.youtube.com/",26)||!strncmp(url,"https://youtu.be/",17);
}
#endif
