#include <assert.h>
#include <string.h>
#include "../src/som_creator.h"

int main(void){
    static uint8_t ram[0x20000];
    SomCreator menu={0};
    const unsigned b=1u<<RETRO_DEVICE_ID_JOYPAD_B;
    const unsigned a=1u<<RETRO_DEVICE_ID_JOYPAD_A;
    const unsigned x=1u<<RETRO_DEVICE_ID_JOYPAD_X;
    const unsigned down=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;
    const unsigned up=1u<<RETRO_DEVICE_ID_JOYPAD_UP;
    const unsigned right=1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT;
    const unsigned left=1u<<RETRO_DEVICE_ID_JOYPAD_LEFT;
    const unsigned select=1u<<RETRO_DEVICE_ID_JOYPAD_SELECT;
    const unsigned layers=(1u<<RETRO_DEVICE_ID_JOYPAD_L)|(1u<<RETRO_DEVICE_ID_JOYPAD_R);
    assert(som_creator_slot(ram)==-1);
    ram[0xA156]=ram[0xA20F]=3;
    assert(som_creator_slot(ram)==0);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_CAPTURE&&menu.active);
    assert(som_creator_input(&menu,0,b,0)==SOM_CREATOR_CAPTURE&&menu.active);
    assert(som_creator_input(&menu,0,left,0)==SOM_CREATOR_CAPTURE&&menu.skin==0);
    assert(som_creator_input(&menu,0,right|layers,0)==SOM_CREATOR_CAPTURE&&menu.skin==1&&menu.layers==3);
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
    assert(som_creator_input(&menu,2,right|layers,4)==SOM_CREATOR_CAPTURE&&menu.skin==4&&!menu.layers);
    assert(som_creator_input(&menu,-1,0,4)==SOM_CREATOR_PASS&&!menu.active);
    uint8_t palette[32]={0};
    som_creator_tint(palette,1,3);
    assert(!palette[10]&&!palette[11]&&palette[12]&&palette[14]);
    assert(!palette[8]&&!palette[16]&&!palette[30]);
    som_creator_tint(palette,0,3);
    assert(palette[10]&&palette[11]);
    /* hair: Down to the style row, Left wraps to the last style; Down to
     * colour, Right; Up back to skin. The accepted hair comes back on open. */
    SomCreator h={0};
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE&&h.hair==3&&h.hair_colour==4&&h.row==0);
    assert(som_creator_input_hair(&h,1,down,0,3,4)==SOM_CREATOR_CAPTURE&&h.row==1);
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,right,0,3,4)==SOM_CREATOR_CAPTURE&&h.hair==4);
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,right,0,3,4)==SOM_CREATOR_CAPTURE&&h.hair==0);
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,left,0,3,4)==SOM_CREATOR_CAPTURE&&h.hair==SOM_HAIR_STYLES-1u);
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,down,0,3,4)==SOM_CREATOR_CAPTURE&&h.row==2);
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,right,0,3,4)==SOM_CREATOR_CAPTURE&&h.hair_colour==5&&h.skin==0);
    assert(som_creator_input_hair(&h,1,0,0,3,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,up|up,0,3,4)==SOM_CREATOR_CAPTURE&&h.row==1);
    /* every art row fits; all views of a style exist; native draws nothing */
    for(unsigned st=1;st<SOM_HAIR_STYLES;st++)for(unsigned v=0;v<3u;v++){
        unsigned ink=0;
        for(unsigned y=0;y<SOM_HAIR_H;y++){
            const char *row=som_hair_art[st][v][y];
            assert(row&&strlen(row)<=SOM_HAIR_W);
            for(const char *c=row;*c;c++){assert(strchr(".KDAaG",*c));ink+=*c!='.';}
        }
        assert(ink>10u);
    }
    for(int u=0;u<SOM_HAIR_W;u++)assert(som_hair_at(SOM_HAIR_NATIVE,1u,u,4,0ul)=='.');
    /* left mirrors right */
    for(int v=0;v<SOM_HAIR_H;v++)for(int u=0;u<SOM_HAIR_W;u++)
        assert(som_hair_at(SOM_HAIR_SPIKY,0x02u,u,v,0ul)==som_hair_at(SOM_HAIR_SPIKY,0x82u,SOM_HAIR_W-1-u,v,0ul));
    /* colour: hair entries 2-4 (and the sprite's 8), nothing else */
    uint8_t pal[32];memset(pal,0,sizeof(pal));
    som_hair_tint(pal,0,1);
    assert((pal[4]|pal[5])&&(pal[8]|pal[9])&&!pal[2]&&!pal[10]&&!pal[16]);
    som_hair_tint(pal,2,4);
    assert(pal[16]|pal[17]);
    memset(pal,0,sizeof(pal));som_hair_tint(pal,1,0);
    for(unsigned i=0;i<32u;i++)assert(!pal[i]);
}
