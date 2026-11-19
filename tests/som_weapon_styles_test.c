#include <assert.h>
#include <stdio.h>
#include "../src/som_weapon_styles.h"

int main(void){
    SomStyles s;som_styles_init(&s);
    unsigned index;
    /* Level 1 hits like the tier-0 row, the max level like the top tier. */
    const SomWeaponStyle *katana=som_style_of(&s,10,&index);
    assert(som_style_attack(katana,1,3,99)==3);
    assert(som_style_attack(katana,SOM_STYLE_MAX_LEVEL,3,99)==99);
    const SomWeaponStyle *dai=&som_weapon_styles[som_style_index(10,2)];
    const SomWeaponStyle *tanto=&som_weapon_styles[som_style_index(10,0)];
    assert(som_style_attack(dai,1,10,99)>10&&som_style_attack(tanto,1,10,99)<10);
    assert(som_style_attack(tanto,1,1,1)>=1);
    /* Own levels, capped. */
    assert(!som_style_award(&s,index,10));
    assert(som_style_award(&s,index,som_style_next_xp(1)));
    assert(s.level[index]==2);
    for(int i=0;i<100;i++)som_style_award(&s,index,1000);
    assert(s.level[index]==SOM_STYLE_MAX_LEVEL&&!som_style_award(&s,index,10));
    assert(s.level[som_style_index(10,0)]==1);     /* other styles untouched */
    /* Cooldown: fast runs down sooner, slow later, 0 stays 0. */
    int carry=0;uint8_t c=60;unsigned frames=0;
    while(c){c=(uint8_t)(som_style_cooldown(c,150,&carry)-(c?1:0));frames++;}
    assert(frames==40);
    carry=0;c=60;frames=0;
    while(c&&frames<500){c=som_style_cooldown(c,70,&carry);if(c)c--;frames++;}
    assert(frames>60&&frames<100);
    carry=0;assert(som_style_cooldown(0,150,&carry)==0);
    /* Long cut: ahead past reach, inside the bonus, near the line. */
    assert(som_style_in_cut(40,0,0x02u,28,20));
    assert(!som_style_in_cut(-40,0,0x02u,28,20));
    assert(som_style_in_cut(-40,4,0x82u,28,20));
    assert(!som_style_in_cut(20,0,0x02u,28,20));
    assert(!som_style_in_cut(60,0,0x02u,28,20));
    assert(som_style_in_cut(0,-35,0x00u,28,20)&&!som_style_in_cut(0,35,0x00u,28,20));
    assert(!som_style_in_cut(40,0,0x02u,28,0));
    /* Save line round trip. */
    s.selected[10]=2;
    char line[600];assert(som_styles_format(&s,line,sizeof(line)));
    SomStyles back;assert(som_styles_parse(&back,line));
    assert(back.selected[10]==2&&back.level[index]==SOM_STYLE_MAX_LEVEL);
    assert(!som_styles_parse(&back,"s1 9"));
    assert(!som_styles_parse(&back,"v1 0"));
    puts("som weapon styles: ok");
    return 0;
}
