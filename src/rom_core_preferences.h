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
#endif
