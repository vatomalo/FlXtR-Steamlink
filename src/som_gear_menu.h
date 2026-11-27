/* The class menu's GEAR page (the ring's ACT icon, after PERKS).
 *
 * Player, 2026-11-27: "a character equip menu inside act menu where you can
 * change shuriken to kunai, or fuma shuriken etc ... for all classes, but if
 * you do not need to do not invent new weapons". So it only picks among
 * what the classes already have:
 *   B  the class's three weapon styles (src/som_weapon_styles.h): Kunai /
 *      Ninjato / Kusarigama, Tanto / Katana / Dai Katana, ...; casters'
 *      spell sets. Each keeps its own level, speed, power and reach.
 *   Y  the throw where there is a choice. The Ninja: Shuriken (a small star
 *      thrown straight and fast like a mage's ball), Fuma Shuriken (the
 *      game's own boomerang flight, drawn as a full-size shuriken) or
 *      Shuriken Volley (three small stars in a fan), or a jutsu on Y
 *      instead of a throw (player, 2026-11-27: "Ninjas should have elemental
 *      style magic, but also other things kagebunshin could sit there"):
 *      Katon (fire), Suiton (water, slows), Raiton (lightning, pierces),
 *      Kage Bunshin; they cost Chakra. Other Y and X weapons are shown, fixed.
 * One hero at a time (X: next). Up/Down picks the row, Left/Right (or B)
 * changes it, A closes. Pure: the caller applies the change. */
#ifndef FLXTR_SOM_GEAR_MENU_H
#define FLXTR_SOM_GEAR_MENU_H
#include <stdio.h>
#include <string.h>
#include "som_weapon_styles.h"

#define SOM_GEAR_LINES 12u
#define SOM_GEAR_ROWS 3u
#define SOM_THROWS 7u
enum { SOM_GEAR_NONE, SOM_GEAR_CLOSED, SOM_GEAR_STYLE, SOM_GEAR_THROW, SOM_GEAR_HERO };

/* Thrown looks per class: [0] is the default; "" ends the list. */
static const char *const som_gear_throws[SOM_DISCIPLINE_COUNT][SOM_THROWS]={
    [2]={"Shuriken","Fuma Shuriken","Shuriken Volley","Katon","Suiton","Raiton","Kage Bunshin"},   /* Ninjutsu */
};
static inline unsigned som_gear_throw_count(unsigned cls){
    unsigned n=0;
    if(cls<SOM_DISCIPLINE_COUNT)while(n<SOM_THROWS&&som_gear_throws[cls][n]&&*som_gear_throws[cls][n])n++;
    return n;
}

typedef struct {
    int open;
    uint8_t who, row;
} SomGearMenu;

/* cls: the hero's class (SOM_DISCIPLINE_NONE classless); throw_pick: the
 * class's chosen throw look. */
