/* Shared stable game-metadata cache key. No ROM or core paths are modified. */
#ifndef FLXTR_GAME_METADATA_H
#define FLXTR_GAME_METADATA_H
static void game_info_path(const char *title,char *out,size_t cap){
    unsigned long h=2166136261UL;
    for(const unsigned char *p=(const unsigned char *)title;*p;p++){
        unsigned char c=*p;
        if(c>='a'&&c<='z')c=(unsigned char)(c-32);
        h=((h^c)*16777619UL)&0xffffffffUL;
    }
    snprintf(out,cap,"media-info-cache/game-%08lx.txt",h);
}
#endif
