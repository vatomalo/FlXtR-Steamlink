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
    /* The four new disciplines are real classes; Mage blinks, Hunter and
     * Artificer evade, while Necromancer and Mentalist keep native A. */
    assert(SOM_DISCIPLINE_COUNT==16u);
    assert(!strcmp(som_discipline_names[9],"Roguecraft")&&!strcmp(som_discipline_names[10],"Bushido")&&
           !strcmp(som_discipline_names[11],"Archery"));
    for(unsigned k=0;k<SOM_DISCIPLINE_COUNT;k++)
        assert(som_disc_has_dodge(k)==(k==0||k==1||k==2||k==3||k==9||k==10||
                                        k==11||k==12||k==14));
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
    return 0;
}
