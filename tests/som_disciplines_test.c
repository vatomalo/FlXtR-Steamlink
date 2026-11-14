/* Standalone classless progression/menu tests. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../src/som_disciplines.h"
int main(void){
    SomDisciplines d;som_disc_init(&d);
    assert(d.selected==SOM_DISCIPLINE_NONE);
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)assert(d.level[i]==1&&d.experience[i]==0);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_OPENED);
    assert(d.open&&d.selected==SOM_DISCIPLINE_NONE);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_DISC_NONE);
    assert(d.cursor==1);
    assert(som_disc_press(&d,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_SELECTED);
    assert(d.selected==1);
    assert(som_disc_visual_profile(&d)==SOM_PAL_MONK_CRIMSON_GOLD);
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
    return 0;
}
