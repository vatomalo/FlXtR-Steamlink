#ifndef FLXTR_SOM_CREATOR_H
#define FLXTR_SOM_CREATOR_H

#include <stdint.h>
#include <stdio.h>
#include "som_overlay.h"
#include "vendor/libretro.h"
#include "som_hair.h"
#include "som_heads.h"
#include "som_disciplines.h"

typedef struct {
    uint16_t previous;
    uint8_t slot, active, seen, release, skin, layers;
    uint8_t row, hair, hair_colour;        /* row: 0 skin, 1 hair (0 native, n: game head n-1), 2 hair colour */
    uint8_t heads;                         /* game heads on offer (som_heads.h) */
    uint8_t cls;                           /* row 3: first class, 0 none, n: class n-1 */
} SomCreator;

enum { SOM_CREATOR_PASS, SOM_CREATOR_CAPTURE, SOM_CREATOR_CONFIRM };
enum { SOM_CREATOR_SKINS=5, SOM_CREATOR_HAIR_OFF=1, SOM_CREATOR_CLOTHES_OFF=2, SOM_CREATOR_ROWS=4 };
#define SOM_RGB5(r,g,b) ((r)|((g)<<5)|((b)<<10))
static const uint16_t som_creator_skins[SOM_CREATOR_SKINS][3]={
    {0xffff,0xffff,0xffff},
    {SOM_RGB5(22,12,9),SOM_RGB5(27,17,13),SOM_RGB5(31,25,19)},
    {SOM_RGB5(18,10,6),SOM_RGB5(23,15,9),SOM_RGB5(30,22,13)},
    {SOM_RGB5(14,7,4),SOM_RGB5(18,10,6),SOM_RGB5(25,15,9)},
    {SOM_RGB5(8,5,4),SOM_RGB5(12,7,5),SOM_RGB5(18,11,8)}
};
#undef SOM_RGB5
static const char *const som_creator_skin_names[]={"NATIVE","IVORY","GOLDEN","COPPER","UMBER"};

static inline void som_creator_tint(uint8_t palette[32],unsigned slot,unsigned skin){
    if(!skin||skin>=SOM_CREATOR_SKINS)return;
    for(unsigned shade=slot?1u:0u;shade<3u;shade++){
        const unsigned colour=som_creator_skins[skin][shade];
        const unsigned offset=2u*(5u+shade);
        palette[offset]=(uint8_t)colour;
        palette[offset+1u]=(uint8_t)(colour>>8);
    }
}

static inline int som_creator_slot(const uint8_t *ram){
    if(!ram||ram[0xA20F]!=3u||ram[0xA156]!=3u||ram[0xA22B]>=3u)return -1;
    return ram[0xA22B];
}

/* applied: the slot's accepted skin; hair / hair_colour: its accepted hair
 * (both 0 = native), loaded when the panel opens. */
/* cls: the slot's accepted first class (0 none, n: class n-1). */
static inline int som_creator_input_full(SomCreator *menu,int slot,uint16_t buttons,
                                         unsigned applied,unsigned hair,unsigned hair_colour,unsigned cls){
    const uint16_t pressed=(uint16_t)(buttons&~menu->previous);
    menu->previous=buttons;
    if(slot<0){
        menu->active=menu->seen=0;
        if(menu->release&&buttons)return SOM_CREATOR_CAPTURE;
        menu->release=0;
        return SOM_CREATOR_PASS;
    }
    if(menu->release){
        if(!buttons)menu->release=0;
        return SOM_CREATOR_CAPTURE;
    }
    if(!menu->seen||menu->slot!=slot||
       (!menu->active&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_X)))){
        menu->slot=(uint8_t)slot;
        menu->seen=menu->active=1;
        menu->skin=(uint8_t)applied;
        menu->hair=(uint8_t)(hair<=menu->heads?hair:0u);
        menu->hair_colour=(uint8_t)(hair_colour<SOM_HAIR_COLOURS?hair_colour:0u);
        menu->cls=(uint8_t)(cls<=SOM_DISCIPLINE_COUNT?cls:0u);
        menu->layers=0;menu->row=0;
        return SOM_CREATOR_CAPTURE;
    }
    if(!menu->active)return SOM_CREATOR_PASS;
    /* Up/Down: the row; Left/Right: its value (skin clamps, hair wraps). */
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))menu->row=(uint8_t)((menu->row+SOM_CREATOR_ROWS-1u)%SOM_CREATOR_ROWS);
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))menu->row=(uint8_t)((menu->row+1u)%SOM_CREATOR_ROWS);
    {
        const int left=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))!=0,right=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))!=0;
        if(left!=right){
            if(menu->row==0u){
                if(left&&menu->skin)menu->skin--;
                if(right&&menu->skin+1u<SOM_CREATOR_SKINS)menu->skin++;
            }else if(menu->row==1u)
                menu->hair=(uint8_t)((menu->hair+(right?1u:menu->heads))%(menu->heads+1u));
            else if(menu->row==2u)
                menu->hair_colour=(uint8_t)((menu->hair_colour+(right?1u:SOM_HAIR_COLOURS-1u))%SOM_HAIR_COLOURS);
            else menu->cls=(uint8_t)((menu->cls+(right?1u:SOM_DISCIPLINE_COUNT))%(SOM_DISCIPLINE_COUNT+1u));
        }
    }
    if(slot==0&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_L)))menu->layers^=SOM_CREATOR_HAIR_OFF;
    if(slot==0&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_R)))menu->layers^=SOM_CREATOR_CLOTHES_OFF;
    if(slot==0&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_SELECT)))
        menu->layers=menu->layers==(SOM_CREATOR_HAIR_OFF|SOM_CREATOR_CLOTHES_OFF)?
            0u:SOM_CREATOR_HAIR_OFF|SOM_CREATOR_CLOTHES_OFF;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A)){
        menu->active=0;
        menu->release=1;
        return SOM_CREATOR_CAPTURE;
    }
    if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_B)|(1u<<RETRO_DEVICE_ID_JOYPAD_START))){
        menu->active=0;
        menu->release=1;
        return SOM_CREATOR_CONFIRM;
    }
    return SOM_CREATOR_CAPTURE;
}

