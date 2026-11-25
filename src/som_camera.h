#ifndef FLXTR_SOM_CAMERA_H
#define FLXTR_SOM_CAMERA_H
/* Automatic camera: zooms, pans and zooms out on the finished picture.
 * Reads WRAM and VRAM only; it never writes game memory and never presses
 * a button. The HUD, dialogue windows and ring titles are the game's BG3
 * layer: their exact pixels are saved before the world is scaled and put
 * back after it, so text is never scaled, cropped or panned off screen.
 *
 * Crisp pixels: a settled shot is exactly 1x or 2x, its view starts on a
 * whole game pixel; only the short ramp between passes other sizes.
 * Shots: TALK = speaker and lead below the window, when both fit at 2x;
 * IDLE = the lead after 10 s standing still; everything else, fights
 * included, is WIDE (1x). Menus and choices ramp out fast; fades, mode 7
 * and map changes cut. */
#include <stdint.h>
#include <string.h>
#define SOM_CAM_W 256
#define SOM_CAM_H 224
#define SOM_CAM_HUD_TOP 160          /* field: subjects stay above the HUD */
#define SOM_CAM_IDLE_FRAMES 600u
#define SOM_CAM_RAMP (1.0f/28.0f)     /* zoom travel per frame */
#define SOM_CAM_RAMP_OUT (1.0f/8.0f)
#define SOM_CAM_DEAD 18.0f           /* px the aim may drift before the view follows */
#define SOM_CAM_TALK_HOLD 45u
#define SOM_CAM_WRAM 0x20000u
enum {SOM_SHOT_WIDE,SOM_SHOT_TALK,SOM_SHOT_IDLE,SOM_SHOT_COUNT};
static const char *const som_shot_names[SOM_SHOT_COUNT]={"wide","talk","idle"};
typedef struct {
    float zoom,cx,cy;                /* view centre, screen pixels */
    float want_zoom,want_cx,want_cy;
    float aim_zoom,aim_cx,aim_cy,at_x,at_y,vx,vy,ramp;   /* held aim, free view centre, speeds */
    int shot,hero_x,hero_y;
    unsigned map,still,talked,window;      /* window = screen row below the dialogue window, 0 none */
} SomCamera;
typedef struct {uint32_t at,ink;} SomCamPixel;

static inline void som_cam_reset(SomCamera *c){
    memset(c,0,sizeof(*c));
    c->zoom=c->want_zoom=1.0f;c->cx=c->want_cx=128.0f;c->cy=c->want_cy=112.0f;
    c->aim_zoom=1.0f;c->aim_cx=128.0f;c->aim_cy=112.0f;
}
static inline unsigned som_cam_u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
/* An object's body centre on the screen; 0 when it is not visible. */
static inline int som_cam_object(const uint8_t *ram,unsigned base,int *x,int *y){
    const uint8_t *o=ram+base;
    const unsigned cam=(o[0x0B]&0x80u)?0xACu:0xA8u;
    if(o[0]!=1)return 0;
    *x=(int16_t)(som_cam_u16(o+2)-som_cam_u16(ram+cam));
    *y=(int16_t)(som_cam_u16(o+4)-som_cam_u16(ram+cam+2u))-12;
    return *x>=0&&*x<SOM_CAM_W&&*y>=0&&*y<SOM_CAM_H;
}
typedef struct {int x0,y0,x1,y1,n;} SomCamBox;
static inline void som_cam_add(SomCamBox *b,int x,int y){
    if(!b->n||x<b->x0)b->x0=x;
    if(!b->n||x>b->x1)b->x1=x;
    if(!b->n||y<b->y0)b->y0=y;
    if(!b->n||y>b->y1)b->y1=y;
    b->n++;
}
/* Aim at the box: 2x if it fits with room around it, inside `area`
 * screen rows top..bottom of the zoomed picture. */
