/* Combo ladder, shadow clone model and clone drawing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_combo.h"
#include "../src/som_bunshin.h"
#include "../src/som_bunshin_draw.h"

static uint8_t ram[0x20000];
static uint8_t vram[SOM_BUN_VRAM_SIZE];
static uint16_t frame[224][256];

static void hit(SomCombo *c,unsigned which,unsigned long *f){
    *f+=30;som_combo_on_swing(c,which,*f);*f+=5;assert(som_combo_on_hit(c,*f));
}
int main(void){
    /* Ladder: 3 hits = level 1, then +1 every 2 hits. */
    SomCombo c;som_combo_reset(&c);
    unsigned long f=1000;
    char text[24];som_combo_text(&c,text);assert(!text[0]);
    hit(&c,0,&f);hit(&c,0,&f);assert(som_combo_level(&c)==0);
    som_combo_text(&c,text);assert(!strcmp(text,"2 HIT"));
    hit(&c,0,&f);assert(som_combo_level(&c)==1);
    hit(&c,0,&f);assert(som_combo_level(&c)==1);
    hit(&c,0,&f);assert(som_combo_level(&c)==2);
    /* Mixing weapons: +1 per switch, at most +2. */
    hit(&c,1,&f);hit(&c,0,&f);hit(&c,1,&f);
    assert(c.hits==8&&c.mixes==3&&som_combo_level(&c)==3+2);
    som_combo_text(&c,text);assert(!strcmp(text,"8 HIT MIX LV5"));
    for(unsigned i=0;i<40;i++)hit(&c,0,&f);
    assert(som_combo_level(&c)==SOM_COMBO_LEVEL_MAX);
    /* A hit without a swing does not count; a whiff or a pause drops it. */
    som_combo_reset(&c);assert(!som_combo_on_hit(&c,f));
    hit(&c,0,&f);
    som_combo_on_swing(&c,0,f);
    assert(!som_combo_tick(&c,f+SOM_COMBO_HIT_WINDOW,0));
    assert(som_combo_tick(&c,f+SOM_COMBO_HIT_WINDOW+1,0)&&!c.hits);
    hit(&c,0,&f);
    assert(!som_combo_tick(&c,f+SOM_COMBO_GAP+50,1));     /* held: kept */
    assert(som_combo_tick(&c,f+SOM_COMBO_GAP+1,0)&&!c.hits);
    hit(&c,0,&f);f+=SOM_COMBO_GAP+1;som_combo_on_swing(&c,0,f);
    assert(c.hits==0&&c.pending);                          /* late swing: new chain */

    /* Clones: open ground, walls, follow, touch, blast, pick. */
    ram[0xC0]=0x00;ram[0xC1]=0x02;ram[0xC2]=0x00;ram[0xC3]=0x02;   /* 512 x 512 */
    ram[0xDC]=5;
    assert(som_bun_spot_open(ram,100,100,0));
    assert(!som_bun_spot_open(ram,4,100,0)&&!som_bun_spot_open(ram,100,509,0));
    ram[0x1B800+4*3]=0x03;                                 /* tile 3: wall */
    ram[0x10000+(100/16)*128+(124/16)]=3;                  /* wall at x 112..127 */
    assert(!som_bun_spot_open(ram,118,100,0)&&!som_bun_spot_open(ram,107,100,0));
    assert(som_bun_spot_open(ram,105,100,0));              /* box edge x+6 = 111 */
    ram[0x1B800+4*4+2]=0x15;                               /* half tile: refused */
    ram[0x1B800+4*5]=0x08;                                 /* layer switch: refused */
    ram[0x10000+(100/16)*128+(60/16)]=4;
    ram[0x10000+(100/16)*128+(40/16)]=5;
    assert(!som_bun_tile_open(ram,60,100,0)&&!som_bun_tile_open(ram,40,100,0));
    assert(som_bun_tile_open(ram,60,100,0x80));            /* layer 2 is other data */
    ram[0x1B800+4*6]=0x01;ram[0x10000+(100/16)*128+(80/16)]=6;    /* floor-1 tile */
    assert(som_bun_tile_open(ram,80,100,0)&&som_bun_tile_open(ram,80,100,1));
    assert(!som_bun_tile_open(ram,80,100,2));              /* hero on floor 2 */
    memset(ram+0x10000,0,0x10000);memset(ram+0x1B800,0,0x800);

    SomBunshin b;
    assert(som_bun_spawn(&b,ram,200,200,0x01,0,50)==2);    /* facing down */
    assert(b.x[0]==176&&b.y[0]==200&&b.x[1]==224&&b.until==50+SOM_BUN_FRAMES&&b.map==5);
    ram[0x1B800+4*3]=0x03;
    for(unsigned r=0;r<32;r++)ram[0x10000+r*128+(240/16)]=3;   /* wall x 240..255 */
    for(int s=0;s<12;s++)som_bun_follow(&b,ram,200+2*(s+1),200,0);
    assert(b.x[0]==200&&b.x[1]<240-6);                     /* stopped by the wall */
    const int stuck=b.x[1];
    som_bun_follow(&b,ram,224,202,0);assert(b.x[1]==stuck&&b.y[1]==202); /* slides */
    som_bun_follow(&b,ram,300,202,0);assert(b.x[0]==200);  /* a warp is not copied */
    memset(ram+0x10000,0,0x10000);

    uint8_t *m=ram+SOM_BUN_MONSTER_BASE;
    m[0]=1;m[0x180]=3;m[2]=210;m[4]=200;                    /* slot 0 */
    uint8_t *m1=m+SOM_BUN_OBJECT;
    m1[0]=1;m1[0x180]=3;m1[2]=0x2C;m1[3]=1;m1[4]=200;       /* slot 1 at x 300 */
    assert(som_bun_toucher(ram,200,200,SOM_BUN_TOUCH_X,SOM_BUN_TOUCH_Y,-1)==0);
    assert(som_bun_toucher(ram,200,200,SOM_BUN_TOUCH_X,SOM_BUN_TOUCH_Y,0)==-1);
    m[0x191]=0x80;                                         /* dead: ignored */
    assert(som_bun_toucher(ram,200,200,SOM_BUN_TOUCH_X,SOM_BUN_TOUCH_Y,-1)==-1);
    m[0x191]=0;m[0x1F1]=250;
    assert(som_bun_blast(ram,200,200,12,-1)==1&&m[0x1F1]==0xFF&&!m1[0x1F1]);
    b.alive[0]=b.alive[1]=1;b.x[0]=100;b.y[0]=200;b.x[1]=300;b.y[1]=200;
    assert(som_bun_pick(&b,200,200,1,290,200,0,0)==0);     /* flee from x 290 */
    assert(som_bun_pick(&b,200,200,0,0,0,1,0)==1);         /* aim right */
    assert(som_bun_pick(&b,200,200,0,0,0,-1,0)==0);
    b.alive[0]=0;assert(som_bun_pick(&b,200,200,0,0,0,-1,0)==1);
    b.x[1]=205;assert(som_bun_pick(&b,200,200,0,0,0,1,0)==-1);   /* on top of him */
    b.x[1]=300;
    b.alive[1]=0;assert(som_bun_pick(&b,200,200,0,0,0,0,0)==-1);

    SomBoom booms[SOM_BUN_BOOMS];memset(booms,0,sizeof(booms));
    for(unsigned i=0;i<SOM_BUN_BOOMS+1;i++)som_bun_boom(booms,(int)i,0,0,10+i);
    assert(booms[0].x==SOM_BUN_BOOMS&&booms[1].x==1);       /* oldest replaced */

    /* Drawing: the boy's 16x16 piece at OAM (40, 59), tile 0x1C0, palette 1,
     * shifted by (+24, 0). Tile 0x1C0 pixel (0,0) = colour 1 (plane 0). */
    memset(ram+0x0800,0,544);
    for(unsigned i=0;i<128;i++)ram[0x0800+4*i+1]=0xF0;
    ram[0x0800]=40;ram[0x0801]=59;ram[0x0802]=0xC0;ram[0x0803]=0x03;
    ram[0x0804]=90;ram[0x0805]=59;ram[0x0806]=0xC0;ram[0x0807]=0x05;   /* palette 2 */
    vram[0x7C00*2]=0x80;                                   /* row 0, pixel 0, plane 0 */
    vram[(0x7C00+16*0x11)*2+2*7+1]=0x01;                   /* tile 0x1D1 row 7 px 7: colour 2 */
    ram[0x0720+2]=0x1F;                                    /* colour 1 = red */
    ram[0x0720+4]=0x00;ram[0x0721+4]=0x7C;                 /* colour 2 = blue */
    assert(som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,24,0)==1);
    assert(frame[60][64]==som_overlay_color(0,255,0,0));
    assert(frame[60+15][64+15]==som_overlay_color(0,0,0,255));
    unsigned painted=0;
    for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)painted+=frame[y][x]!=0;
    assert(painted==2);                                    /* colour 0 transparent */
    memset(frame,0,sizeof(frame));
    ram[0x0803]=0x43;                                      /* horizontal flip */
    som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,0,0);
    assert(frame[60][40+15]==som_overlay_color(0,255,0,0)&&frame[75][40]!=0);
    memset(frame,0,sizeof(frame));
    som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,-60,0);   /* off screen */
    painted=0;
    for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)painted+=frame[y][x]!=0;
    assert(painted==0);
    som_bun_draw_boom((uint8_t*)frame,256,224,512,0,128,100,3,0);
    assert(frame[100][128]==som_overlay_color(0,255,250,220));
    som_bun_draw_boom((uint8_t*)frame,256,224,512,0,2,2,27,1);  /* clipped corner */
    puts("PASS: combo ladder and shadow clones");
    return 0;
}
