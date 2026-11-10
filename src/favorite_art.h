#ifndef FLXTR_FAVORITE_ART_H
#define FLXTR_FAVORITE_ART_H
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
static void favorite_art_path(char *path,size_t capacity,const char *kind,const char *title,const char *url,int id,int season,int episode){
    uint32_t hash=2166136261U;const char *parts[]={kind,title,url};
    for(unsigned i=0;i<3;i++)for(const unsigned char *p=(const unsigned char*)parts[i];*p;p++)hash=(hash^*p)*16777619U;
    hash=(hash^(unsigned)id)*16777619U;hash=(hash^(unsigned)season)*16777619U;hash=(hash^(unsigned)episode)*16777619U;
    snprintf(path,capacity,"favorites-art/%08lx.bmp",(unsigned long)hash);
}
static int favorite_art_valid(const char *path){
    struct stat st;unsigned char h[26];
    if(!path[0]||stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<54||st.st_size>192*288*4+4096)return 0;
    FILE *file=fopen(path,"rb");if(!file)return 0;
    size_t n=fread(h,1,sizeof(h),file);fclose(file);
    if(n!=sizeof(h)||h[0]!='B'||h[1]!='M'||h[14]<40)return 0;
    uint32_t w=(uint32_t)h[18]|(uint32_t)h[19]<<8|(uint32_t)h[20]<<16|(uint32_t)h[21]<<24;
    int32_t height=(int32_t)((uint32_t)h[22]|(uint32_t)h[23]<<8|(uint32_t)h[24]<<16|(uint32_t)h[25]<<24);
    if(!w||w>192||!height||height>288||height< -288)return 0;
    SDL_Surface *image=SDL_LoadBMP(path);if(!image)return 0;SDL_FreeSurface(image);return 1;
}
#endif