/* pouch: shuriken left (-1: no pouch shown) */
static inline int som_gear_line(const SomGearMenu *m,const SomStyles *st,unsigned cls,unsigned throw_pick,
                                int pouch,unsigned line,char out[33]){
    static const char *const who_names[3]={"BOY","GIRL","SPRITE"};
    out[0]=0;
    if(line>=SOM_GEAR_LINES)return 0;
    if(line==0u){snprintf(out,33,"%s GEAR X:who L/R A:close",who_names[m->who%3u]);return 1;}
    if(cls>=SOM_DISCIPLINE_COUNT){
        if(line==2u)snprintf(out,33,"  No class: the game's weapons,");
        if(line==3u)snprintf(out,33,"  change them in the ring.");
        return 1;
    }
    if(line==1u){snprintf(out,33,"< %s >",som_discipline_titles[cls]);return 1;}
    const unsigned throws=som_gear_throw_count(cls);
    unsigned index=0;
    const SomWeaponStyle *s=som_style_of(st,cls,&index);
    if(line>=2u&&line<2u+SOM_GEAR_ROWS){
        const unsigned row=line-2u;
        const char cur=row==m->row?'>':' ';
        if(row==0u)snprintf(out,33,"%cB < %s > Lv%u",cur,s?s->name:"?",s?(unsigned)st->level[index]:0u);
        else if(row==1u){
            if(throws>1u)snprintf(out,33,"%cY < %s >",cur,som_gear_throws[cls][throw_pick%throws]);
            else snprintf(out,33,"%cY   %s",cur,*som_custom_weapon_name(st,cls,1)?som_custom_weapon_name(st,cls,1):"-");
        }else snprintf(out,33,"%cX   %s",cur,*som_custom_weapon_name(st,cls,2)?som_custom_weapon_name(st,cls,2):"-");
        return 1;
    }
    const unsigned at=line-(2u+SOM_GEAR_ROWS);
    if(m->row==0u&&s){
        if(at==1u){
            const char *kind=s->speed_pct>100u?"fast":s->reach_bonus>0||s->power_pct>100u?"heavy":"balanced";
            char wide[64];
            snprintf(wide,sizeof(wide),"  %s: speed %u%% power %u%%",kind,(unsigned)s->speed_pct,(unsigned)s->power_pct);
            memcpy(out,wide,32);out[32]=0;
        }
        if(at==2u&&s->reach_bonus)snprintf(out,33,"  long cut +%d px",(int)s->reach_bonus);
        if(at==3u)snprintf(out,33,"  each style levels on its own");
        if(at==4u){
            out[0]=' ';out[1]=' ';out[2]=0;
            for(unsigned k=0;k<SOM_STYLES_PER_CLASS;k++){
                const unsigned i=som_style_index(cls,k);
                char part[16];snprintf(part,sizeof(part),"%s%s",k?" / ":"",som_weapon_styles[i].name);
                if(strlen(out)+strlen(part)<=32u)strcat(out,part);
            }
        }
    }else if(m->row==1u){
        if(throws<=1u){if(at==1u)snprintf(out,33,"  fixed for this class");}
        else if(throw_pick%throws==0u){
            if(at==1u)snprintf(out,33,"  small, straight and fast");
            if(at==2u)snprintf(out,33,"  one per throw (charged: more)");
            if(at==3u&&pouch>=0)snprintf(out,33,"  pouch %d/15, gathered after",pouch);
            if(at==4u&&pouch>=0)snprintf(out,33,"  a fight, full at a save");
        }else if(throw_pick%throws==1u){
            if(at==1u)snprintf(out,33,"  full size, the game's flight:");
            if(at==2u)snprintf(out,33,"  out, through foes, and back");
        }else if(throw_pick%throws==3u){
            if(at==1u)snprintf(out,33,"  fire jutsu: a burning ball");
            if(at==2u)snprintf(out,33,"  hits hard, 10 Chakra");
        }else if(throw_pick%throws==4u){
            if(at==1u)snprintf(out,33,"  water jutsu: a ball that");
            if(at==2u)snprintf(out,33,"  slows what it hits, 10 Chakra");
        }else if(throw_pick%throws==5u){
            if(at==1u)snprintf(out,33,"  lightning jutsu: a bolt");
            if(at==2u)snprintf(out,33,"  through every foe, 12 Chakra");
        }else if(throw_pick%throws==6u){
            if(at==1u)snprintf(out,33,"  shadow clones on Y");
            if(at==2u)snprintf(out,33,"  (the boy's art), 15 Chakra");
        }else{
            if(at==1u)snprintf(out,33,"  three small stars in a fan");
            if(at==2u)snprintf(out,33,"  each hits once, uses three");
            if(at==3u&&pouch>=0)snprintf(out,33,"  pouch %d/15, gathered after",pouch);
            if(at==4u&&pouch>=0)snprintf(out,33,"  a fight, full at a save");
        }
    }else if(at==1u)snprintf(out,33,"  fixed for this class");
    return 1;
}
static inline int som_gear_press(SomGearMenu *m,uint16_t pressed,unsigned cls,int *dir){
    *dir=0;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A)){m->open=0;return SOM_GEAR_CLOSED;}
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_X)){m->who=(uint8_t)((m->who+1u)%3u);m->row=0;return SOM_GEAR_HERO;}
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))m->row=(uint8_t)((m->row+SOM_GEAR_ROWS-1u)%SOM_GEAR_ROWS);
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))m->row=(uint8_t)((m->row+1u)%SOM_GEAR_ROWS);
    if(cls>=SOM_DISCIPLINE_COUNT)return SOM_GEAR_NONE;
    if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)|(1u<<RETRO_DEVICE_ID_JOYPAD_B))){
        *dir=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:1;
        if(m->row==0u)return SOM_GEAR_STYLE;
        if(m->row==1u&&som_gear_throw_count(cls)>1u)return SOM_GEAR_THROW;
    }
    return SOM_GEAR_NONE;
}
#endif
