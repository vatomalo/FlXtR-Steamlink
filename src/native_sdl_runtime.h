#ifndef FLXTR_NATIVE_SDL_RUNTIME_H
#define FLXTR_NATIVE_SDL_RUNTIME_H
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
/* Call only in the media/game child. The UI retains its firmware runtime. */
static inline void native_sdl_runtime(const char *owner){
    if(getenv("FLXTR_USE_SYSTEM_SDL"))return;
    char cwd[PATH_MAX],library[PATH_MAX],search[PATH_MAX*2];struct stat st;
    if(!getcwd(cwd,sizeof(cwd)))return;
    int n=snprintf(library,sizeof(library),"%s/retroarch-libs/libSDL2-2.0.so.0",cwd);
    if(n<0||n>=(int)sizeof(library)||stat(library,&st)||!S_ISREG(st.st_mode))return;
    const char *old=getenv("LD_LIBRARY_PATH");
    n=snprintf(search,sizeof(search),"%s/retroarch-libs%s%s",cwd,old&&*old?":":"",old&&*old?old:"");
    if(n<0||n>=(int)sizeof(search)||setenv("LD_LIBRARY_PATH",search,1))return;
    fprintf(stderr,"%s runtime: native SDL2 from %s\n",owner,library);
}
#endif
