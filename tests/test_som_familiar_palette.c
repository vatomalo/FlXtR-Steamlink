/* Offline familiar-palette test. Build:
 * cc -std=c11 -Wall -Wextra -Werror -Isrc tests/test_som_familiar_palette.c -o /tmp/test_som_familiar_palette
 * /tmp/test_som_familiar_palette
 */
#include "som_familiar_palette.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned get_color(const uint8_t *p,unsigned i){return p[2*i]|((unsigned)p[2*i+1]<<8);}
int main(void){
    uint8_t p[32]={0},original[32];
    p[0]=0xCD;p[1]=0xAB; /* preserve transparent slot verbatim */
    for(unsigned i=1;i<16;i++){
        const uint16_t c=som_familiar_bgr555(i*2u,i*2u,i*2u);
        p[2*i]=(uint8_t)c;p[2*i+1]=(uint8_t)(c>>8);
    }
    memcpy(original,p,sizeof p);
    som_familiar_palette_celestial(p);
    assert(p[0]==original[0]&&p[1]==original[1]);
    assert(get_color(p,1)!=get_color(p,15));
    for(unsigned i=1;i<16;i++){
        const unsigned c=get_color(p,i);
        assert(c<=0x7FFFu);
        assert((c>>10&31u)>=(c>>5&31u));
        assert((c>>5&31u)>=(c&31u));
    }
    som_familiar_palette_celestial(NULL);
    puts("familiar palette transform OK");
    return 0;
}
