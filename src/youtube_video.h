/* Short-lived YouTube video resolver; native player retains transport/control. */
#ifndef FLXTR_YOUTUBE_VIDEO_H
#define FLXTR_YOUTUBE_VIDEO_H
static pid_t youtube_pid;
static Uint32 youtube_started;
static int youtube_for_tv,mtv_override,youtube_height,youtube_preparing;
static Uint32 youtube_retry_at;
static double youtube_resume;
static unsigned int youtube_recovery_attempts;
static int youtube_recovery_pending;
static char youtube_audio[16004],youtube_agent[502],youtube_next_audio[16004],youtube_next_agent[502];
static void youtube_stop(void){
    if(youtube_pid){kill(-youtube_pid,SIGKILL);kill(youtube_pid,SIGKILL);waitpid(youtube_pid,NULL,0);}
    youtube_pid=0;
}
static int youtube_spawn(const char *query,int television){
    if(youtube_pid||(!youtube_preparing&&player_pid)||catalog_pid||update_pid)return -1;
    mkdir("music-cache",0700);unlink("music-cache/video.txt");unlink("music-cache/error.txt");
    if(!television&&!youtube_recovery_pending)youtube_recovery_attempts=0;
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
#include "mtv_weights.h"

/* VGA and Karaoke use one weighted genre-fusion search builder.
 * A custom anchor (mtv-vga-subject.txt) and optional style
 * (mtv-fusion-style.txt) can be edited without recompiling.
 * Legacy MTV genre weights remain the source of style probabilities.
 */
static void mtv_fusion_file(const char *path,char *out,size_t size){
    out[0]=0;
    FILE *f=fopen(path,"r");
    if(!f)return;
    if(fgets(out,(int)size,f)){
        out[strcspn(out,"\r\n")]=0;
        for(size_t i=0;out[i];i++)
            if((unsigned char)out[i]<32||out[i]=='"'||out[i]=='\\')out[i]=' ';
    }else out[0]=0;
    fclose(f);
}
static void mtv_fusion_query(int anchor,char *out,size_t size){
    static const char *const subjects[]={
        "Persona 5 Royal","Final Fantasy","NieR Automata",
        "Chrono Trigger","Sonic Adventure","Cowboy Bebop",
        "Studio Ghibli","Kingdom Hearts"
    };
    static const char *const styles[MTV_GENRES]={
        "city pop","pop","rock","hip hop","electronic","metal",
        "jazz","country","R&B soul","k-pop","anime music",
        "video game music","karaoke","enka","city pop","latin"
    };
    int weights[MTV_GENRES];mtv_weights_load(weights);
    unsigned int total=0;
    /* Genre weights remain relative and styles must never recursively
     * choose VGA or Karaoke as the second genre. */
    for(int i=1;i<MTV_GENRES;i++)if(i!=11&&i!=12)total+=(unsigned int)weights[i];
    unsigned int roll=total?(unsigned int)tv_sequence("mtv-fusion-sequence.txt")%total:0;
    int style=0;
    for(int i=1;total&&i<MTV_GENRES;i++){
        if(i==11||i==12)continue;
        if(roll<(unsigned int)weights[i]){style=i;break;}
        roll-=(unsigned int)weights[i];
    }
    if(mtv_fusion_genre>0&&mtv_fusion_genre<MTV_GENRES&&mtv_fusion_valid(mtv_fusion_genre))style=mtv_fusion_genre;
    char custom_style[100]={0},subject[100];
    if(mtv_fusion_genre==MTV_FUSION_CUSTOM)mtv_fusion_file("mtv-fusion-style.txt",custom_style,sizeof(custom_style));
    mtv_fusion_file("mtv-vga-subject.txt",subject,sizeof(subject));
    const char *genre=custom_style[0]?custom_style:styles[style];
    if(anchor==11){
        if(!subject[0]){
            unsigned int n=(unsigned int)tv_sequence("mtv-vga-sequence.txt");
            snprintf(subject,sizeof(subject),"%s",subjects[n%(sizeof(subjects)/sizeof(subjects[0]))]);
        }
        snprintf(out,size,"%s %s remix music video",subject,genre);
    }else{
        /* Keep Karaoke search instrumental by default, rather than
         * retrieving regular vocal performances. */
        /* Fixed precision caps avoid -Werror=format-truncation. */
        snprintf(out,size,"%.70s%s%.70s karaoke instrumental backing track",
                 subject,subject[0]?" ":"",genre);
    }
}
static void mtv_query(char *out,size_t size){
    /* Exclusive selection and weighted mix share one fusion builder. */
    if(mtv_genre>0&&mtv_genre<MTV_GENRES){
        if(mtv_genre==11||mtv_genre==12)mtv_fusion_query(mtv_genre,out,size);
        else snprintf(out,size,"%s",mtv_genre_queries[mtv_genre]);
        return;
    }
    /* Keep user-authored MTV queries as the highest-priority custom mode. */
    FILE *f=fopen("mtv-query.txt","r");
    if(f){
        char line[221];
        if(fgets(line,sizeof(line),f)){
            line[strcspn(line,"\r\n")]=0;
            if(line[0]){snprintf(out,size,"%s",line);fclose(f);return;}
        }
        fclose(f);
    }
    int chosen=mtv_weighted_genre((unsigned int)tv_sequence("mtv-genre-sequence.txt"));
    if(chosen==11||chosen==12)mtv_fusion_query(chosen,out,size);
    else snprintf(out,size,"%s",mtv_genre_queries[chosen]);
}
static void youtube_prefetch(void){
    if(!tv_active||!player_pid||strcmp(playing_title.kind,"youtube"))return;
    if(tv_prepared&&tv_buffer_pid){
        int code;pid_t done=waitpid(tv_buffer_pid,&code,WNOHANG);
        if(done==tv_buffer_pid){tv_buffer_pid=0;tv_cancel_prepared();youtube_retry_at=SDL_GetTicks()+15000;}
        static Uint32 next_check;
        Uint32 now=SDL_GetTicks();
        if(tv_prepared&&tv_gate>=0&&(Sint32)(now-next_check)>=0){
            next_check=now+1000;
            double position=0,duration=0;
            FILE *f=fopen("playback-position","r");
            if(f){int fields=fscanf(f,"%lf %lf",&position,&duration);fclose(f);
                if(fields==2&&position>=0&&duration>0&&duration-position<=30){
                    char expand='B';ssize_t n=player_pipe_write(tv_gate,&expand,1);(void)n;
                }
            }
        }
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
