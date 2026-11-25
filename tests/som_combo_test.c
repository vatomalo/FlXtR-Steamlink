/* Combo ladder, shadow clone model and clone drawing. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_combo.h"
#include "../src/som_bunshin.h"
#include "../src/som_bunshin_draw.h"
#include "../src/som_xp.h"
#include "../src/som_perks.h"
#include "../src/som_music_mix.h"

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
    /* ... but the clones walk back to their spots beside him, faster than
     * he walks (wall gone), and never pile onto him. */
    for(int s=0;s<40;s++)som_bun_follow(&b,ram,300,202,0);
    assert(b.x[0]==276&&b.y[0]==202&&b.x[1]==324&&b.y[1]==202);
    /* Facing right with the ground below blocked: the lower clone stands
     * diagonally behind the hero, not on top of him or the other clone. */
    ram[0x1B800+4*3]=0x03;
    for(unsigned c=0;c<256;c++)ram[0x10000+((224u>>4)*128u)+c]=3;   /* wall y 224..255 */
    assert(som_bun_spawn(&b,ram,200,200,0x02,0,50)==2);
    assert(b.x[0]==200&&b.y[0]==168);                      /* above: 32 px */
    assert(b.x[1]==176&&b.y[1]==216);                      /* behind him, low */
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
    assert(som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,0u,24,0)==1);
    assert(frame[60][64]==som_overlay_color(0,255,0,0));
    assert(frame[60+15][64+15]==som_overlay_color(0,0,0,255));
    unsigned painted=0;
    for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)painted+=frame[y][x]!=0;
    assert(painted==2);                                    /* colour 0 transparent */
    memset(frame,0,sizeof(frame));
    ram[0x0743]=0x7C;                                      /* Girl's colour 1 = blue */
    assert(som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,1u,24,0)==1);
    assert(frame[60][114]==som_overlay_color(0,0,0,255)&&frame[60][64]==0);
    memset(frame,0,sizeof(frame));
    ram[0x0803]=0x43;                                      /* horizontal flip */
    som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,0u,0,0);
    assert(frame[60][40+15]==som_overlay_color(0,255,0,0)&&frame[75][40]!=0);
    memset(frame,0,sizeof(frame));
    som_bun_draw_copy((uint8_t*)frame,256,224,512,0,ram,vram,0u,-60,0);   /* off screen */
    painted=0;
    for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)painted+=frame[y][x]!=0;
    assert(painted==0);
    som_bun_draw_boom((uint8_t*)frame,256,224,512,0,128,100,3,0);
    assert(frame[100][128]==som_overlay_color(0,255,250,220));
    som_bun_draw_boom((uint8_t*)frame,256,224,512,0,2,2,27,1);  /* clipped corner */
    /* XP: weapons train the class that uses them, else their home. */
    const uint8_t ninja_weapons[2]={1,6},none[2]={0xFF,0xFF};
    assert(som_xp_weapon_discipline(1,2,ninja_weapons)==2);          /* ninja sword */
    assert(som_xp_weapon_discipline(1,SOM_DISCIPLINE_NONE,none)==SOM_DISC_SWORD);
    assert(som_xp_weapon_discipline(5,3,none)==SOM_DISC_ARCHERY);
    assert(som_xp_spell_discipline(SOM_DISCIPLINE_NONE)==SOM_DISC_MAGIC);
    /* Diminishing returns per species; another species is fresh; a
     * minute away resets; farming stops at SOM_XP_STOP_COUNT. */
    SomXpDecay d;memset(&d,0,sizeof(d));
    assert(som_xp_scaled(&d,7,10,100)==10);
    assert(som_xp_scaled(&d,7,10,101)==8);                 /* 10*8/9 */
    assert(som_xp_scaled(&d,9,10,102)==10);
    unsigned last=0;
    for(unsigned i=0;i<80;i++)last=som_xp_scaled(&d,7,10,200+i);
    assert(last==0);
    assert(som_xp_scaled(&d,7,10,300+SOM_XP_RESET_FRAMES)==10);
    for(unsigned sp=20;sp<40;sp++)som_xp_scaled(&d,sp,10,5000+sp);  /* table recycles */
    assert(som_xp_event(0,0)!=0&&som_xp_event(5,1)!=som_xp_event(5,2)&&
           som_xp_event(5,1)!=som_xp_event(6,1));
    /* Level-ups through the existing model: 100 XP for level 2. */
    SomDisciplines disc;som_disc_init(&disc);
    for(unsigned long ev=1;ev<=10;ev++)assert(som_disc_award(&disc,0,10,som_xp_event(ev,1),1));
    assert(disc.level[0]==2&&disc.experience[0]==0);
    /* Perks: ranks from verified levels only, capped, prerequisites. */
    SomDisciplines pd;som_disc_init(&pd);pd.selected=0;
    assert(!som_perk_find(0)&&!som_perk_find(200)&&som_perk_find(1));
    assert(som_perk_value(SOM_EFFECT_DODGE_FRAMES,&pd,1)==0);          /* level 1: locked */
    pd.level[0]=5;assert(som_perk_rank(som_perk_find(1),&pd)==1&&som_perk_value(SOM_EFFECT_DODGE_FRAMES,&pd,1)==2);
    pd.classless=1;assert(som_perk_rank(som_perk_find(1),&pd)==0);    /* never had the class */
    pd.held[0]=1u<<(pd.selected&15u);assert(som_perk_rank(som_perk_find(1),&pd)==1);
    pd.classless=0;pd.held[0]=0;
    pd.level[0]=14;assert(som_perk_value(SOM_EFFECT_DODGE_FRAMES,&pd,1)==4);
    pd.level[0]=100;assert(som_perk_value(SOM_EFFECT_DODGE_FRAMES,&pd,1)==6);   /* max rank / cap */
    assert(som_perk_value(SOM_EFFECT_DODGE_FRAMES,&pd,0)==0);          /* perks off */
    pd.selected=SOM_DISCIPLINE_NONE;assert(som_perk_value(SOM_EFFECT_DODGE_FRAMES,&pd,1)==0);
    pd.level[2]=10;assert(som_perk_value(SOM_EFFECT_CLONE_DAMAGE,&pd,1)==8);
    pd.level[8]=5;assert(som_perk_value(SOM_EFFECT_FLOW_MIX,&pd,1)==1);
    assert(som_perk_bits(&pd)==((1u<<2)|(1u<<4)|(1u<<6)));  /* Coup de Grace at Ninjutsu 10 */
    {   SomPerk synergy={9,"Shadow Strike",2,10,5,1,SOM_EFFECT_FINISHER_LEVEL,1,1,0,10};
        pd.level[2]=10;pd.level[0]=9;assert(som_perk_rank(&synergy,&pd)==0);   /* needs Sword 10 */
        pd.level[0]=10;assert(som_perk_rank(&synergy,&pd)==1);
    }
    /* Combo with perks: bonus levels only on top of a real chain. */
    SomCombo pc;som_combo_reset(&pc);pc.hits=2;
    assert(som_combo_level_with(&pc,2,2)==0);
    pc.hits=3;pc.mixes=4;
    assert(som_combo_level_with(&pc,2,0)==3&&som_combo_level_with(&pc,4,1)==6);
    pc.hits=40;assert(som_combo_level_with(&pc,4,2)==SOM_COMBO_LEVEL_MAX);
    /* Music mix: split frames across reads, saturating sum, silence when
     * the ring runs dry, bounded ring. */
    static SomMusicRing ring;som_mix_reset(&ring,128);
    const uint8_t bytes[10]={0x00,0x10,0x00,0xF0, 0xFF,0x7F,0x00,0x80, 0x10,0x00};
    som_mix_push_bytes(&ring,bytes,3);som_mix_push_bytes(&ring,bytes+3,7);
    assert(ring.count==2&&ring.partial_bytes==2);
    int16_t game[6]={100,-100, 30000,-30000, 5,5},mixed[6];
    assert(som_mix_apply(&ring,mixed,game,3)==2);
    assert(mixed[0]==100+0x1000/2&&mixed[1]==-100-0x1000/2);
    assert(mixed[2]==32767&&mixed[3]==-32768);                 /* clipped */
    assert(mixed[4]==5&&mixed[5]==5);                           /* dry: game only */
    som_mix_reset(&ring,999);assert(ring.gain==256);
    static uint8_t lots[SOM_MIX_RING_FRAMES*4u+400];memset(lots,1,sizeof(lots));
    som_mix_push_bytes(&ring,lots,sizeof(lots));
    assert(ring.count==SOM_MIX_RING_FRAMES&&som_mix_free_bytes(&ring)==0);
    puts("PASS: combo ladder and shadow clones");
    return 0;
}
