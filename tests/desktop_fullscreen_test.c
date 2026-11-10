#define _POSIX_C_SOURCE 200809L
/* Inspect the real libmpv state initialized by the shared desktop player. */
#include <assert.h>
#include <mpv/client.h>
static int fullscreen_initialize(mpv_handle *handle){
    int result=mpv_initialize(handle);
    if(result>=0){
        int fullscreen=0;
        assert(mpv_get_property(handle,"fullscreen",MPV_FORMAT_FLAG,&fullscreen)>=0);
        assert(fullscreen==1);
    }
    return result;
}
#define mpv_initialize fullscreen_initialize
#ifndef _WIN32
#define main fullscreen_player_main
#endif
#include "../src/player_linux.c"
#undef mpv_initialize
#undef main
#ifndef _WIN32
int main(int argc,char **argv){return fullscreen_player_main(argc,argv);}
#endif