static inline void som_cam_aim(SomCamera *c,const SomCamBox *b,int margin,int top,int bottom){
    const float zx=(float)SOM_CAM_W/(float)(b->x1-b->x0+2*margin);
    const float zy=(float)(bottom-top)/(float)(b->y1-b->y0+2*margin);
    float z=zx<zy?zx:zy;
    c->want_cx=(float)(b->x0+b->x1)/2.0f;
    c->want_zoom=z>=2.0f?2.0f:1.0f;z=2.0f;
    /* the box's middle goes to the area's middle */
    c->want_cy=(float)(b->y0+b->y1)/2.0f-((float)(top+bottom)/2.0f-112.0f)/z;
}
static inline void som_cam_clamp(SomCamera *c){
    const float hw=128.0f/c->zoom,hh=112.0f/c->zoom;
    if(c->cx<hw)c->cx=hw;
    if(c->cx>256.0f-hw)c->cx=256.0f-hw;
    if(c->cy<hh)c->cy=hh;
    if(c->cy>224.0f-hh)c->cy=224.0f-hh;
    /* the world behind the window stays behind it */
    const float low=(float)c->window*(1.0f-1.0f/c->zoom)+hh;
    if(c->window&&c->cy<low)c->cy=low;
}
/* One frame. `lead` = the hero being followed; `busy` = our own menus. */
static inline int som_cam_frame(SomCamera *c,const uint8_t *ram,unsigned lead,int busy){
    const unsigned map=som_cam_u16(ram+0xDC);
    /* 8F = a palette fade; E4 2/4 = transitions and mode 7 */
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||(ram[0x29]&7u)==7u||map!=c->map){
        som_cam_reset(c);c->map=map;
        return SOM_SHOT_WIDE;
    }
    int hx=0,hy=0,x,y,shot=SOM_SHOT_WIDE;
    const int seen=lead<3u&&som_cam_object(ram,0xE000u+0x200u*lead,&hx,&hy);
    const uint8_t *hero=ram+0xE000u+0x200u*(lead<3u?lead:0u);
    const int wx=(int)som_cam_u16(hero+2),wy=(int)som_cam_u16(hero+4);
    c->still=(wx==c->hero_x&&wy==c->hero_y)?c->still+1u:0u;
    c->hero_x=wx;c->hero_y=wy;
    const int choice=ram[0xA170]==0x5A||ram[0xA170]==0x5F||(ram[0x1D00]&0x80u);
    SomCamBox box;memset(&box,0,sizeof(box));
    c->want_zoom=1.0f;
    if(seen&&!busy&&ram[0x7D]!=1){
        som_cam_add(&box,hx,hy);
        if(ram[0xD0]&0x80u){
            if(!choice&&c->window){
                const unsigned who=som_cam_u16(ram+0x3A);
                if(who>=0x600u&&who<0x1800u&&!(who&0x1FFu)&&som_cam_object(ram,0xE000u+who,&x,&y))
                    som_cam_add(&box,x,y);
                som_cam_aim(c,&box,16,72,216);
                if(c->want_zoom>1.0f)shot=SOM_SHOT_TALK;
            }
        }else if(ram[0xD0]==0){
            int monsters=0;
            for(unsigned i=0;i<3u;i++){
                const uint8_t *m=ram+0xE600u+0x200u*i;
                monsters+=m[0x180]<0x80u&&som_cam_u16(m+0x182)&&som_cam_object(ram,0xE600u+0x200u*i,&x,&y);
            }
            if(!monsters&&c->still>=SOM_CAM_IDLE_FRAMES){
                som_cam_aim(c,&box,16,8,SOM_CAM_HUD_TOP);shot=SOM_SHOT_IDLE;
            }
        }
    }
    /* Windows close and reopen between lines: hold the shot meanwhile. */
    if(shot==SOM_SHOT_TALK)c->talked=SOM_CAM_TALK_HOLD;
    else if(c->talked){
        c->talked--;
        if(seen&&!busy&&!choice&&ram[0x7D]!=1&&shot==SOM_SHOT_WIDE){
            som_cam_aim(c,&box,16,72,216);
            if(c->want_zoom>1.0f)shot=SOM_SHOT_TALK;
        }else c->talked=0;
    }
    /* The aim moves only when it must: a new shot or a drift past the dead zone. */
    const float dx=c->want_cx-c->aim_cx,dy=c->want_cy-c->aim_cy;
    if(shot!=SOM_SHOT_WIDE){
        const int fresh=shot!=c->shot;
        if(fresh||dx>SOM_CAM_DEAD||dx<-SOM_CAM_DEAD)c->aim_cx=c->want_cx;
        if(fresh||dy>SOM_CAM_DEAD||dy<-SOM_CAM_DEAD)c->aim_cy=c->want_cy;
        SomCamera t=*c;                  /* only where a 2x view can go */
        t.zoom=2.0f;t.cx=c->aim_cx;t.cy=c->aim_cy;
        som_cam_clamp(&t);c->aim_cx=t.cx;c->aim_cy=t.cy;
        if(c->ramp<=0.0f){c->at_x=c->aim_cx;c->at_y=c->aim_cy;c->vx=c->vy=0.0f;}
        c->ramp+=SOM_CAM_RAMP;
        if(c->ramp>1.0f)c->ramp=1.0f;
    }else{
        c->ramp-=busy||choice||ram[0x7D]==1?SOM_CAM_RAMP_OUT:SOM_CAM_RAMP;
        if(c->ramp<0.0f)c->ramp=0.0f;
    }
    /* Pan: critically damped, soft start and stop. */
    const float wp=0.055f;
    c->vx+=(c->aim_cx-c->at_x)*wp*wp-2.0f*wp*c->vx;c->at_x+=c->vx;
    c->vy+=(c->aim_cy-c->at_y)*wp*wp-2.0f*wp*c->vy;c->at_y+=c->vy;
    c->zoom=1.0f+c->ramp*c->ramp*(3.0f-2.0f*c->ramp);
    c->cx=c->at_x;c->cy=c->at_y;
    if(c->ramp<=0.0f){c->zoom=1.0f;c->cx=128.0f;c->cy=112.0f;}
    som_cam_clamp(c);
    if(c->ramp>=1.0f){                   /* settled: whole game pixels */
        c->cx=(float)(int)(c->cx+0.5f);c->cy=(float)(int)(c->cy+0.5f);
        if(c->cx>192.0f)c->cx=192.0f;
        if(c->cy>168.0f)c->cy=168.0f;
    }
    c->shot=shot;
    return shot;
}
static inline int som_cam_active(const SomCamera *c){return c->zoom>1.004f;}
/* Where an unzoomed screen point is now (speech bubbles follow the hero). */
static inline void som_cam_point(const SomCamera *c,int *x,int *y){
    if(*x<0||*y<0)return;
    *x=(int)(((float)*x-c->cx)*c->zoom+128.0f);
    *y=(int)(((float)*y-c->cy)*c->zoom+112.0f);
    if(*x<0)*x=0;
    if(*x>255)*x=255;
    if(*y<0)*y=0;
    if(*y>223)*y=223;
}
static inline uint32_t som_cam_get(const uint8_t *px,uint32_t at,unsigned bytes){
    uint32_t v=0;memcpy(&v,px+at,bytes);return v;
}
/* Save the BG3 pixels (and the HUD portraits) of a 256x224 picture.
 * BG3: 32x32 map at VRAM B000/B800 (7E:00E8 bit 0), 2bpp tiles at 8000,
 * vertical scroll 7E:0040, screen line y shows BG line y+1 (checked against
 * the rendered HUD: every pixel matches). */
