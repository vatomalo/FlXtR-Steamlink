/* Offline files share the player's existing, validated local-media directory. */
#include "phone_media.h"
#define OFFLINE_MODE 37
static pid_t video_download_pid;
static Title video_download_title;
static char video_download_key[17],video_download_message[128];
static int video_download_delete_confirm;
static Title video_download_delete_title;
static int video_download_requested;
static int video_download_supported(const Title *title){
    return title&&(!strncmp(title->url,"https://",8)||!strncmp(title->url,"http://",7))&&
        (!strcmp(title->kind,"source")||!strcmp(title->kind,"movie")||!strcmp(title->kind,"episode")||
         !strcmp(title->kind,"kiss-episode")||!strcmp(title->kind,"archive-file")||!title->kind[0]);
}
static int video_download_offer(const Title *title){
    return video_download_supported(title)||(!title->url[0]&&
        (!strcmp(title->kind,"movie")||!strcmp(title->kind,"episode")||!strcmp(title->kind,"kiss-episode")));
}
static void video_download_list(void){
    count=0;memset(titles,0,sizeof(titles));
    DIR *dir=opendir("video-downloads");struct dirent *entry;
    if(dir){while(count<MAX_TITLES&&(entry=readdir(dir))){
        if(strlen(entry->d_name)!=20||strcmp(entry->d_name+16,".tsv"))continue;
        int valid=1;for(int i=0;i<16;i++)if(!strchr("0123456789abcdef",entry->d_name[i]))valid=0;
        if(!valid)continue;
        char path[80];snprintf(path,sizeof(path),"video-downloads/%.20s",entry->d_name);
        read_catalog(path);
    }closedir(dir);}
    filter();listing_total=total;listing_pages=1;
    snprintf(status,sizeof(status),total?"A PLAY OFFLINE / Y DELETE / X LIBRARY":"NO SAVED VIDEOS / Y DOWNLOAD ON A PLAYABLE SOURCE");
}
static void video_download_finish(void){
    if(!video_download_pid)return;
    FILE *progress=fopen("video-downloads/progress","r");
    if(progress){long long bytes=0;double seconds=0,duration=0;
        if(fscanf(progress,"%lld %lf %lf",&bytes,&seconds,&duration)==3){
            int percent=duration>0?(int)(seconds*100/duration):0;if(percent<0)percent=0;if(percent>99)percent=99;
            snprintf(video_download_message,sizeof(video_download_message),"DOWNLOADING %d%% / %lld MB / D CANCEL",percent,bytes/1048576);
        }fclose(progress);
    }
    int code;if(waitpid(video_download_pid,&code,WNOHANG)!=video_download_pid)return;
    video_download_pid=0;
    if(WIFEXITED(code)&&!WEXITSTATUS(code)){
        char path[80],temp[80];
        snprintf(path,sizeof(path),"video-downloads/%s.tsv",video_download_key);
        snprintf(temp,sizeof(temp),"video-downloads/%s.next",video_download_key);
        FILE *file=fopen(temp,"w");
        if(file){
            for(char *p=video_download_title.title;*p;p++)if(*p=='\t'||*p=='\r'||*p=='\n')*p=' ';
            fprintf(file,"%s\tSAVED OFFLINE\t\tphone-files/offline-%s.mkv\toffline\t0\t0\t0\n",video_download_title.title,video_download_key);
            if(fclose(file)||rename(temp,path))snprintf(status,sizeof(status),"VIDEO SAVED / INDEX WRITE FAILED");
            else snprintf(status,sizeof(status),"DOWNLOAD COMPLETE / OPEN DOWNLOADS TO PLAY OFFLINE");
        }else snprintf(status,sizeof(status),"VIDEO SAVED / INDEX WRITE FAILED");
    }else{
        snprintf(status,sizeof(status),"DOWNLOAD FAILED OR CANCELLED / SEE VIDEO-DOWNLOAD.LOG");
        FILE *error=fopen("video-downloads/error","r");
        if(error){if(fgets(status,sizeof(status),error))status[strcspn(status,"\r\n")]=0;fclose(error);}
    }
    video_download_message[0]=0;
    if(browse.mode==OFFLINE_MODE){char message[sizeof(status)];snprintf(message,sizeof(message),"%s",status);video_download_list();snprintf(status,sizeof(status),"%s",message);}
}
static void video_download_start(const Title *title){
#ifdef _WIN32
    (void)title;snprintf(status,sizeof(status),"LOCAL VIDEO DOWNLOADS REQUIRE THE NATIVE DOWNLOADER");
#else
    if(video_download_pid){kill(video_download_pid,SIGTERM);snprintf(status,sizeof(status),"CANCELLING VIDEO DOWNLOAD");return;}
    if(!video_download_supported(title))return;
    if(access("./greenlink-download",X_OK)){snprintf(status,sizeof(status),"VIDEO DOWNLOADER NOT INSTALLED");return;}
    video_download_title=*title;
    if(browse.mode==6&&browse.name[0])snprintf(video_download_title.title,sizeof(video_download_title.title),"%s",browse.name);
    uint64_t key=14695981039346656037ULL;
    for(const unsigned char *p=(const unsigned char*)title->url;*p;p++){key^=*p;key*=1099511628211ULL;}
    snprintf(video_download_key,sizeof(video_download_key),"%016llx",(unsigned long long)key);
    char path[96];snprintf(path,sizeof(path),"phone-files/offline-%s.mkv",video_download_key);
    if(!access(path,F_OK)){snprintf(status,sizeof(status),"ALREADY SAVED / OPEN DOWNLOADS");return;}
    mkdir("video-downloads",0700);mkdir("phone-files",0700);unlink("video-downloads/progress");unlink("video-downloads/error");
    video_download_pid=shell_fork();
    if(video_download_pid==0){
        int log=open("video-download.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log<0)_exit(126);
        dup2(log,STDOUT_FILENO);dup2(log,STDERR_FILENO);close(log);
        if(!strcmp(title->kind,"kiss-episode"))setenv("FLXTR_MEDIA_PROVIDER","megaplay",1);else unsetenv("FLXTR_MEDIA_PROVIDER");
        char height[16];snprintf(height,sizeof(height),"%d",title->height?title->height:quality_height());
        execl("./greenlink-download","greenlink-download",title->url,video_download_key,height,(char*)NULL);_exit(127);
    }
    if(video_download_pid<0){video_download_pid=0;snprintf(status,sizeof(status),"COULD NOT START DOWNLOAD");return;}
    snprintf(video_download_message,sizeof(video_download_message),"DOWNLOADING TO LOCAL / D CANCEL");
    snprintf(status,sizeof(status),"DOWNLOAD STARTED / BROWSE WHILE IT RUNS");
#endif
}
static void video_download_delete(const Title *title){
    if(strcmp(title->kind,"offline")||!phone_local_media(title->url))return;
    const char *key=title->url+20; /* phone-files/offline- */
    if(strlen(key)!=20||strcmp(key+16,".mkv"))return;
    char path[80];snprintf(path,sizeof(path),"video-downloads/%.16s.tsv",key);
    if(!unlink(title->url)){unlink(path);video_download_list();}
    else snprintf(status,sizeof(status),"COULD NOT DELETE LOCAL VIDEO");
}
static void video_download_selected(const Title *title){
    if(video_download_supported(title)){video_download_start(title);return;}
    if(video_download_offer(title)){
        video_download_requested=1;play();
        if(!catalog_pid)video_download_requested=0;
    }
}
