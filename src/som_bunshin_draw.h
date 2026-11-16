/* Drawing for Kage Bunshin (src/som_bunshin.h), on the proxy's private copy
 * of the video frame; game RAM, VRAM and OAM are only read.
 *
 * A clone is the boy's current picture: his OAM pieces from the OAM shadow
 * 7E:0800 (tiles 0x1C0-0x1C7 / 0x1D0-0x1D7, palette 1, 16x16), decoded from
 * VRAM (name table 1 at word 0x7000, so tile 0x1C0 is word 0x7C00) with the
 * colours of 7E:0700 + palette * 32, the buffer the game sends to CGRAM
 * (cidwel/som-decomp docs/graphics.md). So clones copy his every pose, his
 * class colours included. Colour 0 is transparent. */
#ifndef FLXTR_SOM_BUNSHIN_DRAW_H
#define FLXTR_SOM_BUNSHIN_DRAW_H
#include <stddef.h>
#include <stdint.h>
#include "som_overlay.h"

#define SOM_BUN_VRAM_SIZE 0x10000u

static inline int som_bun_boy_piece(const uint8_t *e){
    const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
    return ((tile>=0x1C0u&&tile<=0x1C7u)||(tile>=0x1D0u&&tile<=0x1D7u))&&
           ((e[3]>>1)&7u)==1u&&e[1]<0xE0u;
}
/* Colour index (0..15) of pixel (px, py) of 8x8 4bpp tile `tile`. */
static inline unsigned som_bun_tile_pixel(const uint8_t *vram,unsigned tile,unsigned px,unsigned py){
    const unsigned word=(tile&0x100u?0x7000u:0x6000u)+(tile&0xFFu)*16u;
    const uint8_t *t=vram+((word*2u)&(SOM_BUN_VRAM_SIZE-1u));
    const unsigned bit=7u-px;
    return ((t[2u*py]>>bit)&1u)|(((t[2u*py+1u]>>bit)&1u)<<1)|
           (((t[16u+2u*py]>>bit)&1u)<<2)|(((t[16u+2u*py+1u]>>bit)&1u)<<3);
}
/* Draw a copy of the boy shifted by (dx, dy) base pixels. Returns the
 * number of pieces drawn. */
static inline unsigned som_bun_draw_copy(uint8_t *pixels,unsigned width,unsigned height,
                                         size_t pitch,int format,const uint8_t *ram,
                                         const uint8_t *vram,int dx,int dy){
    if(!pixels||!ram||!vram)return 0;
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const unsigned base_w=width/sx,base_h=height/sy;
    const uint8_t *oam=ram+0x0800u,*high=ram+0x0A00u;
    const uint8_t *palette=ram+0x0700u+32u;      /* OBJ palette 1 */
    uint32_t colours[16];
    for(unsigned i=1;i<16u;i++){
        const unsigned c=palette[2u*i]|((unsigned)palette[2u*i+1u]<<8);
        colours[i]=som_overlay_color(format,(c&31u)*255u/31u,((c>>5)&31u)*255u/31u,
                                     ((c>>10)&31u)*255u/31u);
    }
    unsigned drawn=0;
    for(unsigned i=0;i<128u;i++){
        const uint8_t *e=oam+4u*i;
        if(!som_bun_boy_piece(e))continue;
        const unsigned hb=(high[i/4u]>>(2u*(i%4u)))&3u;
        if(hb&2u)continue;                       /* hero pieces are small */
        int x=(int)(e[0]|((hb&1u)<<8));if(x>=256)x-=512;
        const int y=(int)e[1]+1;                 /* sprites show one line low */
        const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
        const int hflip=(e[3]&0x40u)!=0,vflip=(e[3]&0x80u)!=0;
        for(unsigned py=0;py<16u;py++)for(unsigned px=0;px<16u;px++){
            const unsigned sxp=hflip?15u-px:px,syp=vflip?15u-py:py;
            const unsigned t=(tile&0x100u)|((tile+(sxp>>3)+16u*(syp>>3))&0xFFu);
            const unsigned colour=som_bun_tile_pixel(vram,t,sxp&7u,syp&7u);
            if(!colour)continue;
            const int ox=x+dx+(int)px,oy=y+dy+(int)py;
            if(ox<0||oy<0||ox>=(int)base_w||oy>=(int)base_h)continue;
            som_overlay_fill(pixels,pitch,format,(unsigned)ox*sx,(unsigned)oy*sy,sx,sy,
                             colours[colour]);
        }
        drawn++;
    }
    return drawn;
}
/* Kawarimi's log: the game's whip post (the tree stump the whip catches
 * to cross a ledge), copied from VRAM on map 30 (2026-11-16: BG1 tile
 * A1, characters 0x300/0x301/0x310/0x311, BG palette 1). Drawn 16x16 with
 * its feet on base pixel (cx, cy), the hero's feet. Digits are colours
 * 1-6 of that palette; '.' (the grass around it) is transparent. */
