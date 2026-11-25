/* Camera: crisp 2x shots, limits, and text pixels that never move. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/som_camera.h"
static uint8_t ram[SOM_CAM_WRAM],vram[0x10000],mask[256*224];
static uint16_t px[256*224],copy[256*224],before[256*224];
static int hd_ok;
static SomCamPixel text[256*224];
static void obj(unsigned base,int x,int y){
    uint8_t *o=ram+base;o[0]=1;o[2]=(uint8_t)x;o[3]=(uint8_t)(x>>8);o[4]=(uint8_t)y;o[5]=(uint8_t)(y>>8);
}
static void run(SomCamera *c,int n,int busy){while(n--)som_cam_frame(c,ram,0,busy,hd_ok);}
static void inside(const SomCamera *c){
    assert(c->cx-128/c->zoom>-.01f&&c->cx+128/c->zoom<256.01f);
    assert(c->cy-112/c->zoom>-.01f&&c->cy+112/c->zoom<224.01f);
}
/* settled: every 2x2 block is one game pixel, taken from a whole position */
static void crisp(const SomCamera *c){
    assert(c->zoom==2.0f);
    const int sx=som_cam_start(c,c->cx,128,1),sy=som_cam_start(c,c->cy,112,1);
    const int left2=sx>>15,top2=sy>>15;
    assert(!(sx&0x7FFF)&&!(sy&0x7FFF));
    for(int i=0;i<256*224;i++)px[i]=(uint16_t)(i*7);
    som_cam_scale(c,(uint8_t*)px,(uint8_t*)copy,512,2);
    for(int y=0;y<224;y++)for(int x=0;x<256;x++)
        assert(px[y*256+x]==(uint16_t)((((top2+y)/2)*256+(left2+x)/2)*7));
}
/* settled 1.5x: whole 3x3 blocks in 512x448 */
static uint16_t big[512*448];
static void crisp_hd(const SomCamera *c){
    assert(c->hd&&c->zoom==1.5f);
    for(int i=0;i<256*224;i++)px[i]=(uint16_t)i;
    som_cam_scale_hd(c,(uint8_t*)px,512,(uint8_t*)big,2);
    int run=1,edges=0;
    for(int x=1;x<512;x++){
        if(big[x]==big[x-1])run++;
        else{assert(edges++==0?run<=3:run==3);assert(big[x]==big[x-1]+1);run=1;}
    }
    run=1;edges=0;
    for(int y=1;y<448;y++){
        if(big[y*512]==big[(y-1)*512])run++;
        else{assert(edges++==0?run<=3:run==3);assert(big[y*512]==big[(y-1)*512]+256);run=1;}
    }
}
int main(void){
    SomCamera c;som_cam_reset(&c);
    ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xDC]=52;
    obj(0xE000,20,200);                              /* near a corner */
    run(&c,100,0);assert(c.shot==SOM_SHOT_WIDE&&!som_cam_active(&c));
    run(&c,700,0);assert(c.shot==SOM_SHOT_IDLE);inside(&c);crisp(&c);
    obj(0xE000,22,200);run(&c,120,0);assert(!som_cam_active(&c)&&c.zoom==1.0f);
    run(&c,800,0);crisp(&c);
    run(&c,30,1);assert(!som_cam_active(&c));        /* our menu: out fast */
    run(&c,100,0);obj(0xE600,90,150);ram[0xE600+0x180]=14;ram[0xE600+0x182]=9;
    run(&c,200,0);assert(!som_cam_active(&c));        /* fights stay wide */
    ram[0xE600]=0;run(&c,300,0);crisp(&c);
    ram[0xDC]=53;run(&c,1,0);assert(!som_cam_active(&c));   /* new map: cut */
    /* a window: BG3 rows 1..7 filled, and a hidden leftover further down */
    for(unsigned k=1;k<8;k++)for(unsigned col=1;col<31;col++)vram[0xB000+2*(k*32+col)]=1;
    vram[0xB000+2*(20*32+5)]=2;vram[0x8000+2*16]=0x80;
    for(int i=0;i<16;i++)vram[0x8000+16+i]=0xFF;
    ram[0xD0]=0x83;obj(0xE000,128,100);obj(0xEA00,160,110);ram[0x3B]=0x0A;
    float last=1.0f;
    for(int f=0;f<200;f++){
        for(int i=0;i<256*224;i++)px[i]=(uint16_t)(i*7);
        som_cam_frame(&c,ram,0,0,hd_ok);
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
    obj(0xE000,10,100);run(&c,200,0);assert(!som_cam_active(&c));         /* does not fit */
    obj(0xE000,128,100);run(&c,250,0);crisp(&c);
    ram[0x1D00]=0x80;run(&c,30,0);assert(!som_cam_active(&c));            /* choice */
    ram[0x1D00]=0;ram[0x29]=7;run(&c,1,0);assert(!som_cam_active(&c));
    {   /* a HUD portrait keeps all of its 16x16 pixels */
        static uint8_t oam[0x220];
        memset(vram,0,sizeof(vram));ram[0xD0]=0;ram[0x29]=9;
        oam[0]=88;oam[1]=173;oam[2]=0xEA;oam[3]=1;
        (void)som_cam_save(&c,ram,vram,oam,(uint8_t*)px,512,2,mask,text,256*224);
        for(int y=0;y<224;y++)for(int x=0;x<256;x++)
            assert(mask[y*256+x]==(x>=88&&x<104&&y>=174&&y<190));
    }
    /* with hd: fights at a crisp 1.5x, everybody in the picture */
    hd_ok=1;ram[0x3B]=0;obj(0xE000,100,120);obj(0xE200,130,110);obj(0xE600,90,130);run(&c,300,0);
    assert(c.shot==SOM_SHOT_FIGHT);crisp_hd(&c);inside(&c);
    for(int i=0;i<3;i++){
        float x=((i==0?100:i==1?130:90)-c.cx)*1.5f+128,y=((i==0?108:i==1?98:118)-c.cy)*1.5f+112;
        assert(x>=8&&x<248&&y>=4&&y<SOM_CAM_HUD_TOP);
    }
    obj(0xE200,130,108);run(&c,200,0);crisp_hd(&c);
    obj(0xE200,250,40);run(&c,200,0);assert(!som_cam_active(&c)&&!c.hd);  /* too far apart */
    obj(0xE200,130,108);run(&c,300,0);assert(c.hd);hd_ok=0;run(&c,200,0);
    assert(!c.hd&&!som_cam_active(&c));               /* governor */
    {   /* whacked: in fast, crisp, over, and back */
        ram[0xE600]=0;obj(0xE600,120,110);run(&c,200,0);som_cam_whack(&c,ram+0xE600);ram[0xE600]=0;
        run(&c,40,0);assert(c.shot==SOM_SHOT_WHACK);crisp(&c);
        obj(0xE000,101,130);run(&c,150,0);assert(c.shot!=SOM_SHOT_WHACK&&!som_cam_active(&c));
    }
    /* text is 2x2 in hd */
    text[0].at=10*512+2*20;text[0].ink=0xABCD;memset(big,0,sizeof(big));
    som_cam_restore_hd((uint8_t*)big,512,2,text,1);
    for(int y=0;y<448;y++)for(int x=0;x<512;x++)
        assert(big[y*512+x]==(y/2==10&&x/2==20?0xABCD:0));
    SomCamGovern g;memset(&g,0,sizeof(g));
    for(int i=0;i<200;i++)assert(!som_cam_govern(&g,0,17));
    for(int i=0;i<200;i++)assert(!som_cam_govern(&g,1,19));
    int fired=0;
    for(int i=0;i<400;i++)fired+=som_cam_govern(&g,1,23);
    assert(fired==1&&g.off);
    {   /* a lifted field message box goes back to its place */
        memset(vram,0,sizeof(vram));ram[0xD0]=0;ram[0x40]=26;
        assert(!som_cam_box_up(ram,vram));
        for(int col=1;col<31;col++)vram[0xB000+2*(2*32+col)]=1;
        for(int i=0;i<16;i+=2)vram[0x8010+i]=0xFF;        /* ink 1 */
        ram[0x602]=0x1F;ram[0x603]=0;                     /* red */
        assert(som_cam_box_up(ram,vram));
        ram[0xD0]=0x83;assert(!som_cam_box_up(ram,vram));ram[0xD0]=0;
        memset(px,0,sizeof(px));som_cam_box_draw(ram,vram,(uint8_t*)px,512,2,1);
        for(int y=0;y<224;y++)for(int x=0;x<256;x++)
            assert(px[y*256+x]==(y>=15&&y<23&&x>=8&&x<248?0xF800:0));
    }
    puts("PASS: crisp 2x shots, bounds and fixed text");
    return 0;
}
