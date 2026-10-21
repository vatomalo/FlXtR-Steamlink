#ifndef FLXTR_ROM_CORE_PREFERENCES_H
#define FLXTR_ROM_CORE_PREFERENCES_H
#include <stdint.h>
static inline void rom_identity(const char *path,char key[17]){
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char*)path;*p;p++){hash^=*p;hash*=UINT64_C(1099511628211);}
    snprintf(key,17,"%016llx",(unsigned long long)hash);
}
static inline int rom_key_valid(const char *key){
    if(strlen(key)!=16)return 0;
    for(int i=0;i<16;i++)if(!isxdigit((unsigned char)key[i]))return 0;
    return 1;
}
static inline int rom_core_name_valid(const char *name){
    size_t n=strlen(name);if(!n||n>=80)return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)name[i])&&name[i]!='_'&&name[i]!='-')return 0;
    return 1;
}
static inline int rom_core_preference(const char *key,char *name,size_t cap){
    if(!rom_key_valid(key))return 0;
    FILE *f=fopen("rom-cores.tsv","r");if(!f)return 0;
    char row[160],saved_key[17],saved_name[96];int found=0;
    while(fgets(row,sizeof(row),f))
        if(sscanf(row,"%16[^\t]\t%95[^\n]",saved_key,saved_name)==2&&
           !strcmp(key,saved_key)&&rom_core_name_valid(saved_name)){
            snprintf(name,cap,"%s",saved_name);found=1;
        }
    fclose(f);return found;
}
static inline int rom_core_preference_save(const char *key,const char *name){
    if(!rom_key_valid(key)||(name&&*name&&!rom_core_name_valid(name)))return 0;
    FILE *in=fopen("rom-cores.tsv","r"),*out=fopen("rom-cores.tsv.next","w");
    if(!out){if(in)fclose(in);return 0;}
    if(in){
        char row[160],saved_key[17],saved_name[96];
        while(fgets(row,sizeof(row),in))
            if(sscanf(row,"%16[^\t]\t%95[^\n]",saved_key,saved_name)==2&&
               rom_key_valid(saved_key)&&rom_core_name_valid(saved_name)&&strcmp(key,saved_key))
                fprintf(out,"%s\t%s\n",saved_key,saved_name);
        fclose(in);
    }
    if(name&&*name)fprintf(out,"%s\t%s\n",key,name);
    if(fclose(out))return 0;
    return !rename("rom-cores.tsv.next","rom-cores.tsv");
}

/* Platform defaults are separate from per-ROM preferences.
 * A missing or unavailable core falls back to the catalog's original core. */
static const char *const rom_platform_names[]={"NES","SNES","GBA","GB/GBC","GENESIS","PSX","PSP","MAME","NEOGEO"};
enum { ROM_PLATFORM_COUNT=9 };
static inline int rom_platform_for_path(const char *path){
    const char *ext=strrchr(path,'.');if(!ext)return -1;
    if(!strcasecmp(ext,".nes"))return 0;
    if(!strcasecmp(ext,".smc")||!strcasecmp(ext,".sfc"))return 1;
    if(!strcasecmp(ext,".gba"))return 2;
    if(!strcasecmp(ext,".gb")||!strcasecmp(ext,".gbc"))return 3;
    if(!strcasecmp(ext,".md")||!strcasecmp(ext,".gen")||!strcasecmp(ext,".smd")||
       !strcasecmp(ext,".sms")||!strcasecmp(ext,".gg"))return 4;
    if(!strcasecmp(ext,".cue")||!strcasecmp(ext,".pbp")||!strcasecmp(ext,".chd")||
       !strcasecmp(ext,".m3u"))return 5;
    if(!strcasecmp(ext,".iso"))return 5;
    if(!strcasecmp(ext,".zip")){
        if(strcasestr(path,"/neogeo/")||strcasestr(path,"/neo-geo/"))return 8;
        if(strcasestr(path,"/mame/")||strcasestr(path,"/arcade/"))return 7;
    }
    return -1;
}
static inline int rom_platform_core_get(int platform,char *name,size_t cap){
    if(platform<0||platform>=ROM_PLATFORM_COUNT)return 0;
    FILE *f=fopen("platform-cores.tsv","r");if(!f)return 0;
    char line[160],saved[96];int id,found=0;
    while(fgets(line,sizeof(line),f))
        if(sscanf(line,"%d\t%95[^\n]",&id,saved)==2&&id==platform&&rom_core_name_valid(saved)){
            snprintf(name,cap,"%s",saved);found=1;
        }
    fclose(f);return found;
}
static inline int rom_platform_core_save(int platform,const char *name){
    if(platform<0||platform>=ROM_PLATFORM_COUNT||(name&&*name&&!rom_core_name_valid(name)))return 0;
    FILE *in=fopen("platform-cores.tsv","r"),*out=fopen("platform-cores.tsv.next","w");
    if(!out){if(in)fclose(in);return 0;}
    if(in){char line[160],saved[96];int id;
        while(fgets(line,sizeof(line),in))
            if(sscanf(line,"%d\t%95[^\n]",&id,saved)==2&&id!=platform&&id>=0&&id<ROM_PLATFORM_COUNT&&rom_core_name_valid(saved))
                fprintf(out,"%d\t%s\n",id,saved);
        fclose(in);
    }
    if(name&&*name)fprintf(out,"%d\t%s\n",platform,name);
    if(fclose(out))return 0;
    return rename("platform-cores.tsv.next","platform-cores.tsv")==0;
}
#endif
