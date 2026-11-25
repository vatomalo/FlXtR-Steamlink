/* Camera: crisp 2x shots, limits, and text pixels that never move. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/som_camera.h"
static uint8_t ram[SOM_CAM_WRAM],vram[0x10000],mask[256*224];
static uint16_t px[256*224],copy[256*224],before[256*224];
static SomCamPixel text[256*224];
static void obj(unsigned base,int x,int y){
    uint8_t *o=ram+base;o[0]=1;o[2]=(uint8_t)x;o[3]=(uint8_t)(x>>8);o[4]=(uint8_t)y;o[5]=(uint8_t)(y>>8);
}
static void run(SomCamera *c,int n,int busy){while(n--)som_cam_frame(c,ram,0,busy);}
static void inside(const SomCamera *c){
    assert(c->cx-128/c->zoom>-.01f&&c->cx+128/c->zoom<256.01f);
    assert(c->cy-112/c->zoom>-.01f&&c->cy+112/c->zoom<224.01f);
}
/* settled: every 2x2 block is one game pixel, taken from a whole position */
static void crisp(const SomCamera *c){
    assert(c->zoom==2.0f);
    const int left=(int)(c->cx-64),top=(int)(c->cy-56);
    assert((float)left==c->cx-64&&(float)top==c->cy-56);
    for(int i=0;i<256*224;i++)px[i]=(uint16_t)(i*7);
    som_cam_scale(c,(uint8_t*)px,(uint8_t*)copy,512,2);
    for(int y=0;y<224;y++)for(int x=0;x<256;x++)
        assert(px[y*256+x]==(uint16_t)(((top+y/2)*256+left+x/2)*7));
}
int main(void){
    SomCamera c;som_cam_reset(&c);
    ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xDC]=52;
    obj(0xE000,20,200);                              /* near a corner */
    run(&c,100,0);assert(c.shot==SOM_SHOT_WIDE&&!som_cam_active(&c));
    run(&c,700,0);assert(c.shot==SOM_SHOT_IDLE);inside(&c);crisp(&c);
    obj(0xE000,22,200);run(&c,40,0);assert(!som_cam_active(&c)&&c.zoom==1.0f);
    run(&c,800,0);crisp(&c);
    run(&c,10,1);assert(!som_cam_active(&c));        /* our menu: out fast */
    run(&c,100,0);obj(0xE600,90,150);ram[0xE600+0x180]=14;ram[0xE600+0x182]=9;
    run(&c,40,0);assert(!som_cam_active(&c));        /* fights stay wide */
    ram[0xE600]=0;run(&c,100,0);crisp(&c);
    ram[0xDC]=53;run(&c,1,0);assert(!som_cam_active(&c));   /* new map: cut */
    /* a window: BG3 rows 1..7 filled, and a hidden leftover further down */
    for(unsigned k=1;k<8;k++)for(unsigned col=1;col<31;col++)vram[0xB000+2*(k*32+col)]=1;
    vram[0xB000+2*(20*32+5)]=2;vram[0x8000+2*16]=0x80;
    for(int i=0;i<16;i++)vram[0x8000+16+i]=0xFF;
    ram[0xD0]=0x83;obj(0xE000,128,100);obj(0xEA00,160,110);ram[0x3B]=0x0A;
    float last=1.0f;
    for(int f=0;f<200;f++){
        for(int i=0;i<256*224;i++)px[i]=(uint16_t)(i*7);
        som_cam_frame(&c,ram,0,0);
        unsigned n=som_cam_save(&c,ram,vram,NULL,(uint8_t*)px,512,2,mask,text,256*224);
        som_cam_clamp(&c);
        assert(c.window==63u&&n>=56u*240u);
        assert(c.zoom>=last&&c.zoom-last<.06f);last=c.zoom;        /* eased */
        memcpy(before,px,sizeof(px));
        som_cam_rub(mask,(uint8_t*)px,512,2);
        som_cam_scale(&c,(uint8_t*)px,(uint8_t*)copy,512,2);
        som_cam_restore((uint8_t*)px,2,text,n);
        for(int i=0;i<256*224;i++)if(mask[i])assert(px[i]==before[i]);
        inside(&c);
        assert((63-(c.cy-112/c.zoom))*c.zoom<63.5f);  /* nothing of it shows below */
    }
    assert(c.shot==SOM_SHOT_TALK&&!mask[159*256+40]);crisp(&c);
    for(int i=0;i<2;i++){                             /* both below the window */
        float x=((i?160:128)-c.cx)*2+128,y=((i?98:88)-c.cy)*2+112;
        assert(x>15&&x<241&&y>63&&y<216);
    }
    {   const float x0=c.cx,y0=c.cy;                  /* small steps: no movement */
        for(int i=0;i<90;i++){obj(0xE000,128+i%6,100);run(&c,1,0);assert(c.cx==x0&&c.cy==y0);}
    }
    obj(0xE000,10,100);run(&c,100,0);assert(!som_cam_active(&c));         /* does not fit */
    obj(0xE000,128,100);run(&c,100,0);crisp(&c);
    ram[0x1D00]=0x80;run(&c,10,0);assert(!som_cam_active(&c));            /* choice */
    ram[0x1D00]=0;ram[0x29]=7;run(&c,1,0);assert(!som_cam_active(&c));
    puts("PASS: crisp 2x shots, bounds and fixed text");
    return 0;
}
