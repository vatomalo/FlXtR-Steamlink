/* Standalone classless progression/menu tests. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../src/som_disciplines.h"
int main(void){
    SomDisciplines d;som_disc_init(&d);
    assert(d.selected==SOM_DISCIPLINE_NONE);
    assert(som_disc_title(&d)==NULL);
    {   /* HUD portrait: palette 1, tile 0x1E8.., low on screen. */
        static uint8_t ram[0x20000];
        for(unsigned i=0;i<128;i++)ram[0x0800+4*i+1]=0xF0;
        int x=-1,y=-1;assert(!som_hud_portrait(ram,&x,&y));
        ram[0x0804]=50;ram[0x0805]=100;ram[0x0806]=0xE8;ram[0x0807]=0x33;   /* too high */
        ram[0x0808]=88;ram[0x0809]=199;ram[0x080A]=0xC0;ram[0x080B]=0x33;   /* body tile */
        ram[0x080C]=88;ram[0x080D]=199;ram[0x080E]=0xE8;ram[0x080F]=0x35;   /* palette 2 */
        assert(!som_hud_portrait(ram,&x,&y));
        ram[0x0810]=88;ram[0x0811]=199;ram[0x0812]=0xE8;ram[0x0813]=0x33;
        assert(som_hud_portrait(ram,&x,&y)&&x==88&&y==199);
    }
    {   /* Class label: centred under the hero, clipped, outlined. */
        uint16_t frame[224][256];memset(frame,0,sizeof(frame));
        assert(!som_disc_draw_label((uint8_t*)frame,256,224,512,0,NULL,128,100));
        assert(!som_disc_draw_label((uint8_t*)frame,256,224,512,0,"Monk",-1,100));
        assert(som_disc_draw_label((uint8_t*)frame,256,224,512,0,"Monk",128,100));
        const uint16_t white=(uint16_t)som_overlay_color(0,255,255,255);
        unsigned ink=0,above=0;
        for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)if(frame[y][x]==white){
            ink++;if(y<100u+SOM_CLASS_LABEL_DROP_PX)above++;
            assert(x>=128-12&&x<=128+12);
        }
        assert(ink>0&&above==0);
        memset(frame,0,sizeof(frame));
        assert(som_disc_draw_label((uint8_t*)frame,256,224,512,0,"Weaponmaster",250,223));
        memset(frame,0,sizeof(frame));
        assert(som_disc_draw_label((uint8_t*)frame,256,224,512,0,"Weaponmaster",2,0));
    }
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)assert(d.level[i]==1&&d.experience[i]==0);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_OPENED);
    assert(d.open&&d.selected==SOM_DISCIPLINE_NONE);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_DISC_NONE);
    assert(d.cursor==1);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_SELECTED);
    assert(d.selected==1);
    assert(som_disc_visual_profile(&d)==SOM_PAL_MONK_CRIMSON_GOLD);
    assert(!strcmp(som_disc_title(&d),"Monk"));
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_SELECTED);
    assert(d.selected==SOM_DISCIPLINE_NONE);
    assert(som_disc_visual_profile(&d)==SOM_PAL_ORIGINAL);
    assert(!som_disc_award(&d,2,40,1,0)); /* not verified */
    assert(!som_disc_award(&d,2,40,0,1)); /* no event ID */
    assert(som_disc_award(&d,2,40,100,1));
    assert(!som_disc_award(&d,2,40,100,1)); /* duplicate */
    assert(d.experience[2]==40&&d.level[2]==1);
    assert(som_disc_award(&d,2,90,101,1));
    assert(d.level[2]==2&&d.experience[2]==30);
    assert(d.level[1]==1&&d.experience[1]==0);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_A)==SOM_DISC_CLOSED);
    assert(!d.open);
    char line[33];assert(som_disc_line(&d,3,line));
    assert(strstr(line,"Ninjutsu")!=NULL);
    puts("disciplines model and menu tests passed");
        /* The sixteen disciplines include Hunter, Necromancer, Artificer and Mentalist. */
    assert(SOM_DISCIPLINE_COUNT==16u);
    assert(!strcmp(som_discipline_names[9],"Roguecraft")&&!strcmp(som_discipline_names[10],"Bushido")&&
           !strcmp(som_discipline_names[11],"Archery")&&
           !strcmp(som_discipline_names[15],"Psionics"));
    for(unsigned k=0;k<SOM_DISCIPLINE_COUNT;k++)
        assert(som_disc_has_dodge(k)==(k==0||k==1||k==2||k==3||k==9||k==10||k==11||k==12||k==14));
    {
        SomDisciplines v;som_disc_init(&v);
        unsigned seen[3];
        for(unsigned k=0;k<3;k++){
            v.selected=(uint8_t)(9u+k);seen[k]=som_disc_visual_profile(&v);
            assert(seen[k]!=SOM_PAL_ORIGINAL);
            uint16_t grey=som_pal_rgb(20,20,20);
            assert(som_pal_color(grey,seen[k])!=grey);
        }
        assert(seen[0]!=seen[1]&&seen[1]!=seen[2]&&seen[0]!=seen[2]);
        char line[33];
        assert(som_disc_line(&v,12,line)&&strstr(line,"Archery"));
    }
    {   /* The player's class order: Y picks the row up, Up/Down carry it,
         * Y puts it down; B selects the class shown, and the cycle follows. */
        SomDisciplines o;som_disc_init(&o);
        assert(som_disc_order_valid(o.order));
        const uint16_t Y=1u<<RETRO_DEVICE_ID_JOYPAD_Y,UP=1u<<RETRO_DEVICE_ID_JOYPAD_UP,
            DOWN=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN,B=1u<<RETRO_DEVICE_ID_JOYPAD_B;
        o.open=1;o.cursor=10;                       /* Bushido */
        assert(som_disc_press(&o,Y)==SOM_DISC_NONE&&o.grab);
        char line[33];som_disc_line(&o,11,line);assert(line[0]=='=');
        for(unsigned k=0;k<10;k++)assert(som_disc_press(&o,UP)==SOM_DISC_NONE);
        assert(o.cursor==0&&o.order[0]==10&&o.order[1]==0&&o.order[10]==9);
        assert(som_disc_press(&o,Y)==SOM_DISC_ORDERED&&!o.grab);
        assert(som_disc_order_valid(o.order));
        som_disc_line(&o,1,line);assert(line[0]=='>'&&strstr(line,"Bushido"));
        assert(som_disc_press(&o,B)==SOM_DISC_SELECTED&&o.selected==10);
        assert(som_disc_order_step(&o,10,1)==0&&som_disc_order_step(&o,0,-1)==10);
        assert(som_disc_order_step(&o,10,-1)==15);
        assert(som_disc_order_step(&o,SOM_DISCIPLINE_NONE,1)==10);
        assert(som_disc_row_of(&o,9)==10);
        o.order[3]=o.order[4];assert(!som_disc_order_valid(o.order));
        (void)DOWN;
    }
    {   /* classless rules: level, stats, tiers, passives, old saves */
        const uint8_t boy[5]={18,18,17,7,9},girl[5]={11,11,11,7,15},sprite[5]={8,13,9,15,5};
        assert(som_unlock_mask(4,boy,0)==0);
        uint16_t m=som_unlock_mask(5,boy,0);
        assert(m==((1u<<0)|(1u<<1)|(1u<<9)|(1u<<11)));          /* sword, monk, rogue, archer */
        assert(som_unlock_mask(5,girl,0)==((1u<<4)|(1u<<7)));   /* cleric, dancer */
        assert(som_unlock_mask(5,sprite,0)==((1u<<3)|(1u<<11)));/* mage, archer */
        assert(!(som_unlock_mask(14,boy,1u)>>10&1u)&&(som_unlock_mask(15,boy,1u)>>10&1u));
        assert(!(som_unlock_mask(99,boy,0)>>10&1u));            /* samurai needs swordsman */
        SomDisciplines d;som_disc_init(&d);
        for(unsigned c=0;c<SOM_DISCIPLINE_COUNT;c++)assert(som_disc_may(&d,0,c)); /* old save */
        d.classless=1;d.party[0]=d.party[1]=SOM_DISCIPLINE_NONE;
        for(unsigned c=0;c<SOM_DISCIPLINE_COUNT;c++)assert(!som_disc_may(&d,0,c));
        d.open=1;d.cursor=0;
        assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_LOCKED&&d.selected==SOM_DISCIPLINE_NONE);
        assert(som_disc_order_step_for(&d,0,SOM_DISCIPLINE_NONE,1)==SOM_DISCIPLINE_NONE);
        d.can[0]=m;
        assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_SELECTED&&d.selected==0);
        assert(som_disc_order_step_for(&d,0,0,1)==1&&som_disc_order_step_for(&d,0,1,1)==9);
        assert(som_disc_has_dodge(SOM_DISCIPLINE_NONE)&&som_disc_dodge(SOM_DISCIPLINE_NONE,1).speed);
    }
    return 0;
}
