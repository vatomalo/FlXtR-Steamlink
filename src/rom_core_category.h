#ifndef FLXTR_ROM_CORE_CATEGORY_H
#define FLXTR_ROM_CORE_CATEGORY_H
/* A recognizable assigned core determines platform, not file extension or
 * import directory. Unknown cores retain the old filename-based fallback. */
#include <string.h>
#include <strings.h>
#include <ctype.h>
static inline int rom_core_has(const char *haystack,const char *needle){
    size_t n=strlen(needle);
    for(const char *p=haystack;*p;p++){
        size_t i=0;
        while(i<n&&p[i]&&tolower((unsigned char)p[i])==tolower((unsigned char)needle[i]))i++;
        if(i==n)return 1;
    }
    return 0;
}
/* 1-based Games folder IDs: NES=1, SNES=2, GB=3, GBC=4,
 * GBA=5, PSX=6, PSP=7, NEO GEO=8, ARCADE=9, OTHER=10. */
static inline int rom_category_from_core(const char *core,const char *rom){
    if(rom_core_has(core,"ppsspp"))return 7;
    if(rom_core_has(core,"pcsx_rearmed")||rom_core_has(core,"mednafen_psx")||
       rom_core_has(core,"beetle_psx")||rom_core_has(core,"swanstation")||
       rom_core_has(core,"duckstation"))return 6;
    if(rom_core_has(core,"fceumm")||rom_core_has(core,"nestopia")||
       rom_core_has(core,"quicknes")||rom_core_has(core,"mesen"))return 1;
    if(rom_core_has(core,"snes9x")||rom_core_has(core,"catsfc")||
       rom_core_has(core,"bsnes"))return 2;
    if(rom_core_has(core,"gpsp")||rom_core_has(core,"mgba")||
       rom_core_has(core,"vba_next")||rom_core_has(core,"vbam"))return 5;
    if(rom_core_has(core,"gambatte")||rom_core_has(core,"gearboy")||
       rom_core_has(core,"sameboy"))return rom_core_has(rom,".gbc")?4:3;
    if(rom_core_has(core,"neogeo")||rom_core_has(core,"fbalpha2012_neogeo"))return 8;
    if(rom_core_has(core,"mame")||rom_core_has(core,"fbneo")||
       rom_core_has(core,"fbalpha"))return 9;
    return 0;
}
#endif
