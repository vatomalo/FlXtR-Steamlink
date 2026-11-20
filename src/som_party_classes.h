#ifndef FLXTR_SOM_PARTY_CLASSES_H
#define FLXTR_SOM_PARTY_CLASSES_H

#include "som_disciplines.h"

#define SOM_PARTY_COUNT 3u

static inline const uint8_t *som_party_actor(const uint8_t *ram,unsigned slot){
    return ram+0xE000u+0x200u*slot;
}

static inline unsigned som_party_word(const uint8_t *p){
    return p[0]|((unsigned)p[1]<<8);
}

static inline int som_party_alive(const uint8_t *actor,unsigned slot){
    return actor[0]==1u&&actor[0x180]==0x80u+slot&&
           som_party_word(actor+0x182)&&!(actor[0x191]&0x80u);
}

static inline int som_party_can_cast(const uint8_t *actor,unsigned slot){
    return som_party_alive(actor,slot)&&actor[0x2C]==0u&&
           !(som_party_word(actor+0x190)&0xC160u)&&
           !actor[0x01C]&&!actor[0x060]&&!actor[0x19B];
}

static inline int som_party_heal_target(const uint8_t *ram){
    int best=-1;
    unsigned best_hp=100;
    for(unsigned slot=0;slot<SOM_PARTY_COUNT;slot++){
        const uint8_t *actor=som_party_actor(ram,slot);
        if(!som_party_alive(actor,slot))continue;
        const unsigned max=som_party_word(actor+0x184);
        if(!max)continue;
        const unsigned hp=100u*som_party_word(actor+0x182)/max;
        if(hp<=60&&hp<best_hp){best=(int)slot;best_hp=hp;}
    }
    return best;
}
#endif
