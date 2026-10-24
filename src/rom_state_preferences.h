#ifndef FLXTR_ROM_STATE_PREFERENCES_H
#define FLXTR_ROM_STATE_PREFERENCES_H
static inline int rom_state_slot(const char *key){
    if(!rom_key_valid(key))return -1;
    FILE *f=fopen("rom-state-slots.tsv","r");if(!f)return -1;
    char line[96],saved[17];int slot,result=-1;
    while(fgets(line,sizeof(line),f))if(sscanf(line,"%16[^\t]\t%d",saved,&slot)==2&&
        !strcmp(key,saved)&&slot>=0&&slot<=99)result=slot;
    fclose(f);return result;
}
#endif
