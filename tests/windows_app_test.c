/* Exercise the UI's actual Windows player launch path and native IPC. */
#define FLXTR_APP_TEST
#include "../src/shell.c"
#undef main
static int test_main(int argc,char **argv){
    if(argc!=2)return 2;
    if(SDL_Init(SDL_INIT_TIMER))return 1;
    Title title={0};snprintf(title.url,sizeof(title.url),"%s",argv[1]);
    strcpy(title.title,"Windows playback fixture");strcpy(title.kind,"movie");
    tv_preloading=1;launch_player(&title);tv_preloading=0;
    if(player_pid<=0||player_control<0||tv_gate<0){fprintf(stderr,"FAIL: launch native player\n");return 1;}
    SDL_Delay(100);int code=0;
    if(waitpid(player_pid,&code,WNOHANG)!=0){fprintf(stderr,"FAIL: player did not wait at preload gate\n");return 1;}
    char back='b',go='G';
    if(player_pipe_write(player_control,&back,1)!=1||player_pipe_write(tv_gate,&go,1)!=1)return 1;
    player_pipe_close(tv_gate);tv_gate=-1;
    Uint32 began=SDL_GetTicks();
    while(waitpid(player_pid,&code,WNOHANG)==0){
        if(SDL_GetTicks()-began>15000){kill(player_pid,SIGKILL);waitpid(player_pid,NULL,0);return 1;}
        SDL_Delay(10);
    }
    player_pipe_close(player_control);player_control=-1;
    if(!WIFEXITED(code)||WEXITSTATUS(code)!=40){fprintf(stderr,"FAIL: player exit %d\n",code);return 1;}
    FILE *f=fopen("playback-request","rb");int action=-1;if(f){fscanf(f,"%d",&action);fclose(f);}
    if(action!=7){fprintf(stderr,"FAIL: controller response\n");return 1;}
    puts("PASS: actual UI player launch, inherited handle whitelist, preload gate and controller response");
    if(access("music-runtime/bin/python.exe",X_OK))return 1;
    strcpy(music_query,"https://invalid.example/");
    music_spawn(1);if(music_pid<=0)return 1;began=SDL_GetTicks();
    while(music_pid){
        music_poll();
        if(SDL_GetTicks()-began>15000){music_stop();return 1;}SDL_Delay(10);
    }
    if(access("music-cache/error.txt",R_OK)){fprintf(stderr,"FAIL: native music worker\n");return 1;}
    puts("PASS: UI music worker, packaged Python path and nested worker Job Object");
    SDL_Quit();return 0;
}
int wmain(int argc,wchar_t **argv){return flxtr_win_entry(test_main,argc,argv,1);}
