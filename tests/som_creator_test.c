#include <assert.h>
#include "../src/som_creator.h"

int main(void){
    static uint8_t ram[0x20000];
    SomCreator menu={0};
    const unsigned b=1u<<RETRO_DEVICE_ID_JOYPAD_B;
    const unsigned a=1u<<RETRO_DEVICE_ID_JOYPAD_A;
    const unsigned x=1u<<RETRO_DEVICE_ID_JOYPAD_X;
    const unsigned down=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;
    const unsigned up=1u<<RETRO_DEVICE_ID_JOYPAD_UP;
    const unsigned select=1u<<RETRO_DEVICE_ID_JOYPAD_SELECT;
    const unsigned layers=(1u<<RETRO_DEVICE_ID_JOYPAD_L)|(1u<<RETRO_DEVICE_ID_JOYPAD_R);
    assert(som_creator_slot(ram)==-1);
    ram[0xA156]=ram[0xA20F]=3;
    assert(som_creator_slot(ram)==0);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_CAPTURE&&menu.active);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_CAPTURE&&menu.active);
    assert(som_creator_input(&menu,0,up,0)==SOM_CREATOR_CAPTURE&&menu.skin==0);
    assert(som_creator_input(&menu,0,down|layers,0)==SOM_CREATOR_CAPTURE&&menu.skin==1&&menu.layers==3);
    assert(som_creator_input(&menu,0,select,0)==SOM_CREATOR_CAPTURE&&menu.layers==0);
    assert(som_creator_input(&menu,0,0,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,0,select,0)==SOM_CREATOR_CAPTURE&&menu.layers==3);
    assert(som_creator_input(&menu,0,a,0)==SOM_CREATOR_CAPTURE&&!menu.active);
    assert(som_creator_input(&menu,0,a,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,0,0,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,0,x,0)==SOM_CREATOR_CAPTURE&&menu.active&&!menu.skin&&!menu.layers);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_CONFIRM&&!menu.active);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,0,0,0)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_PASS);
    assert(som_creator_input(&menu,2,0,4)==SOM_CREATOR_CAPTURE&&menu.skin==4);
    assert(som_creator_input(&menu,2,down|layers,4)==SOM_CREATOR_CAPTURE&&menu.skin==4&&!menu.layers);
    assert(som_creator_input(&menu,-1,0,4)==SOM_CREATOR_PASS&&!menu.active);
    uint8_t palette[32]={0};
    som_creator_tint(palette,1,3);
    assert(!palette[10]&&!palette[11]&&palette[12]&&palette[14]);
    assert(!palette[8]&&!palette[16]&&!palette[30]);
    som_creator_tint(palette,0,3);
    assert(palette[10]&&palette[11]);
}
