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
    /* hair: Down to the hair row; Left/Right step through native and the
     * game heads on offer (wrapping); Down to colour, Right; Up back. */
    SomCreator h={0};
    h.heads=3;
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE&&h.hair==2&&h.hair_colour==4&&h.row==0);
    assert(som_creator_input_hair(&h,1,down,0,2,4)==SOM_CREATOR_CAPTURE&&h.row==1);
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,right,0,2,4)==SOM_CREATOR_CAPTURE&&h.hair==3);
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,right,0,2,4)==SOM_CREATOR_CAPTURE&&h.hair==0);
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,left,0,2,4)==SOM_CREATOR_CAPTURE&&h.hair==3);
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,down,0,2,4)==SOM_CREATOR_CAPTURE&&h.row==2);
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,right,0,2,4)==SOM_CREATOR_CAPTURE&&h.hair_colour==5&&h.skin==0);
    assert(som_creator_input_hair(&h,1,0,0,2,4)==SOM_CREATOR_CAPTURE);
    assert(som_creator_input_hair(&h,1,up,0,2,4)==SOM_CREATOR_CAPTURE&&h.row==1);
    /* no pack: only native */
    SomCreator z={0};
    assert(som_creator_input_hair(&z,0,0,0,5,0)==SOM_CREATOR_CAPTURE&&z.hair==0);
    /* a head pack: parse, roles, the hero's palette entry for each role */
    static uint8_t pk[10+2*SOM_HEAD_ENTRY];
    memcpy(pk,"SOMHEAD1",8);pk[8]=2;
    uint8_t *e=pk+10;
    e[0]=0x9D;e[1]=10;e[4]=20;e[7]=30;e[10]=1;e[11]=200;e[12]=0;e[13]=0;
    e[35+256+5*16+5]=3;e[35+256+6*16+5]=6;e[35+256+7*16+5]=16;
    SomHeadPack hp={0};
    assert(!som_heads_parse(&hp,pk,10));
    assert(som_heads_parse(&hp,pk,sizeof(pk))&&hp.count==2&&som_head_entry(&hp,0)[0]==0x9D&&!som_head_entry(&hp,2));
    assert(som_head_role(som_head_entry(&hp,0),1,5,5)==3&&som_head_role(som_head_entry(&hp,0),1,5,6)==6);
    assert(som_head_hair(som_head_entry(&hp,0),2)[0]==30);
    uint8_t live[32]={0};
    live[2*13]=31;                                   /* entry 13: pure red */
    assert(som_head_index(e,0,6,live)==6&&som_head_index(e,1,5,live)==6&&som_head_index(e,2,7,live)==7);
    assert(som_head_index(e,1,3,live)==3&&som_head_index(e,0,15,live)==15&&som_head_index(e,0,0,live)==0);
    assert(som_head_index(e,0,16,live)==13);        /* the extra red: the nearest hero colour */
    som_heads_free(&hp);
    /* colour: hair entries 2-4 (and the sprite's 8), nothing else */
    uint8_t pal[32];memset(pal,0,sizeof(pal));
    som_hair_tint(pal,0,1);
    assert((pal[4]|pal[5])&&(pal[8]|pal[9])&&!pal[2]&&!pal[10]&&!pal[16]);
    som_hair_tint(pal,2,4);
    assert(pal[16]|pal[17]);
    memset(pal,0,sizeof(pal));som_hair_tint(pal,1,0);
    for(unsigned i=0;i<32u;i++)assert(!pal[i]);
}
