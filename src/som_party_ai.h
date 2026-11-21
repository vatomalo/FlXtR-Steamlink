/* Native party slots only. Summoned creatures live in the monster table. */
#ifndef FLXTR_SOM_PARTY_AI_H
#define FLXTR_SOM_PARTY_AI_H

#include <stdint.h>

static inline int som_party_member_alive(const uint8_t *actor,unsigned slot){
    return slot<3u&&actor[0]==1u&&actor[0x180]==0x80u+slot&&
           (actor[0x184]|actor[0x185])&&(actor[0x182]|actor[0x183])&&
           !(actor[0x191]&0x80u);
}

/* The actor's binding and global pad assignments both have to indicate AI. */
static inline int som_party_ai_owned(const uint8_t *ram,const uint8_t *actor,unsigned slot){
    return slot>0u&&slot<3u&&!actor[0x2C]&&!ram[0xD9u+slot];
}

static inline int som_party_action_ready(const uint8_t *actor){
    const unsigned status=actor[0x190]|((unsigned)actor[0x191]<<8);
    return !(status&0xC160u)&&actor[0x060]==0u;
}
#endif
