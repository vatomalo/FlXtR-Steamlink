#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char temp[]="/tmp/flxtr-favorite-ui-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));assert(!SDL_Init(SDL_INIT_TIMER));
    FILE *file=fopen("favorites.tsv","w");assert(file);fputs("Saved movie\tTEST\tmissing.bmp\t\tmovie\t42\t0\t0\n",file);fclose(file);
    SDL_Surface *image=SDL_CreateRGBSurfaceWithFormat(0,16,24,32,SDL_PIXELFORMAT_ARGB8888);assert(image);
    assert(!SDL_SaveBMP(image,"downloaded.bmp"));SDL_FreeSurface(image);
    char path[96];favorite_art_path(path,sizeof(path),"movie","Saved movie","",42,0,0);
    file=fopen("greenlink-catalog","w");assert(file);
    fprintf(file,"#!/bin/sh\nsleep 0.1\nmkdir -p favorites-art\ncp downloaded.bmp %s\n",path);fclose(file);chmod("greenlink-catalog",0700);
    Browse favorite={FAVORITES_MODE,1,0,0,0,0,"",""};request_catalog(favorite,0,0);
    assert(total==1&&!strcmp(titles[0].poster,path));
    Uint32 start=SDL_GetTicks();favorites_art_tick();assert(favorite_art_pid&&SDL_GetTicks()-start<100);
    while(favorite_art_pid){assert(SDL_GetTicks()-start<3000);favorites_art_tick();SDL_Delay(1);}
    assert(favorite_art_valid(titles[0].poster)&&cached_page==-1);
    favorite_art_retry=0;favorites_art_tick();assert(!favorite_art_pid);
    assert(!unlink(path));favorite_art_retry=0;favorites_art_tick();assert(favorite_art_pid);
    start=SDL_GetTicks();while(favorite_art_pid){assert(SDL_GetTicks()-start<3000);favorites_art_tick();SDL_Delay(1);}
    assert(favorite_art_valid(path));
    assert(!unlink(path));file=fopen("greenlink-catalog","w");assert(file);fputs("#!/bin/sh\nexit 1\n",file);fclose(file);chmod("greenlink-catalog",0700);
    favorite_art_retry=0;favorites_art_tick();assert(favorite_art_pid);
    start=SDL_GetTicks();while(favorite_art_pid){assert(SDL_GetTicks()-start<3000);favorites_art_tick();SDL_Delay(1);}
    favorites_art_tick();assert(!favorite_art_pid&&(Sint32)(favorite_art_retry-SDL_GetTicks())>290000);
    file=fopen("favorites.tsv","r");assert(file);char row[256];assert(fgets(row,sizeof(row),file));fclose(file);
    assert(!strcmp(row,"Saved movie\tTEST\tmissing.bmp\t\tmovie\t42\t0\t0\n"));
    puts("PASS: background cover job, cache refresh, missing-file re-download, offline backoff, favorites preserved");SDL_Quit();return 0;
}