static inline int som_creator_input_hair(SomCreator *menu,int slot,uint16_t buttons,
                                         unsigned applied,unsigned hair,unsigned hair_colour){
    return som_creator_input_full(menu,slot,buttons,applied,hair,hair_colour,0u);
}
static inline int som_creator_input(SomCreator *menu,int slot,uint16_t buttons,unsigned applied){
    return som_creator_input_full(menu,slot,buttons,applied,0u,0u,0u);
}

/* A game head (facing the player) over a naming sprite drawn at base
 * (ox, oy) at `scale` (the preview: 2; the naming page itself: 1): its eye
 * row on the sprite's (row 13, measured on the naming sprite 2026-11-27),
 * centred on the sprite's head (the matte's top rows; column 8 without
 * one), in the hero's skin and the chosen (or the head's own) hair colour. */
static inline void som_creator_draw_head(uint8_t *pixels,unsigned width,unsigned height,size_t pitch,int format,
                                         const uint8_t *head,unsigned slot,unsigned skin_pick,unsigned colour,
                                         int ox,int oy,int scale,const uint8_t *matte){
    const unsigned sx=width>=512u?2u:1u,sy=height>=400u?2u:1u;
    if(!head||!pixels)return;
    int top=-1,l=18,r=-1;
    for(int y=0;matte&&y<32&&top<0;y++)for(int x=0;x<18;x++)if(matte[y*18+x]){top=y;break;}
    for(int y=top;matte&&y>=0&&y<top+6&&y<32;y++)for(int x=0;x<18;x++)
        if(matte[y*18+x]){if(x<l)l=x;if(x>r)r=x;}
    const int cx=r>=l?(l+r+1)/2:8;
    int eye=-1,chin=-1;
    for(int y=0;y<SOM_HEAD_SIZE;y++)for(int x=0;x<SOM_HEAD_SIZE;x++){
        const uint8_t q=som_head_role(head,1u,x,y);
        if(q==15u)eye=y;
        if(q>=5u&&q<=7u)chin=y;
    }
    if(eye<0)eye=chin>=0?chin-2:SOM_HEAD_SIZE-4;
    static const uint8_t skins[3][3][3]={
        {{139,82,57},{213,123,82},{246,189,123}},
        {{222,131,90},{222,131,90},{255,222,172}},
        {{213,139,115},{213,139,115},{255,197,172}}};
    uint8_t skin[3][3];
    for(unsigned k=0;k<3u;k++){
        const unsigned c=skin_pick&&skin_pick<SOM_CREATOR_SKINS?som_creator_skins[skin_pick][k]:0xFFFFu;
        if(c==0xFFFFu){memcpy(skin[k],skins[slot%3u][k],3);continue;}
        skin[k][0]=(uint8_t)((c&31u)<<3);skin[k][1]=(uint8_t)(((c>>5)&31u)<<3);skin[k][2]=(uint8_t)(((c>>10)&31u)<<3);
    }
    static const uint8_t outline[3]={49,41,32},white[3]={250,250,250};
    for(int v=0;v<SOM_HEAD_SIZE;v++)for(int u=0;u<SOM_HEAD_SIZE;u++){
        const uint8_t q=som_head_role(head,1u,u,v);
        if(!q)continue;
        const uint8_t *c;
        if(q==1u)c=outline;
        else if(q<=4u)c=colour&&colour<SOM_HAIR_COLOURS?som_hair_ramps[colour][q-2u]:som_head_hair(head,q-2u);
        else if(q<=7u)c=skin[q-5u];
        else if(q==15u)c=white;
        else if(q-16u<head[10])c=head+11u+3u*(q-16u);
        else c=outline;
        const int x=cx-SOM_HEAD_SIZE/2+u,y=13-eye+v;
        if(x<-4||x>=22||y<-4||y>=32)continue;
        const int px=ox+scale*x,py=oy+scale*y;
        if(px<0||py<0||(unsigned)(px+scale)*sx>width||(unsigned)(py+scale)*sy>height)continue;
        som_overlay_fill(pixels,pitch,format,(unsigned)px*sx,(unsigned)py*sy,(unsigned)scale*sx,(unsigned)scale*sy,
                         som_overlay_color(format,c[0],c[1],c[2]));
    }
}

