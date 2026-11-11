#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char tmp[]="/tmp/flxtr-offline-ui-XXXXXX";assert(mkdtemp(tmp));assert(!chdir(tmp));
    assert(!SDL_Init(SDL_INIT_TIMER));
    FILE *file=fopen("greenlink-download","w");assert(file);
    fputs("#!/bin/sh\nprintf '%s' \"$1\" > requested-url\nprintf 'local movie' > \"phone-files/offline-$2.mkv\"\n",file);fclose(file);chmod("greenlink-download",0700);
    Title title={0};strcpy(title.title,"Offline Movie");strcpy(title.kind,"source");strcpy(title.url,"http://localhost/movie.mp4?literal=$(touch BAD)");
    assert(video_download_supported(&title));
    video_download_start(&title);assert(video_download_pid);
    Uint32 start=SDL_GetTicks();while(video_download_pid){assert(SDL_GetTicks()-start<3000);video_download_finish();SDL_Delay(1);}
    assert(access("BAD",F_OK));assert(strstr(status,"DOWNLOAD COMPLETE"));
    Browse next={OFFLINE_MODE,1,0,0,0,0,"",""};request_catalog(next,0,0);
    assert(total==1&&!strcmp(titles[0].title,"Offline Movie")&&!strcmp(titles[0].kind,"offline"));
    assert(phone_local_media(titles[0].url));assert(!catalog_pid);
    video_download_start(&title);assert(!video_download_pid&&strstr(status,"ALREADY SAVED"));
    /* Deletion requires confirmation; cancelling leaves the local file intact. */
    Title local=titles[0];action(SDLK_y);assert(video_download_delete_confirm&&phone_local_media(local.url));
    action(SDLK_ESCAPE);assert(!video_download_delete_confirm&&phone_local_media(local.url));
    action(SDLK_y);action(SDLK_RETURN);assert(!total&&access(local.url,F_OK));
    strcpy(title.url,"file:///etc/passwd");assert(!video_download_supported(&title));
    file=fopen("greenlink-player","w");assert(file);fputs("#!/bin/sh\ntouch PLAYER_STARTED\n",file);fclose(file);chmod("greenlink-player",0700);
    file=fopen("greenlink-catalog","w");assert(file);
    fputs("#!/bin/sh\nif [ \"$1\" = source ]; then\nprintf 'alpha\\tTEST\\t\\t\\tserver\\t42\\t0\\t0\\n'\nelse\nprintf 'AUTO\\tTEST\\t\\thttp://localhost/resolved.m3u8\\tsource\\t42\\t0\\t0\\n'\nfi\n",file);fclose(file);chmod("greenlink-catalog",0700);
    browse.mode=1;count=total=1;selection=0;visible[0]=0;memset(titles,0,sizeof(titles));
    strcpy(titles[0].title,"Resolved Movie");strcpy(titles[0].kind,"movie");titles[0].id=42;
    action(SDLK_d);assert(video_download_requested&&catalog_pid&&!player_pid);
    start=SDL_GetTicks();while(catalog_pid||video_download_pid){
        assert(SDL_GetTicks()-start<5000);if(catalog_pid)finish_catalog();video_download_finish();SDL_Delay(1);
    }
    assert(!video_download_requested&&!player_pid&&access("PLAYER_STARTED",F_OK));
    video_download_list();assert(total==1&&!strcmp(titles[0].title,"Resolved Movie"));
    /* A video, ROM, and second video must run serially in enqueue order. */
    Title first={0},last={0};
    snprintf(first.title,sizeof(first.title),"Queued Video One");
    snprintf(first.kind,sizeof(first.kind),"source");
    snprintf(first.url,sizeof(first.url),"http://localhost/queued-one.mp4");
    snprintf(last.title,sizeof(last.title),"Queued Video Two");
    snprintf(last.kind,sizeof(last.kind),"source");
    snprintf(last.url,sizeof(last.url),"http://localhost/queued-two.mp4");
    video_download_start(&first);
    assert(video_download_pid>0&&download_active_job>=0);
    pid_t first_pid=video_download_pid;
    Browse rom={16,1,2,0,0,0,"",""};
    download_queue_add_rom(rom);
    video_download_start(&last);
    assert(video_download_pid==first_pid);
    int waiting=0;for(int i=0;i<DOWNLOAD_QUEUE_LIMIT;i++)
        if(download_jobs[i].state==DOWNLOAD_WAITING)waiting++;
    assert(waiting==2);
    start=SDL_GetTicks();
    while(video_download_pid){assert(SDL_GetTicks()-start<5000);video_download_finish();SDL_Delay(1);}
    download_queue_pump();
    assert(download_bg_pid>0&&download_active_job>=0);
    start=SDL_GetTicks();
    while(download_bg_pid){
        int code=0;pid_t done=waitpid(download_bg_pid,&code,WNOHANG);
        if(done==download_bg_pid){
            download_bg_pid=0;
            download_queue_finished(WIFEXITED(code)&&!WEXITSTATUS(code));
        }
        assert(SDL_GetTicks()-start<5000);SDL_Delay(1);
    }
    download_queue_pump();
    assert(video_download_pid>0);
    start=SDL_GetTicks();
    while(video_download_pid){assert(SDL_GetTicks()-start<5000);video_download_finish();SDL_Delay(1);}
    for(int i=0;i<DOWNLOAD_QUEUE_LIMIT;i++)
        assert(download_jobs[i].state!=DOWNLOAD_WAITING&&
            download_jobs[i].state!=DOWNLOAD_RUNNING);
    video_download_list();
    assert(total==3);
    puts("PASS: offline saves, duplicate guard, deletion and FIFO video-ROM-video queue");
    SDL_Quit();return 0;
}
