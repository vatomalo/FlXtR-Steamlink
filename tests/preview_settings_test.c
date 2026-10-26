#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char temp[]="/tmp/flxtr-preview-settings-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    assert(!SDL_Init(SDL_INIT_TIMER));
    settings_on=1;settings_tab=SETTINGS_VIDEO;settings_row=6;default_viewing=VIEW_FIT;
    assert(settings_id()==26);action(SDLK_RIGHT);assert(default_viewing==VIEW_STRETCH);
    mtv_genre=4;save_settings();default_viewing=viewing=VIEW_FIT;mtv_genre=0;load_settings();
    assert(default_viewing==VIEW_STRETCH&&viewing==VIEW_STRETCH&&mtv_genre==4);
    settings_tab=SETTINGS_MUSIC;settings_row=2;action(SDLK_RIGHT);assert(mtv_genre==5);
    mtv_genre=4;
    char query[221];mtv_query(query,sizeof(query));assert(strstr(query,"electronic"));
    FILE *f=fopen("mtv-query.txt","w");assert(f);fputs("my custom music query\n",f);fclose(f);
    mtv_query(query,sizeof(query));assert(strstr(query,"electronic"));
    mtv_genre=0;mtv_query(query,sizeof(query));assert(!strcmp(query,"my custom music query"));
    assert(!mkdir("music-cache",0700));
    f=fopen("greenlink-preview","w");assert(f);
    fputs("#!/bin/sh\nprintf '%s\\n' \"$1\" > preview-url.txt\nprintf '%s\\n' \"$FLXTR_VIDEO_USER_AGENT\" > preview-agent.txt\n",f);fclose(f);assert(!chmod("greenlink-preview",0700));
    f=fopen("music-cache/preview.txt","w");assert(f);
    fputs("Trailer\r\nhttps://example.test/trailer?return=normal\r\nTrailer agent\r\nhttps://www.youtube.com/watch?v=abcdefghijk\r\n",f);fclose(f);
    assert(!preview_launch_decoder());int code;assert(waitpid(preview_decode_pid,&code,0)>0&&WIFEXITED(code)&&!WEXITSTATUS(code));preview_decode_pid=0;
    char line[256];f=fopen("preview-url.txt","r");assert(f&&fgets(line,sizeof(line),f));fclose(f);
    assert(!strcmp(line,"https://example.test/trailer?return=normal\n"));
    f=fopen("preview-agent.txt","r");assert(f&&fgets(line,sizeof(line),f));fclose(f);assert(!strcmp(line,"Trailer agent\n"));
    settings_on=0;coverflow=0;total=1;visible[0]=selection=0;
    titles[0]=(Title){.kind="movie",.title="Trailer test",.id=603};
    renderer=(SDL_Renderer*)1;snprintf(preview_key,sizeof(preview_key),"%d:603:Trailer test",browse.mode);
    preview_lookup_pid=fork();assert(preview_lookup_pid>=0);if(!preview_lookup_pid){for(;;)pause();}
    preview_state=1;preview_selected_at=SDL_GetTicks()-32000;
    assert(!preview_tick()&&preview_lookup_pid>0&&preview_state==1); /* real lookup took 32 seconds */
    preview_selected_at=SDL_GetTicks()-76000;assert(preview_tick()&&preview_state==3&&!preview_lookup_pid);
    renderer=NULL;preview_stop();unlink("music-cache/preview.txt");rmdir("music-cache");
    f=fopen("greenlink-player","w");assert(f);fputs("#!/bin/sh\nprintf '%s\\n' \"$@\" > player-arguments.txt\n",f);fclose(f);assert(!chmod("greenlink-player",0700));
    Title video={.kind="youtube",.url="https://example.test/video",.title="MTV test"};
    viewing=default_viewing=VIEW_STRETCH;launch_player(&video);assert(player_pid>0);
    assert(waitpid(player_pid,&code,0)>0&&WIFEXITED(code)&&!WEXITSTATUS(code));player_pid=0;
    player_pipe_close(player_control);player_control=-1;
    f=fopen("player-arguments.txt","r");assert(f);char arguments[2048];size_t n=fread(arguments,1,sizeof(arguments)-1,f);arguments[n]=0;fclose(f);
    assert(strstr(arguments,"--view\nstretch\n"));
    unlink("player-arguments.txt");unlink("player.log");unlink("greenlink-player");unlink("subtitle-debug.log");
    unlink("preview-url.txt");unlink("preview-agent.txt");unlink("preview-decoder.log");unlink("greenlink-preview");
    unlink("settings.cfg");unlink("tv-gambits.cfg");unlink("mtv-query.txt");SDL_Quit();assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: saved video scaling and MTV genres, custom query priority, restored preview URL/agent handoff");
}