static inline unsigned som_cam_save(SomCamera *c,const uint8_t *ram,const uint8_t *vram,const uint8_t *oam,
                                    const uint8_t *px,size_t pitch,unsigned bytes,
                                    uint8_t *mask,SomCamPixel *out,unsigned cap){
    const unsigned tm=0xB000u+((ram[0xE8]&1u)?0x800u:0u),scroll=som_cam_u16(ram+0x40);
    unsigned n=0;
    c->window=0;
    memset(mask,0,SOM_CAM_W*SOM_CAM_H);
    for(unsigned y=0;y<SOM_CAM_H;y++){
        /* BG3 also holds leftovers the game does not show (rows 13-15): only
         * the window's place, or the HUD band and the spell banner, count. */
        if((ram[0xD0]&0x80u)?y>=80u:y>=40u&&y<150u)continue;
        const unsigned v=(y+1u+scroll)&0xFFu;
        for(unsigned col=0;col<32u;col++){
            const unsigned w=som_cam_u16(vram+tm+2u*((v>>3)*32u+col));
            if(!w)continue;
            const unsigned ty=(w&0x8000u)?7u-(v&7u):v&7u;
            const unsigned a=(0x8000u+(w&0x3FFu)*16u+ty*2u)&0xFFFFu;
            const unsigned bits=vram[a]|vram[a+1u];
            for(unsigned k=0;bits&&k<8u;k++)
                if(bits>>((w&0x4000u)?k:7u-k)&1u)mask[y*SOM_CAM_W+col*8u+k]=1;
        }
        /* a window's rows are kept whole, with its 8 px margins */
        unsigned filled=0;
        if(ram[0xD0]&0x80u)
            for(unsigned x=0;x<SOM_CAM_W;x++)filled+=mask[y*SOM_CAM_W+x];
        if(filled>=160u){memset(mask+y*SOM_CAM_W,1,SOM_CAM_W);c->window=y+1u;}
    }
    if(oam&&ram[0xD0]==0)for(unsigned i=0;i<128u;i++){
        const uint8_t *e=oam+4u*i;
        const unsigned tile=e[2]|(unsigned)(e[3]&1u)<<8;
        const unsigned hi=oam[0x200u+i/4u]>>(2u*(i%4u))&3u,size=(hi&2u)?16u:8u;
        if(tile<0x1E0u||e[1]<SOM_CAM_HUD_TOP||e[1]>=0xE0u||(hi&1u))continue;
        for(unsigned y=e[1]+1u;y<e[1]+1u+size&&y<SOM_CAM_H;y++)
            for(unsigned x=e[0];x<e[0]+size&&x<SOM_CAM_W;x++)mask[y*SOM_CAM_W+x]=1;
    }
    if(c->window)memset(mask,1,c->window*SOM_CAM_W);   /* and what is above it */
    for(unsigned y=0;y<SOM_CAM_H;y++){
        const uint8_t *m=mask+y*SOM_CAM_W;
        if(!memchr(m,1,SOM_CAM_W))continue;
        for(unsigned x=0;x<SOM_CAM_W;x++)if(m[x]&&n<cap){
            out[n].at=(uint32_t)(y*pitch+x*bytes);
            out[n].ink=som_cam_get(px,out[n].at,bytes);n++;
        }
    }
    return n;
}
/* Scale the picture about the view; at a settled 2x every game pixel is
 * exactly 2x2 and the view starts on a whole pixel (som_cam_frame). `copy` is scratch of the same layout. */
