/* The class menu's GEAR page (no ROM):
 *   cc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_gear_menu.c -o build/test-gear-menu */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "som_gear_menu.h"
#define BTN(b) ((uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_##b))
int main(void){
    SomStyles st;som_styles_init(&st);
    SomGearMenu m;memset(&m,0,sizeof(m));m.open=1;
    char line[33];int dir=0;
    /* a Ninja: B styles from the table, Y throw choice */
    assert(som_gear_line(&m,&st,2u,0,1,line)&&strstr(line,"Ninja"));
    som_gear_line(&m,&st,2u,0,2,line);assert(line[0]=='>'&&strstr(line,"Ninjato"));
    som_gear_line(&m,&st,2u,0,3,line);assert(strstr(line,"< Shuriken >"));
    som_gear_line(&m,&st,2u,1,3,line);assert(strstr(line,"Fuma Shuriken"));
    som_gear_line(&m,&st,2u,2,3,line);assert(strstr(line,"Shuriken Volley"));
    som_gear_line(&m,&st,2u,0,9,line);assert(strstr(line,"Kunai")&&strstr(line,"Kusarigama"));
    assert(som_gear_press(&m,BTN(LEFT),2u,&dir)==SOM_GEAR_STYLE&&dir==-1);
    assert(som_gear_press(&m,BTN(DOWN),2u,&dir)==SOM_GEAR_NONE&&m.row==1);
    assert(som_gear_press(&m,BTN(RIGHT),2u,&dir)==SOM_GEAR_THROW&&dir==1);
    /* no choice: the Samurai's Yumi is fixed, the B row still changes */
    assert(som_gear_press(&m,BTN(RIGHT),SOM_DISC_BUSHIDO,&dir)==SOM_GEAR_NONE);
    som_gear_line(&m,&st,SOM_DISC_BUSHIDO,0,3,line);assert(strstr(line,"Yumi"));
    m.row=0;som_gear_line(&m,&st,SOM_DISC_BUSHIDO,0,2,line);assert(strstr(line,"Katana"));
    /* classless: nothing to change */
    assert(som_gear_press(&m,BTN(RIGHT),SOM_DISCIPLINE_NONE,&dir)==SOM_GEAR_NONE);
    som_gear_line(&m,&st,SOM_DISCIPLINE_NONE,0,2,line);assert(strstr(line,"No class"));
    /* every class, every row: lines fit */
    for(unsigned c=0;c<=SOM_DISCIPLINE_COUNT;c++)for(unsigned r=0;r<SOM_GEAR_ROWS;r++){
        m.row=(uint8_t)r;
        for(unsigned l=0;l<SOM_GEAR_LINES;l++){assert(som_gear_line(&m,&st,c<SOM_DISCIPLINE_COUNT?c:SOM_DISCIPLINE_NONE,0,l,line));assert(strlen(line)<=32u);}
    }
    assert(som_gear_press(&m,BTN(X),2u,&dir)==SOM_GEAR_HERO&&m.who==1);
    assert(som_gear_press(&m,BTN(A),2u,&dir)==SOM_GEAR_CLOSED&&!m.open);
    printf("PASS: gear menu page\n");
    return 0;
}