static inline void som_creator_draw(const SomCreator *menu,uint8_t *pixels,unsigned width,
                                    unsigned height,size_t pitch,int format,
                                    const uint8_t *native,size_t native_pitch,const uint8_t *matte,
                                    const uint8_t *head){
    const unsigned sx=width>=512u?2u:1u,sy=height>=400u?2u:1u;
    if(width<256u*sx||height<224u*sy)return;
    const uint32_t back=som_overlay_color(format,13,26,32);
    const uint32_t panel=som_overlay_color(format,27,48,53);
    const uint32_t mint=som_overlay_color(format,132,225,181);
    const uint32_t text=som_overlay_color(format,235,239,218);
    som_overlay_fill(pixels,pitch,format,0,0,width,height,back);
    som_overlay_fill(pixels,pitch,format,12*sx,37*sy,88*sx,142*sy,panel);
    som_overlay_fill(pixels,pitch,format,110*sx,37*sy,134*sx,142*sy,panel);
    som_overlay_label(pixels,width,height,pitch,format,"CHARACTER CREATOR",128,17,1,mint);
    som_overlay_label(pixels,width,height,pitch,format,"APPEARANCE",177,52,1,mint);
    static const char *const actors[]={"RANDI SLOT","PRIMM SLOT","POPOI SLOT"};
    som_overlay_label(pixels,width,height,pitch,format,actors[menu->slot],56,52,1,text);
    {
        const uint32_t gold=som_overlay_color(format,255,223,143);
        char line[40];
        static const char *const rows[SOM_CREATOR_ROWS]={"SKIN","HAIR","COLOUR","CLASS"};
        char head[16];
        if(menu->hair)snprintf(head,sizeof(head),"%u/%u",(unsigned)menu->hair,(unsigned)menu->heads);
        else snprintf(head,sizeof(head),"NATIVE");
        const char *values[SOM_CREATOR_ROWS]={som_creator_skin_names[menu->skin%SOM_CREATOR_SKINS],
            head,som_hair_colour_names[menu->hair_colour%SOM_HAIR_COLOURS],
            menu->cls&&menu->cls<=SOM_DISCIPLINE_COUNT?som_discipline_titles[menu->cls-1u]:"NONE"};
        for(unsigned r=0;r<SOM_CREATOR_ROWS;r++){
            snprintf(line,sizeof(line),"%c%-6s <%s>",menu->row==r?'>':' ',rows[r],values[r]);
            som_overlay_label(pixels,width,height,pitch,format,line,112,66+(int)r*13,0,menu->row==r?gold:text);
        }
    }
    som_overlay_label(pixels,width,height,pitch,format,"UP/DN ROW  L/R CHANGE",177,121,1,mint);
    if(menu->slot==0){
        som_overlay_label(pixels,width,height,pitch,format,menu->layers&SOM_CREATOR_HAIR_OFF?
                          "L: SHOW HAIR":"L: HIDE HAIR",177,135,1,text);
        som_overlay_label(pixels,width,height,pitch,format,menu->layers&SOM_CREATOR_CLOTHES_OFF?
                          "R: SHOW CLOTHES":"R: HIDE CLOTHES",177,148,1,text);
        som_overlay_label(pixels,width,height,pitch,format,"SELECT: BASE MODEL",177,163,1,mint);
    }
    if(native){
        /* The observed Turbo naming preview sits at x=144, y=16 in
         * base coordinates. Retain the native pixels and scale by 2. */
        const unsigned bytes=format==1?4u:2u;
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<18;x++){
            if(matte&&!matte[y*18u+x])continue;
            uint32_t ink=0;
            memcpy(&ink,native+(16u+y)*sy*native_pitch+(143u+x)*sx*bytes,bytes);
            som_overlay_fill(pixels,pitch,format,(38u+2u*x)*sx,(67u+2u*y)*sy,2u*sx,2u*sy,ink);
        }
    }
    /* the game head on the preview, eyes on its eye row (13) */
    som_creator_draw_head(pixels,width,height,pitch,format,head,menu->slot,menu->skin,menu->hair_colour,
                          38,67,2,matte);
    som_overlay_label(pixels,width,height,pitch,format,
                      menu->layers==(SOM_CREATOR_HAIR_OFF|SOM_CREATOR_CLOTHES_OFF)?
                      "BASE MODEL":"NATIVE BODY",56,141,1,text);
    som_overlay_label(pixels,width,height,pitch,format,"B CONTINUE   A CANCEL",128,195,1,mint);
}
#endif
