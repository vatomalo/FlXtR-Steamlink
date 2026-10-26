/* Nonblocking muted game/media inset: yt-dlp resolve, separate H264 frame worker,
 * and an SDL texture. The fullscreen hardware player is never touched. */
#ifndef FLXTR_PREVIEW_INSET_H
#define FLXTR_PREVIEW_INSET_H
#ifndef _WIN32
static pid_t preview_lookup_pid,preview_decode_pid;
static Uint32 preview_selected_at,preview_frame_at;
static SDL_Texture *preview_texture;
static char preview_key[192];
static int preview_state; /* 0 debounce, 1 resolving, 2 decoding, 3 unavailable */
static time_t preview_frame_mtime;
static long preview_frame_nsec;
static void preview_kill(pid_t *child){
    if(*child>0){
        kill(-*child,SIGKILL);kill(*child,SIGKILL);
        waitpid(*child,NULL,0);
        *child=0;
    }
}
static void preview_stop(void){
    preview_kill(&preview_lookup_pid);preview_kill(&preview_decode_pid);
    if(preview_texture){SDL_DestroyTexture(preview_texture);preview_texture=NULL;}
    preview_key[0]=0;preview_state=0;preview_frame_mtime=0;preview_frame_nsec=0;
    unlink("music-cache/preview.txt");unlink("music-cache/preview-frame.bmp");
}
static const char *preview_category(const Title *title){
    if(!strcmp(title->kind,"game")||!strcmp(title->kind,"game-zip"))return "game";
    if(!strcmp(title->kind,"movie"))return "movie";
    if(!strcmp(title->kind,"tv"))return "tv";
    if(!strcmp(title->kind,"kiss"))return "anime";
    return NULL;
}
static int preview_spawn_lookup(const Title *title,const char *kind){
    unlink("music-cache/preview.txt");
    pid_t pid=shell_fork();
    if(pid==0){
        setpgid(0,0);
        int log=open("preview.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log>=0){dup2(log,1);dup2(log,2);close(log);}
        execl("/bin/sh","sh","scripts/music-runtime.sh","tools/music.py",
              "preview",kind,title->title,(char*)NULL);
        _exit(127);
    }
    if(pid<=0)return -1;
    setpgid(pid,pid);preview_lookup_pid=pid;preview_state=1;return 0;
}
static int preview_launch_decoder(void){
    FILE *file=fopen("music-cache/preview.txt","r");if(!file)return -1;
    char title[160],url[2048],agent[512],origin[160];
    int valid=fgets(title,sizeof(title),file)&&fgets(url,sizeof(url),file)&&
        fgets(agent,sizeof(agent),file)&&fgets(origin,sizeof(origin),file);
    fclose(file);if(!valid)return -1;
    url[strcspn(url,"\r\n")]=0;
    agent[strcspn(agent,"\r\n")]=0;
    if(strncmp(url,"https://",8)||strlen(url)<16)return -1;
    unlink("music-cache/preview-frame.bmp");
    pid_t pid=shell_fork();
    if(pid==0){
        setpgid(0,0);
        int log=open("preview-decoder.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log>=0){dup2(log,1);dup2(log,2);close(log);}
        setenv("FLXTR_VIDEO_USER_AGENT",agent,1);
        execl("./greenlink-preview","greenlink-preview",url,(char*)NULL);
        _exit(127);
    }
    if(pid<=0)return -1;
    setpgid(pid,pid);preview_decode_pid=pid;preview_state=2;return 0;
}
static int preview_tick(void){
    const Title *selected=NULL;
    if(renderer&&!coverflow&&!catalog_pid&&!player_pid&&!music_on&&!settings_on&&!rom_assets_on&&!rom_options_menu&&!rom_core_menu&&
       !search_on&&!about&&browse.mode!=11&&total>0&&selection>=0&&selection<total)
        selected=&titles[visible[selection]];
    const char *kind=selected?preview_category(selected):NULL;
    if(!kind){
        if(preview_key[0]){preview_stop();return 1;}
        return 0;
    }
    char key[192];snprintf(key,sizeof(key),"%d:%d:%.120s",browse.mode,selected->id,selected->title);
    if(strcmp(key,preview_key)){
        preview_stop();snprintf(preview_key,sizeof(preview_key),"%s",key);
        preview_selected_at=SDL_GetTicks();return 1;
    }
    Uint32 now=SDL_GetTicks();
    if(preview_state==0&&now-preview_selected_at>=1100){
        if(preview_spawn_lookup(selected,kind))preview_state=3;
        return 1;
    }
    if(preview_state==1){
        int status=0;
        pid_t result=waitpid(preview_lookup_pid,&status,WNOHANG);
        if(result==preview_lookup_pid){
            preview_lookup_pid=0;
            if(!WIFEXITED(status)||WEXITSTATUS(status)||preview_launch_decoder())preview_state=3;
            return 1;
        }
        if(now-preview_selected_at>75000){preview_kill(&preview_lookup_pid);preview_state=3;return 1;}
    }
    if(preview_state==2){
        if(preview_decode_pid){
            int status=0;pid_t result=waitpid(preview_decode_pid,&status,WNOHANG);
            if(result==preview_decode_pid){preview_decode_pid=0;if(!preview_texture)preview_state=3;}
        }
        if(now-preview_frame_at>=30){
            preview_frame_at=now;
            struct stat st;
            if(!stat("music-cache/preview-frame.bmp",&st)&&st.st_size>54&&
               (st.st_mtime!=preview_frame_mtime||st.st_mtim.tv_nsec!=preview_frame_nsec)){
                SDL_Surface *surface=SDL_LoadBMP("music-cache/preview-frame.bmp");
                if(surface){
                    SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);
                    SDL_FreeSurface(surface);
                    if(texture){
                        if(preview_texture)SDL_DestroyTexture(preview_texture);
                        preview_texture=texture;preview_frame_mtime=st.st_mtime;
                        preview_frame_nsec=st.st_mtim.tv_nsec;return 1;
                    }
                }
            }
        }
    }
    return 0;
}
static void preview_draw(void){
    SDL_Rect dest={691,124,232,114};
    if(preview_texture){
        int w,h;SDL_QueryTexture(preview_texture,NULL,NULL,&w,&h);
        if(w>0&&h>0){
            double scale=232.0/w;if(h*scale>114)scale=114.0/h;
            dest.w=(int)(w*scale);dest.h=(int)(h*scale);
            dest.x+=(232-dest.w)/2;dest.y+=(114-dest.h)/2;
        }
        SDL_RenderCopy(renderer,preview_texture,NULL,&dest);return;
    }
    text(749,161,"TRAILER",2,green,18);
    text(715,191,preview_state==3?"NO VIDEO AVAILABLE":"FINDING PREVIEW",1,dim,28);
}
#else
static void preview_stop(void){}
static int preview_tick(void){return 0;}
static void preview_draw(void){text(728,174,"PREVIEW ON STEAM LINK",1,dim,31);}
#endif
#endif
