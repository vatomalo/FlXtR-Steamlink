#ifndef FLXTR_SOM_OVERLAY_H
#define FLXTR_SOM_OVERLAY_H
/* A message box drawn onto a copy of the emulator's video frame.
 * It never touches game RAM, ROM, or the core's own frame buffer. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "som_overlay_font.h"

#define SOM_OVERLAY_LINES 2u
#define SOM_OVERLAY_COLUMNS 32u

typedef struct {
    char lines[SOM_OVERLAY_LINES][SOM_OVERLAY_COLUMNS + 1u];
    unsigned line_count;
    unsigned frames_left;
    int speech_bubble; /* '^' prefix: separate non-modal dialogue style */
} SomOverlay;

/* Libretro pixel formats: 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565. */
static inline uint32_t som_overlay_color(int format, unsigned r, unsigned g, unsigned b){
    if(format==1)return (uint32_t)((r<<16)|(g<<8)|b);
    if(format==2)return (uint32_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
    return (uint32_t)(((r>>3)<<10)|((g>>3)<<5)|(b>>3));
}

/* '|' separates lines. Only printable ASCII is kept; the text is truncated to
 * two lines of 32 characters. Returns 1 when something is left to show. */
static inline int som_overlay_set(SomOverlay *overlay, const char *text, unsigned frames){
    memset(overlay,0,sizeof(*overlay));
    if(text&&text[0]=='^'){overlay->speech_bubble=1;text++;}
    unsigned line=0,column=0;
    for(const char *p=text;p&&*p&&*p!='\n'&&*p!='\r';p++){
        if(*p=='|'){
            if(++line>=SOM_OVERLAY_LINES)break;
            column=0;continue;
        }
        if(*p<32||*p>126||column>=SOM_OVERLAY_COLUMNS)continue;
        overlay->lines[line][column++]=*p;
    }
    for(unsigned i=0;i<SOM_OVERLAY_LINES;i++)
        if(overlay->lines[i][0])overlay->line_count=i+1;
    overlay->frames_left=overlay->line_count?frames:0;
    return overlay->line_count!=0;
}

static inline void som_overlay_put(uint8_t *pixels,size_t pitch,int format,
                                   unsigned x,unsigned y,uint32_t color){
    if(format==1)((uint32_t*)(void*)(pixels+y*pitch))[x]=color;
    else ((uint16_t*)(void*)(pixels+y*pitch))[x]=(uint16_t)color;
}

static inline void som_overlay_fill(uint8_t *pixels,size_t pitch,int format,unsigned x0,
                                    unsigned y0,unsigned w,unsigned h,uint32_t color){
    for(unsigned y=y0;y<y0+h;y++)
        for(unsigned x=x0;x<x0+w;x++)som_overlay_put(pixels,pitch,format,x,y,color);
}

/* Draw a bordered box near the bottom of a width x height frame. Hi-res and
 * interlaced SNES frames get the same apparent size via per-axis scaling. */
/* anchor_x/anchor_y: the hero's position in base SNES screen pixels
 * (256-wide), or negative when unknown. A speech bubble then sits with its
 * tail tip SOM_BUBBLE_HEAD_PX above that point and follows the hero. */
#define SOM_BUBBLE_HEAD_PX 30
static inline void som_overlay_draw(uint8_t *pixels,unsigned width,unsigned height,
                                    size_t pitch,int format,const SomOverlay *overlay,
                                    int anchor_x,int anchor_y){
    if(!overlay->line_count||!pixels)return;
    unsigned longest=0;
    for(unsigned i=0;i<overlay->line_count;i++){
        unsigned length=(unsigned)strlen(overlay->lines[i]);
        if(length>longest)longest=length;
    }
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const unsigned box_w=(longest*6u+9u)*sx,box_h=(overlay->line_count*10u+7u)*sy;
    if(longest==0||box_w+4u>width||box_h+8u*sy>height)return;
    unsigned x0=(width-box_w)/2u;
    /* Speech bubbles follow the hero when its screen position is known,
     * otherwise sit above the screen centre. Status messages keep the
     * bottom HUD box. */
    unsigned y0=overlay->speech_bubble?
        (height/2u>box_h+(6u+SOM_BUBBLE_HEAD_PX)*sy?
         height/2u-box_h-(6u+SOM_BUBBLE_HEAD_PX)*sy:2u*sy):
        height-box_h-8u*sy;
    if(overlay->speech_bubble&&anchor_x>=0&&anchor_y>=0){
        const long cx=(long)anchor_x*(long)sx;
        const long tip=((long)anchor_y-SOM_BUBBLE_HEAD_PX)*(long)sy;
        long left=cx-(long)box_w/2,top=tip-6L*(long)sy-(long)box_h;
        if(left<2L*(long)sx)left=2L*(long)sx;
        if(left+(long)box_w+2L*(long)sx>(long)width)left=(long)width-(long)box_w-2L*(long)sx;
        if(top<2L*(long)sy)top=2L*(long)sy;
        if(top+(long)box_h>(long)height)top=(long)height-(long)box_h;
        x0=(unsigned)left;y0=(unsigned)top;
    }
    const uint32_t border=overlay->speech_bubble?
        som_overlay_color(format,25,30,45):
        som_overlay_color(format,248,248,248);
    const uint32_t back=overlay->speech_bubble?
        som_overlay_color(format,248,248,240):
        som_overlay_color(format,16,24,96);
    const uint32_t ink=overlay->speech_bubble?
        som_overlay_color(format,16,24,45):
        som_overlay_color(format,255,255,255);
    som_overlay_fill(pixels,pitch,format,x0,y0,box_w,box_h,border);
    som_overlay_fill(pixels,pitch,format,x0+sx,y0+sy,box_w-2u*sx,box_h-2u*sy,back);
    if(overlay->speech_bubble && y0+box_h+6u*sy<height){
        /* Simple pixel-art speech tail pointing toward the player region. */
        const unsigned cx=x0+box_w/2u;
        for(unsigned row=0;row<6u*sy;row++){
            const unsigned half=((6u*sy-row)*sx)/(2u*sy);
            const unsigned x=cx>half?cx-half:0;
            const unsigned w=2u*half+1u;
            if(x+w<=width)
                som_overlay_fill(pixels,pitch,format,x,y0+box_h+row,w,1u,back);
        }
    }
    for(unsigned line=0;line<overlay->line_count;line++){
        const char *text=overlay->lines[line];
        for(unsigned i=0;text[i];i++){
            const uint8_t *glyph=som_overlay_font[(unsigned char)text[i]-32u];
            for(unsigned row=0;row<7u;row++)
                for(unsigned col=0;col<5u;col++){
                    if(!(glyph[row]&(0x10u>>col)))continue;
                    unsigned px=x0+(5u+i*6u+col)*sx,py=y0+(4u+line*10u+row)*sy;
                    som_overlay_fill(pixels,pitch,format,px,py,sx,sy,ink);
                }
        }
    }
}
#endif
