#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/som_depth_mask.h"

int main(void){
    uint8_t oam[544]={0};
    oam[2]=0xC0; oam[3]=0xA3; /* boy tile, palette 1, priority 2 */
    assert(som_owner_sprite_depth(oam,0u)==44u);
    uint8_t base[8]={1,2,3,4,5,6,7,8};
    uint8_t frame[8]={9,9,9,9,9,9,9,9};
    const uint8_t depth[4]={43,44,45,46};
    som_mask_behind_foreground(frame,base,4,1,8,2,depth,4,44);
    assert(frame[0]==9&&frame[1]==9&&frame[2]==9&&frame[3]==9);
    assert(memcmp(frame+4,base+4,4)==0);
    return 0;
}
