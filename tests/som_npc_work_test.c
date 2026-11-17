/* Working villagers: frame pack, command checks, hide/restore, drawing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_npc_work.h"

static uint8_t wram[0x20000];
static uint16_t frame[224][256];

/* One kind (0xC7), facing 2, two frames: 1 tick 2x1 pixels, 2 ticks 1x1. */
static size_t make_pack(uint8_t *p){
    size_t n=0;
    memcpy(p,"SOMWORK1",8);n=8;p[n++]=2;p[n++]=0;
    for(unsigned facing=0;facing<3;facing+=2){
        p[n++]=0xC7;p[n++]=2;p[n++]=(uint8_t)facing;p[n++]=2;
        for(unsigned i=0;i<20;i++){p[n++]=(uint8_t)(i==0?0x1F:0);p[n++]=0;}  /* colour 1 red */
        p[n++]=1;p[n++]=(uint8_t)-3;p[n++]=(uint8_t)-20;p[n++]=2;p[n++]=1;p[n++]=1;p[n++]=0;
        p[n++]=2;p[n++]=4;p[n++]=(uint8_t)-1;p[n++]=1;p[n++]=1;p[n++]=1;
    }
    return n;
}
static void field(void){
    memset(wram,0,sizeof(wram));
    wram[0xE000]=1;wram[0x00E4]=1;wram[0x00E6]=0x0F;wram[0x00DC]=131;
    uint8_t *npc=wram+0xF000;                       /* slot 8 */
    npc[0]=1;npc[0x3D]=0xC7;npc[0x44]=0xC0;npc[0x0E]=0x02;
    npc[2]=472&0xFF;npc[3]=472>>8;npc[4]=404&0xFF;npc[5]=404>>8;
    wram[0xA8]=400&0xFF;wram[0xA9]=400>>8;wram[0xAA]=300&0xFF;wram[0xAB]=300>>8;
}
int main(void){
    (void)som_npc_apply_walk;(void)som_npc_parse_walk;(void)som_npc_rom_verified;
    uint8_t pack[256];
    const size_t size=make_pack(pack);
    SomWorkEntry e;
    assert(som_work_find(pack,size,0xC7,2,&e)&&e.frames==2&&e.weapon==2&&e.colours[0]==0x1F);
    assert(som_work_find(pack,size,0xC7,0,&e));
    assert(!som_work_find(pack,size,0xC7,1,&e));                  /* no down facing */
    assert(!som_work_find(pack,size,0xC0,2,&e));
    assert(!som_work_find(pack,size-1,0xC7,2,&e));               /* truncated */
    pack[0]='X';assert(!som_work_find(pack,size,0xC7,2,&e));pack[0]='S';
    assert(som_work_swing_frames(&e)==3u*SOM_WORK_TICK_FRAMES);

    SomNpcWork w;
    assert(som_npc_parse_work("npc_work 131 8 199 472 404 2 3",&w)&&w.cycles==3&&w.dir==2);
    assert(!som_npc_parse_work("npc_work 131 8 199 472 404 2",&w));
    assert(!som_npc_parse_work("npc_walk 131 8 199 472 404 2 3",&w));
    assert(som_npc_parse_work("npc_work 131 8 199 472 404 2 3",&w));

    field();
    assert(som_npc_work_refusal(wram,sizeof(wram),&w,pack,size,&e)==NULL);
    SomNpcWork bad=w;bad.cycles=0;assert(som_npc_work_refusal(wram,sizeof(wram),&bad,pack,size,&e));
    bad=w;bad.dir=1;assert(!strcmp(som_npc_work_refusal(wram,sizeof(wram),&bad,pack,size,&e),
                                   "no work frames for this kind"));
    bad=w;bad.x=470;assert(!strcmp(som_npc_work_refusal(wram,sizeof(wram),&bad,pack,size,&e),"stale position"));
    assert(!strcmp(som_npc_work_refusal(wram,sizeof(wram),&w,NULL,0,&e),"no frame pack"));

    /* Begin: only bit 7 of +0x0E changes. */
    static uint8_t before[sizeof(wram)];
    memcpy(before,wram,sizeof(wram));
    SomNpcWorkState s;memset(&s,0,sizeof(s));
    assert(som_npc_work_refusal(wram,sizeof(wram),&w,pack,size,&e)==NULL);
    som_npc_work_begin(&s,wram,&w,&e,1000);
    for(size_t i=0;i<sizeof(wram);i++)
        assert(i==0xF00Eu?wram[i]==0x82:wram[i]==before[i]);
    assert(s.length==3u*(3u*SOM_WORK_TICK_FRAMES+SOM_WORK_REST_FRAMES));
    assert(!strcmp(som_npc_work_refusal(wram,sizeof(wram),&w,pack,size,&e),"NPC already hidden"));

    /* Poses: frame 0 for 5 frames, frame 1 for 10, then the rest pose. */
    assert(som_npc_work_pose(&s,0)==s.entry.first&&som_npc_work_pose(&s,4)==s.entry.first);
    assert(som_npc_work_pose(&s,5)!=s.entry.first&&som_npc_work_pose(&s,14)!=s.entry.first);
    assert(som_npc_work_pose(&s,15)==s.entry.first);

    /* Draw at frame 1000: foot (72,104); frame 0 pixel at (-3,-20) -> (69,84).
     * The colour buffer holds the villager's red in OBJ palette 0. */
    wram[0x0702]=0x1F;
    const uint32_t red=som_overlay_color(0,255,0,0);
    assert(som_npc_work_draw(&s,wram,1000,(uint8_t*)frame,256,224,512,0));
    assert(frame[84][69]==red&&frame[84][70]==0&&frame[83][69]==0);
    memset(frame,0,sizeof(frame));
    s.dir=3;                                          /* left: mirrored, -(dx+x)-1 */
    assert(som_npc_work_draw(&s,wram,1000,(uint8_t*)frame,256,224,512,0));
    assert(frame[84][74]==red);
    s.dir=2;

    /* Ticks: runs, ends when done and shows the villager again. */
    assert(som_npc_work_tick(&s,wram,1001)==NULL&&s.active);
    assert(!strcmp(som_npc_work_tick(&s,wram,1000+s.length),"done")&&!s.active);
    assert(wram[0xF00E]==0x02);
    assert(som_npc_work_tick(&s,wram,5000)==NULL);   /* idle */

    /* Talking ends it too. */
    som_npc_work_begin(&s,wram,&w,&e,2000);
    wram[0x00D0]=0x81;
    assert(!strcmp(som_npc_work_tick(&s,wram,2001),"event (talk) started")&&wram[0xF00E]==0x02);
    wram[0x00D0]=0;

    /* Map change: the slot belongs to another object, nothing is written. */
    som_npc_work_begin(&s,wram,&w,&e,3000);
    wram[0x00DC]=132;wram[0xF03D]=0xC0;wram[0xF00E]=0x80;
    assert(!strcmp(som_npc_work_tick(&s,wram,3001),"villager or map gone")&&!s.active);
    assert(wram[0xF00E]==0x80);
    /* Live colours: the Turbo filter measured at CF06 = 4 (R|1, G|1, B/2). */
    {
        SomNpcWorkState c;memset(&c,0,sizeof(c));
        c.slot=8;c.active=1;
        for(unsigned i=0;i<20;i++){                       /* spread-out ROM colours */
            const unsigned r=(i*3u)&31u,g=(31u-i*2u)&31u,b=(i*5u+2u)&31u;
            c.entry.colours[i]=(uint16_t)(r|g<<5|b<<10);
        }
        field();
        wram[0xF08B]=0x27;                                /* attribute: palette 3 */
        for(unsigned i=1;i<=15;i++){
            const unsigned v=c.entry.colours[i-1];
            const unsigned f=(((v&31u)|1u))|((((v>>5)&31u)|1u)<<5)|((((v>>10)&31u)>>1)<<10);
            wram[0x0700+32*3+2*i]=(uint8_t)f;wram[0x0700+32*3+2*i+1]=(uint8_t)(f>>8);
        }
        uint16_t out[21];
        assert(som_npc_work_colours(&c,wram,out));
        for(unsigned i=1;i<=15;i++)assert(out[i]==(wram[0x760+2*i]|wram[0x761+2*i]<<8));
        for(unsigned i=16;i<=20;i++){
            const unsigned v=c.entry.colours[i-1];
            const int want[3]={(int)((v&31u)|1u),(int)(((v>>5)&31u)|1u),(int)(((v>>10)&31u)>>1)};
            for(unsigned ch=0;ch<3;ch++){
                const int got=(out[i]>>(5*ch))&31;
                assert(got-want[ch]<=1&&want[ch]-got<=1);
            }
        }
        wram[0xF08B]=0x25;                                /* wrong slot: ROM colours */
        for(unsigned i=1;i<=15;i++){wram[0x0740+2*i]=(uint8_t)(i*37u);wram[0x0741+2*i]=(uint8_t)(i*11u&0x7F);}
        assert(!som_npc_work_colours(&c,wram,out));
        for(unsigned i=1;i<=20;i++)assert(out[i]==c.entry.colours[i-1]);
    }
    puts("som npc work: ok");
    return 0;
}
