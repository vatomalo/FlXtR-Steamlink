#ifndef FLXTR_SOM_THROW_SKIN_H
#define FLXTR_SOM_THROW_SKIN_H
/* Looks for the game's thrown boomerang (2026-11-25): the ninja's is the
 * ring menu's own shuriken icon, read from the player's ROM; the hunter's
 * is a painted holy cross. Only the picture: flight, hits and return stay
 * the game's. Projectile table 7E:D000 + 0x40*hero + 0x10*k
 * (weapons-ranged.md 3.2); its sprite offset varies with facing. */
#include <stdint.h>
#include <string.h>
#include "som_overlay.h"
#define SOM_ICON_BYTES 96u
#define SOM_THROW_MATCH_RADIUS 24
enum {SOM_SKIN_NONE,SOM_SKIN_STAR,SOM_SKIN_CROSS};
typedef struct {int kind,x,y;} SomThrowSkin;

/* A 16x16 menu icon (four 3bpp tiles) as colour numbers 0..7. */
static inline void som_icon_decode(const uint8_t *b,uint8_t out[256]){
    for(unsigned t=0;t<4u;t++)for(unsigned y=0;y<8u;y++)for(unsigned x=0;x<8u;x++){
        const uint8_t *q=b+24u*t;
        out[((t>>1)*8u+y)*16u+(t&1u)*8u+x]=(uint8_t)((q[2u*y]>>(7u-x)&1u)|
            (q[2u*y+1u]>>(7u-x)&1u)<<1|(q[16u+y]>>(7u-x)&1u)<<2);
    }
}
/* Colour numbers run light (1) to dark (4); 5 is the outline, 7 the empty
 * background of every ring icon. */
static inline uint32_t som_icon_steel(int format,unsigned v){
    static const uint8_t ramp[8][3]={{0,0,0},{250,252,255},{200,208,226},{140,152,178},
        {88,98,126},{22,24,36},{54,60,84},{0,0,0}};
    return som_overlay_color(format,ramp[v&7u][0],ramp[v&7u][1],ramp[v&7u][2]);
}
static inline void som_skin_dot(uint8_t *px,unsigned width,unsigned height,size_t pitch,int format,
                                int x,int y,uint32_t ink){
    const unsigned s=width>=512u?2u:1u,t=height>=400u?2u:1u;
    if(x<0||y<0||x>=(int)(width/s)||y>=(int)(height/t))return;
    som_overlay_fill(px,pitch,format,(unsigned)x*s,(unsigned)y*t,s,t,ink);
}
static inline void som_icon_draw(uint8_t *px,unsigned width,unsigned height,size_t pitch,int format,
                                 const uint8_t icon[256],int x0,int y0,unsigned turn){
    for(int y=0;y<16;y++)for(int x=0;x<16;x++){
        const int sx=turn==0u?x:turn==1u?y:turn==2u?15-x:15-y;
        const int sy=turn==0u?y:turn==1u?15-x:turn==2u?15-y:x;
        const unsigned v=icon[sy*16+sx];
        if(v&&v!=7u)som_skin_dot(px,width,height,pitch,format,x0+x,y0+y,som_icon_steel(format,v));
    }
}
/* The cross: silver arms, golden rim, turning on its long axis. */
static inline void som_cross_draw(uint8_t *px,unsigned width,unsigned height,size_t pitch,int format,
                                  int cx,int cy,unsigned phase){
    static const int8_t wide[4]={6,4,1,4};
    const uint32_t gold=som_overlay_color(format,255,206,84),dark=som_overlay_color(format,148,96,24);
    const uint32_t silver=som_overlay_color(format,244,248,255);
    const int arm=wide[phase&3u],thick=arm>2?2:1;
    for(int y=-8;y<=7;y++)for(int x=-arm-1;x<=arm+1;x++){
        const int post=x>=-thick&&x<=thick-1,bar=y>=-4&&y<=-1&&x>=-arm&&x<=arm-1;
        if(!post&&!bar){
            const int near=(x>=-thick-1&&x<=thick)||(y>=-5&&y<=0&&x>=-arm-1&&x<=arm);
            if(near)som_skin_dot(px,width,height,pitch,format,cx+x,cy+y,dark);
            continue;
        }
        const int core=(x==-1||x==0)||(bar&&(y==-3||y==-2));
        som_skin_dot(px,width,height,pitch,format,cx+x,cy+y,core&&arm>1?silver:gold);
    }
}
/* This frame's thrown boomerangs with a look, in screen pixels, and their
 * sprite struck from the OAM shadow. look[hero] = SOM_SKIN_*. */
static inline unsigned som_throw_skins(uint8_t *ram,const uint8_t look[3],SomThrowSkin out[9]){
    unsigned n=0;
    for(unsigned who=0;who<3u;who++){
        if(!look[who])continue;
        for(unsigned k=0;k<3u;k++){
            const uint8_t *e=ram+0xD000u+0x40u*who+0x10u*k;
            const unsigned kind=e[0]&7u;
            if((kind!=3u&&kind!=5u)||(e[0]&0xF8u))continue;       /* not ours, or delayed */
            const unsigned cam=(e[1]&0x80u)?0xACu:0xA8u;
            const int x=(int16_t)((e[2]|e[3]<<8)-(ram[cam]|ram[cam+1u]<<8));
            const int y=(int16_t)((e[4]|e[5]<<8)-(ram[cam+2u]|ram[cam+3u]<<8));
            const unsigned tile=(unsigned)(e[14]|e[15]<<8)&0x1FFu;
            int best=-1,best_distance=SOM_THROW_MATCH_RADIUS*2+1;
            int draw_x=0,draw_y=0;
            for(unsigned i=0;i<128u;i++){
                uint8_t *o=ram+0x0800u+4u*i;
                if((unsigned)(o[2]|(o[3]&1u)<<8)!=tile||o[1]>=0xE0u)continue;
                /* The one-piece record carries directional offsets. Compare
                 * near the projectile, then use the actual OAM position for
                 * the replacement art rather than a side-facing constant. */
                const int dx=(int)(int8_t)(o[0]-(uint8_t)(x-8));
                const int dy=(int)(int8_t)(o[1]-(uint8_t)(y-4));
                const int distance=(dx<0?-dx:dx)+(dy<0?-dy:dy);
                if(dx<-SOM_THROW_MATCH_RADIUS||dx>SOM_THROW_MATCH_RADIUS||
                   dy<-SOM_THROW_MATCH_RADIUS||dy>SOM_THROW_MATCH_RADIUS||
                   distance>=best_distance)continue;
                best=(int)i;best_distance=distance;
                draw_x=x-8+dx;draw_y=y-4+dy;
            }
            if(best>=0&&n<9u){
                ram[0x0800u+4u*(unsigned)best+1u]=0xF0u;
                out[n].kind=look[who];out[n].x=draw_x+8;out[n].y=draw_y+8;n++;
            }
        }
    }
    return n;
}
#endif
