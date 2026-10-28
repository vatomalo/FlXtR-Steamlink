#include "favorite_art.h"
static pid_t favorite_art_pid;
static Uint32 favorite_art_retry,favorite_art_refresh;
static int favorite_art_supported(const Title *title){
    return !strcmp(title->kind,"movie")||!strcmp(title->kind,"tv")||!strcmp(title->kind,"episode")||
        !strcmp(title->kind,"season")||!strcmp(title->kind,"kiss")||!strcmp(title->kind,"kiss-episode")||
        !strcmp(title->kind,"archive")||!strcmp(title->kind,"archive-file");
}
static void favorites_prepare_art(void){
    for(int i=0;i<count;i++){
        Title *title=&titles[i];if(!favorite_art_supported(title))continue;
        char path[96];favorite_art_path(path,sizeof(path),title->kind,title->title,title->url,title->id,title->season,title->episode);
        if(favorite_art_valid(path)||!favorite_art_valid(title->poster)||!strncmp(title->poster,"catalog-cache/",14))
            snprintf(title->poster,sizeof(title->poster),"%s",path);
    }
}
static int favorites_art_tick(void){
    Uint32 now=SDL_GetTicks();int changed=0;
    if(favorite_art_pid){
        int code;
        if(waitpid(favorite_art_pid,&code,WNOHANG)==favorite_art_pid){
            favorite_art_pid=0;
            favorite_art_retry=now+(WIFEXITED(code)&&!WEXITSTATUS(code)?5000:300000);
            changed=1;
        }else if((Sint32)(now-favorite_art_refresh)>=0){favorite_art_refresh=now+1500;changed=1;}
        if(changed&&browse.mode==FAVORITES_MODE){favorites_prepare_art();cached_page=-1;return 1;}
        return 0;
    }
    if(browse.mode!=FAVORITES_MODE||player_pid||catalog_pid||update_pid||settings_on||search_on||
       (Sint32)(now-favorite_art_retry)<0)return 0;
    favorite_art_retry=now+5000;favorites_prepare_art();
    int missing=0;for(int i=0;i<count;i++)if(favorite_art_supported(&titles[i])&&!favorite_art_valid(titles[i].poster))missing=1;
    if(!missing||access("./greenlink-catalog",X_OK))return 0;
    const char *args[]={"greenlink-catalog","--repair-favorites-art",NULL};
    favorite_art_pid=shell_spawn("./greenlink-catalog",args,"/dev/null","favorites-art.log");
    if(favorite_art_pid<0)favorite_art_pid=0;
    favorite_art_retry=now+300000;favorite_art_refresh=now+1500;
    return 0;
}
