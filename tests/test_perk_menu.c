/* The class menu's PERKS page (no ROM):
 *   cc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_perk_menu.c -o build/test-perk-menu */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "som_perk_menu.h"

#define BTN(b) ((uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_##b))
int main(void){
    SomPerkRuntime rt;som_prt_init(&rt);
    uint16_t levels[SOM_DISCIPLINE_COUNT];
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)levels[i]=1;
    levels[SOM_DISC_BUSHIDO]=10;
    SomPerkUnlockView v={levels,0,0,1};                      /* hero level 1: capacity 6 */
    SomPerkMenu m;memset(&m,0,sizeof(m));m.open=1;
    char line[33];
    assert(som_pmenu_line(&rt,&m,&v,0,line)&&strstr(line,"BOY PERKS 0/6"));
    assert(som_pmenu_line(&rt,&m,&v,1,line)&&strstr(line,"Equipped 0"));
    assert(som_pmenu_line(&rt,&m,&v,2,line)&&strstr(line,"none"));
    /* Right walks to Samurai: classes in som_discipline_titles order */
    uint16_t id=0;
    for(unsigned i=0;i<=SOM_DISC_BUSHIDO;i++)assert(som_pmenu_press(&rt,&m,BTN(RIGHT),&id)==SOM_PMENU_NONE);
    som_pmenu_line(&rt,&m,&v,1,line);assert(strstr(line,"Samurai 14"));
    som_pmenu_line(&rt,&m,&v,2,line);
    assert(line[0]=='>'&&line[1]==' '&&strstr(line,"Iaijutsu Focus"));   /* ready */
    /* Down to Bushido (adept, level 10: unlocked) and B equips */
    for(unsigned i=0;i<5u;i++)som_pmenu_press(&rt,&m,BTN(DOWN),&id);
    assert(som_pmenu_press(&rt,&m,BTN(B),&id)==SOM_PMENU_EQUIP&&id==2006);
    assert(som_prt_equip(&rt,0,id,&v)==SOM_PEQ_OK);
    som_pmenu_line(&rt,&m,&v,7,line);assert(line[1]=='*');
    som_pmenu_line(&rt,&m,&v,0,line);assert(strstr(line,"2/6"));
    /* Mushin needs an unwired hook: marked x; Kirisute (level 28): locked '-' */
    som_pmenu_line(&rt,&m,&v,8,line);assert(line[1]=='x'&&strstr(line,"Mushin"));
    som_pmenu_line(&rt,&m,&v,13,line);
    /* description of the cursor's perk */
    som_pmenu_line(&rt,&m,&v,2+SOM_PMENU_ROWS,line);assert(strstr(line,"Samurai Lv10 Adept"));
    som_pmenu_line(&rt,&m,&v,3+SOM_PMENU_ROWS,line);assert(!strncmp(line,"A perfect block",15));
    /* scrolling: Mentalist has 27 */
    m.filter=1u+SOM_DISC_PSIONICS;m.cursor=m.scroll=0;
    for(unsigned i=0;i<20u;i++)som_pmenu_press(&rt,&m,BTN(DOWN),&id);
    assert(m.cursor==20&&m.scroll==20u-SOM_PMENU_ROWS+1u);
    som_pmenu_line(&rt,&m,&v,2+SOM_PMENU_ROWS-1u,line);assert(line[0]=='>');
    /* back to Equipped: B takes it off */
    m.filter=0;m.cursor=m.scroll=0;
    assert(som_pmenu_press(&rt,&m,BTN(B),&id)==SOM_PMENU_UNEQUIP&&id==2006);
    /* X: next hero; A: closes */
    assert(som_pmenu_press(&rt,&m,BTN(X),&id)==SOM_PMENU_HERO&&m.who==1);
    som_pmenu_line(&rt,&m,&v,0,line);assert(strstr(line,"GIRL PERKS"));
    assert(som_pmenu_press(&rt,&m,BTN(A),&id)==SOM_PMENU_CLOSED&&!m.open);
    /* every line of every list fits 32 characters */
    for(unsigned f=0;f<SOM_PMENU_FILTERS;f++){
        m.filter=(uint8_t)f;m.cursor=m.scroll=0;
        for(unsigned c=0;c<30u;c++){
            for(unsigned l=0;l<SOM_PMENU_LINES;l++){assert(som_pmenu_line(&rt,&m,&v,l,line));assert(strlen(line)<=32u);}
            som_pmenu_press(&rt,&m,BTN(DOWN),&id);
        }
    }
    printf("PASS: perk menu page\n");
    return 0;
}
