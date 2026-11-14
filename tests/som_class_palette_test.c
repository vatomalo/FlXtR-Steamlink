/* Pure palette regression tests; no game memory writes or emulator. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_class_palette.h"
#include "../src/som_familiar.h"
int main(void){
    uint8_t original[32],tinted[32];
    for(unsigned i=0;i<16u;i++){
        uint16_t c=som_pal_rgb(i,(i*2u)%32u,(i*3u)%32u);
        original[i*2u]=(uint8_t)c;
        original[i*2u+1u]=(uint8_t)(c>>8);
    }
    for(unsigned profile=0;profile<SOM_PAL_PROFILE_COUNT;profile++){
        memcpy(tinted,original,sizeof(original));
        som_pal_apply(tinted,profile);
        assert(tinted[0]==original[0]&&tinted[1]==original[1]);
        if(profile==SOM_PAL_ORIGINAL)
            assert(!memcmp(tinted,original,sizeof(original)));
        else
            assert(memcmp(tinted+2,original+2,sizeof(original)-2)!=0);
    }
    for(unsigned shade=0;shade<=31u;shade++){
        uint16_t c=som_pal_color(som_pal_rgb(shade,shade,shade),SOM_PAL_SUMMONER_WHITE);
        assert((c&31u)==((c>>5)&31u));
        assert((c&31u)==((c>>10)&31u));
    }
    assert(som_pal_color(0x4210,SOM_PAL_NINJA_MIDNIGHT) !=
           som_pal_color(0x4210,SOM_PAL_SWORDMASTER_FROST));
    memcpy(tinted,original,sizeof(original));
    som_familiar_tint(tinted);
    uint8_t expected[32];memcpy(expected,original,sizeof(original));
    som_pal_apply(expected,SOM_PAL_CELESTIAL_FAMILIAR);
    assert(!memcmp(tinted,expected,sizeof(expected)));
    puts("class/familiar palette tests passed");
    return 0;
}