static const char *const som_bun_log_art[16]={
    "......555.......",
    ".....54445......",
    "....4333345.....",
    "....5133326.....",
    "....4511256.....",
    "....4434556.....",
    "....4434546.....",
    "....4534546.....",
    "....4545546.....",
    "....4545546.....",
    "....4445556.....",
    "....5444556555..",
    "...554445566665.",
    "...5643455666665",
    "....56334666665.",
    ".....566666665.."
};
static const uint8_t som_bun_log_rgb[7][3]={
    {0,0,0},{98,115,90},{82,115,82},{74,90,49},{49,90,57},{41,49,24},{16,49,24}
};
static inline void som_bun_draw_log(uint8_t *pixels,unsigned width,unsigned height,
                                    size_t pitch,int format,int cx,int cy){
    if(!pixels)return;
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const int base_w=(int)(width/sx),base_h=(int)(height/sy);
    uint32_t colours[7];
    for(unsigned i=1;i<7u;i++)
        colours[i]=som_overlay_color(format,som_bun_log_rgb[i][0],som_bun_log_rgb[i][1],
                                     som_bun_log_rgb[i][2]);
    for(int row=0;row<16;row++)for(int col=0;col<16;col++){
        const char c=som_bun_log_art[row][col];
        if(c<'1'||c>'6')continue;
        const int x=cx-8+col,y=cy-13+row;
        if(x<0||y<0||x>=base_w||y>=base_h)continue;
        som_overlay_fill(pixels,pitch,format,(unsigned)x*sx,(unsigned)y*sy,sx,sy,
                         colours[c-'0']);
    }
}
/* An explosion (or a smoke puff) centred on base pixel (cx, cy), `age`
 * frames old: a hot core that grows, then a thinning ring of smoke. */
static inline void som_bun_draw_boom(uint8_t *pixels,unsigned width,unsigned height,
                                     size_t pitch,int format,int cx,int cy,
                                     unsigned age,int smoke){
    if(!pixels||age>=28u)return;
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const int base_w=(int)(width/sx),base_h=(int)(height/sy);
    const int fire=!smoke&&age<14u;
    const int radius=smoke?6+(int)age/2:fire?4+(int)age:12+(int)age/2;
    const uint32_t white=som_overlay_color(format,255,250,220);
    const uint32_t yellow=som_overlay_color(format,255,210,60);
    const uint32_t orange=som_overlay_color(format,240,110,30);
    const uint32_t grey=som_overlay_color(format,200,200,205);
    const uint32_t dark=som_overlay_color(format,120,120,130);
    for(int oy=-radius;oy<=radius;oy++)for(int ox=-radius;ox<=radius;ox++){
        const int d2=ox*ox+oy*oy;
        if(d2>radius*radius)continue;
        const int x=cx+ox,y=cy+oy;
        if(x<0||y<0||x>=base_w||y>=base_h)continue;
        uint32_t colour;
        if(fire){
            if(d2<(radius*radius)/6)colour=white;
            else if(d2<(radius*radius)/2)colour=yellow;
            else colour=((x+y)&1)?orange:yellow;
        }else{
            /* Smoke thins out: fewer dots as it ages, a hollow centre. */
            const unsigned keep=(unsigned)((x*7+y*13+(int)age)&7);
            if(d2<(radius*radius)/4&&!smoke)continue;
            if(keep<age/4u)continue;
            colour=((x^y)&2)?grey:dark;
        }
        som_overlay_fill(pixels,pitch,format,(unsigned)x*sx,(unsigned)y*sy,sx,sy,colour);
    }
}
#endif
