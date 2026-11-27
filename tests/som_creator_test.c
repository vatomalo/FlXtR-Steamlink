#include <assert.h>
#include "../src/som_creator.h"

int main(void){
    static uint8_t ram[0x20000];
    SomCreator menu={0};
    const unsigned b=1u<<RETRO_DEVICE_ID_JOYPAD_B;
    const unsigned a=1u<<RETRO_DEVICE_ID_JOYPAD_A;
    const unsigned x=1u<<RETRO_DEVICE_ID_JOYPAD_X;
    const unsigned right=1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT;
    assert(som_creator_slot(ram)==-1);
    ram[0xA156]=ram[0xA20F]=3;
    ram[0xA22B]=1;
    assert(som_creator_slot(ram)==1);
    assert(som_creator_input(&menu,1,b,2,0)==SOM_CREATOR_CAPTURE&&menu.active);
    assert(som_creator_input(&menu,1,b,2,0)==SOM_CREATOR_CAPTURE&&menu.active);
    assert(som_creator_input(&menu,1,right,2,0)==SOM_CREATOR_CAPTURE&&menu.selected);
    assert(som_creator_input(&menu,1,a,2,0)==SOM_CREATOR_CAPTURE&&!menu.active);
    assert(som_creator_input(&menu,1,a,2,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,1,0,2,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,1,x,2,0)==SOM_CREATOR_CAPTURE&&menu.active&&!menu.selected);
    assert(som_creator_input(&menu,1,b,2,0)==SOM_CREATOR_CONFIRM&&!menu.active);
    assert(som_creator_input(&menu,1,b,2,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,1,0,2,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,1,b,2,0)==SOM_CREATOR_PASS);
    assert(som_creator_input(&menu,2,0,4,4)==SOM_CREATOR_CAPTURE&&menu.selected);
    assert(som_creator_input(&menu,-1,0,4,4)==SOM_CREATOR_PASS&&!menu.active);
}
