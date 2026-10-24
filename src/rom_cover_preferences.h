#ifndef FLXTR_ROM_COVER_PREFERENCES_H
#define FLXTR_ROM_COVER_PREFERENCES_H
/* Store a snapshot of the donor's artwork, never change ROM or launch identity. */
static inline int rom_cover_override(const char *key,char *poster,size_t cap){
    if(!rom_key_valid(key))return 0;
    FILE *f=fopen("rom-covers.tsv","r");if(!f)return 0;
    char line[1024];int found=0;
    while(fgets(line,sizeof(line),f)){
        char *a=strchr(line,'\t');if(!a)continue;*a++=0;
        char *b=strchr(a,'\t');if(!b)continue;*b++=0;
        char *c=strchr(b,'\t');if(!c)continue;*c++=0;c[strcspn(c,"\r\n")]=0;
        if(strcmp(line,key)||!rom_key_valid(a)||strlen(c)>=cap||strstr(c,".."))continue;
        if(strncmp(c,"gameart:",8)&&strncmp(c,"localthumb:",11)&&strncmp(c,"game-art-cache/",15))continue;
        snprintf(poster,cap,"%s",c);found=1;
    }
    fclose(f);return found;
}
#endif
