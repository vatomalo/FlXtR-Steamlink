#ifndef FLXTR_ROM_AUDIO_PREFERENCES_H
#define FLXTR_ROM_AUDIO_PREFERENCES_H
static inline int rom_background_music(const char *key){
    if(!rom_key_valid(key))return 0;
    FILE *f=fopen("rom-audio.tsv","r");if(!f)return 0;
    char line[96],saved[17];int enabled,result=0;
    while(fgets(line,sizeof(line),f))if(sscanf(line,"%16[^\t]\t%d",saved,&enabled)==2&&
        !strcmp(key,saved)&&(enabled==0||enabled==1))result=enabled;
    fclose(f);return result;
}
#endif