static inline void som_cam_scale(const SomCamera *c,uint8_t *px,uint8_t *copy,size_t pitch,
                                 unsigned bytes){
    const int32_t step=(int32_t)(65536.0f/c->zoom);
    int32_t sx=(int32_t)((c->cx-128.0f/c->zoom)*65536.0f);
    int32_t sy=(int32_t)((c->cy-112.0f/c->zoom)*65536.0f);
    uint8_t cols[SOM_CAM_W];
    for(unsigned x=0;x<SOM_CAM_W;x++,sx+=step){
        const int32_t col=sx>>16;
        cols[x]=(uint8_t)(col<0?0:col>255?255:col);
    }
    memcpy(copy,px,pitch*SOM_CAM_H);
    int last=-1;
    for(unsigned y=0;y<SOM_CAM_H;y++,sy+=step){
        int row=(int)(sy>>16);
        if(row<0)row=0;
        if(row>SOM_CAM_H-1)row=SOM_CAM_H-1;
        uint8_t *dst=px+(size_t)y*pitch;
        if(row==last){memcpy(dst,dst-pitch,SOM_CAM_W*bytes);continue;}
        last=row;
        if(bytes==2u){
            const uint16_t *a=(const uint16_t*)(const void*)(copy+(size_t)row*pitch);
            uint16_t *b=(uint16_t*)(void*)dst;
            for(unsigned x=0;x<SOM_CAM_W;x++)b[x]=a[cols[x]];
        }else{
            const uint32_t *a=(const uint32_t*)(const void*)(copy+(size_t)row*pitch);
            uint32_t *b=(uint32_t*)(void*)dst;
            for(unsigned x=0;x<SOM_CAM_W;x++)b[x]=a[cols[x]];
        }
    }
}
/* Put the saved text back, exactly where the game drew it. */
static inline void som_cam_restore(uint8_t *px,unsigned bytes,const SomCamPixel *in,unsigned n){
    for(unsigned i=0;i<n;i++)memcpy(px+in[i].at,&in[i].ink,bytes);
}
/* Before scaling: rub the text out of the world, or its scaled ghost shows
 * next to the real one. Each masked pixel takes the colour above it. */
static inline void som_cam_rub(const uint8_t *mask,uint8_t *px,size_t pitch,unsigned bytes){
    for(unsigned y=0;y<SOM_CAM_H;y++)if(memchr(mask+y*SOM_CAM_W,1,SOM_CAM_W))
    for(unsigned x=0;x<SOM_CAM_W;x++){
        if(!mask[y*SOM_CAM_W+x])continue;
        unsigned from=y;
        if(y>0)from=y-1u;                       /* already clean: rows go down */
        else while(from<SOM_CAM_H-1u&&mask[from*SOM_CAM_W+x])from++;
        memcpy(px+(size_t)y*pitch+x*bytes,px+(size_t)from*pitch+x*bytes,bytes);
    }
}
#endif
