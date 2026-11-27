/* The class menu's PERKS page (the ring's ACT icon, L1/R1 from CLASSES).
 *
 * One hero at a time (X: next hero). Left/Right picks the list: EQUIPPED,
 * then each class's perks, then the universal ones. Up/Down moves, B equips
 * or takes off, A closes. The lines show capacity used/total, each perk's
 * cost and a mark: * equipped, space ready, - locked (level or class not
 * had yet), x not available yet (the engine cannot run it). The bottom lines
 * describe the perk under the cursor and why it is locked.
 * Pure: the caller does the equipping (som_prt_equip) and passes the
 * runtime and the hero's unlock view in. */
#ifndef FLXTR_SOM_PERK_MENU_H
#define FLXTR_SOM_PERK_MENU_H
#include <stdio.h>
#include <string.h>
#include "som_perk_runtime.h"
#include "som_disciplines.h"

#define SOM_PMENU_LINES 18u
#define SOM_PMENU_ROWS 11u            /* list rows between the header and the description */
#define SOM_PMENU_FILTERS (2u+SOM_DISCIPLINE_COUNT)   /* equipped, 16 classes, universal */
enum { SOM_PMENU_NONE, SOM_PMENU_CLOSED, SOM_PMENU_EQUIP, SOM_PMENU_UNEQUIP, SOM_PMENU_HERO };

typedef struct {
    int open;
    uint8_t who, filter, cursor, scroll;
    char note[33];                    /* the last equip answer */
} SomPerkMenu;

/* The perks of a filter, in id order. Returns how many (at most cap). */
static inline unsigned som_pmenu_list(const SomPerkRuntime *rt,const SomPerkMenu *m,uint16_t *ids,unsigned cap){
    unsigned n=0;
    if(!m->filter){
        uint16_t own[SOM_PERK_MAX_EQUIPPED];
        const unsigned k=som_prt_ids(rt,m->who,own);
        for(unsigned i=0;i<k&&n<cap;i++)ids[n++]=own[i];
        return n;
    }
    const unsigned disc=m->filter<=SOM_DISCIPLINE_COUNT?m->filter-1u:SOM_PERK_UNIVERSAL;
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT&&n<cap;i++)
        if(som_perk_defs[i].discipline==disc)ids[n++]=som_perk_defs[i].id;
    return n;
}
static inline const char *som_pmenu_filter_name(unsigned filter){
    if(!filter)return "Equipped";
    if(filter<=SOM_DISCIPLINE_COUNT)return som_discipline_titles[filter-1u];
    return "Universal";
}
static inline char som_pmenu_mark(const SomPerkRuntime *rt,unsigned who,const SomPerkDef *d,
                                  const SomPerkUnlockView *v){
    if(som_prt_has(rt,who,d->id))return '*';
    if(!som_prt_selectable(rt,d))return 'x';
    if(!som_prt_unlocked(d,v))return '-';
    return ' ';
}
/* Text of one line (32 characters at most); 0 past the last line. */
static inline int som_pmenu_line(const SomPerkRuntime *rt,const SomPerkMenu *m,const SomPerkUnlockView *v,
                                 unsigned line,char out[33]){
    static const char *const who_names[3]={"BOY","GIRL","SPRITE"};
    const unsigned who=m->who<3u?m->who:0u;
    uint16_t ids[40];
    const unsigned n=som_pmenu_list(rt,m,ids,40u);
    const SomPerkDef *cur=n&&m->cursor<n?som_perk_def_by_id(ids[m->cursor]):NULL;
    out[0]=0;
    if(line==0u){
        snprintf(out,33,"%s PERKS %u/%u X:who L/R A:close",who_names[who],
                 som_prt_used(rt,who),som_prt_capacity(v?v->hero_level:1u));
        return 1;
    }
    if(line==1u){
        snprintf(out,33,"< %s %u >",som_pmenu_filter_name(m->filter),n);
        return 1;
    }
    if(line<2u+SOM_PMENU_ROWS){
        const unsigned row=m->scroll+line-2u;
        if(row>=n){
            if(!n&&line==2u)snprintf(out,33,m->filter?"  (none)":"  none: Right picks a class");
            return 1;
        }
        const SomPerkDef *d=som_perk_def_by_id(ids[row]);
        if(!d)return 1;
        snprintf(out,33,"%c%c%-23.23s %u",row==m->cursor?'>':' ',som_pmenu_mark(rt,who,d,v),d->name,
                 (unsigned)d->cost);
        return 1;
    }
    if(line>=SOM_PMENU_LINES)return 0;
    const unsigned at=line-(2u+SOM_PMENU_ROWS);       /* 0..4: about the cursor's perk */
    if(!cur){if(!at&&m->note[0])snprintf(out,33,"%s",m->note);return 1;}
    if(at==0u){
        const char *why="";
        if(!som_prt_selectable(rt,cur))why=" not ready";
        else if(!som_prt_unlocked(cur,v))why=" locked";
        if(cur->flags&SOM_PDF_UNIVERSAL)snprintf(out,33,"Hero Lv%u %s%s",cur->mastery,
                                                 som_perk_tier_names[cur->tier],why);
        else snprintf(out,33,"%s Lv%u %s%s",cur->discipline<SOM_DISCIPLINE_COUNT?
                      som_discipline_titles[cur->discipline]:"?",cur->mastery,som_perk_tier_names[cur->tier],why);
        return 1;
    }
    if(at==4u){snprintf(out,33,"%s",m->note);return 1;}
    /* the description, word-wrapped over three lines */
    const char *s=cur->description;
    for(unsigned l=1u;l<=at;l++){
        while(*s==' ')s++;
        size_t len=strlen(s);
        if(len>32u){len=32u;while(len&&s[len]!=' ')len--;if(!len)len=32u;}
        if(l==at){memcpy(out,s,len);out[len]=0;return 1;}
        s+=len;
    }
    return 1;
}
/* A press. *id: the perk to equip or take off. */
static inline int som_pmenu_press(const SomPerkRuntime *rt,SomPerkMenu *m,uint16_t pressed,uint16_t *id){
    uint16_t ids[40];
    unsigned n=som_pmenu_list(rt,m,ids,40u);
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A)){m->open=0;return SOM_PMENU_CLOSED;}
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_X)){
        m->who=(uint8_t)((m->who+1u)%3u);m->cursor=m->scroll=0;m->note[0]=0;return SOM_PMENU_HERO;
    }
    if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))){
        const int right=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))!=0;
        m->filter=(uint8_t)((m->filter+(right?1u:SOM_PMENU_FILTERS-1u))%SOM_PMENU_FILTERS);
        m->cursor=m->scroll=0;m->note[0]=0;
        return SOM_PMENU_NONE;
    }
    if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)))m->note[0]=0;
    if(n&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP)))m->cursor=(uint8_t)((m->cursor+n-1u)%n);
    if(n&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)))m->cursor=(uint8_t)((m->cursor+1u)%n);
    if(m->cursor>=n)m->cursor=(uint8_t)(n?n-1u:0u);
    if(m->cursor<m->scroll)m->scroll=m->cursor;
    if(m->cursor>=m->scroll+SOM_PMENU_ROWS)m->scroll=(uint8_t)(m->cursor-SOM_PMENU_ROWS+1u);
    if(n&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_B))){
        *id=ids[m->cursor];
        return som_prt_has(rt,m->who,*id)?SOM_PMENU_UNEQUIP:SOM_PMENU_EQUIP;
    }
    return SOM_PMENU_NONE;
}
#endif
