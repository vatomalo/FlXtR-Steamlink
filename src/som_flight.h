/* Player flight on outdoor maps, using Secret of Mana's native actor height
 * and noclip debug bit. The native Flammie Drum instead enters its separate
 * Mode 7 world-map ride; this keeps the party in the current map. */
#ifndef SOM_FLIGHT_H
#define SOM_FLIGHT_H

#include <stdint.h>
#include <string.h>

#ifndef RETRO_DEVICE_ID_JOYPAD_MASK
#define RETRO_DEVICE_ID_JOYPAD_MASK 256
#endif

#define SOM_FLIGHT_HEIGHT 28u
#define SOM_FLIGHT_NOCLIP 0x01u
#define SOM_FLIGHT_OUTDOOR_DRUM 0x80u
#define SOM_FLIGHT_INDOOR 0x20u

typedef struct {
    unsigned active;
    unsigned member_mask;
    unsigned map;
    uint8_t original_noclip;
    uint8_t original_z[3];
    unsigned consume_chord;
} SomFlight;

static SomFlight som_flight;

static inline unsigned som_flight_map(const uint8_t *ram){
    return (unsigned)ram[0xDC]|((unsigned)ram[0xDD]<<8);
}

static inline int som_flight_field(const uint8_t *ram){
    return ram&&ram[0xE000u]==1u&&ram[0x00E4u]==1u&&ram[0x00E6u]==0x0Fu&&
           ram[0x00D0u]==0u&&ram[0x007Du]!=1u;
}

/* Stops flight and restores the party's pre-flight heights and noclip bit.
 * A map transition resets actor height instead of carrying an old floor Z. */
static inline void som_flight_end(uint8_t *ram){
    if(!som_flight.active)return;
    if(ram){
        const int same_map=som_flight_map(ram)==som_flight.map;
        for(unsigned who=0;who<3u;who++)if(som_flight.member_mask&(1u<<who)){
            uint8_t *hero=ram+0xE000u+0x200u*who;
            if(hero[0]!=1u)continue;
            hero[0x008u]=0;
            hero[0x045u]=same_map?som_flight.original_z[who]:0u;
        }
        ram[0xCF01u]=(uint8_t)((ram[0xCF01u]&~SOM_FLIGHT_NOCLIP)|som_flight.original_noclip);
    }
    som_flight.active=0;
    som_flight.member_mask=0;
}

/* Return 1 on takeoff, 2 on landing, -1 when a takeoff is refused. */
static inline int som_flight_toggle(uint8_t *ram,unsigned controlled_member){
    if(som_flight.active){som_flight_end(ram);return 2;}
    if(controlled_member>=3u||!som_flight_field(ram))return -1;
    /* Use the same map eligibility that gates the Drum. Never take off in
     * interiors, events, ring menus or while the map is transitioning. */
    if(!(ram[0x011Fu]&SOM_FLIGHT_OUTDOOR_DRUM)||(ram[0x011Fu]&SOM_FLIGHT_INDOOR))return -1;
    som_flight.map=som_flight_map(ram);
    som_flight.original_noclip=(uint8_t)(ram[0xCF01u]&SOM_FLIGHT_NOCLIP);
    som_flight.member_mask=0;
    for(unsigned who=0;who<3u;who++){
        uint8_t *hero=ram+0xE000u+0x200u*who;
        if(hero[0]!=1u)continue;
        som_flight.member_mask|=1u<<who;
        som_flight.original_z[who]=hero[0x045u];
    }
    if(!(som_flight.member_mask&(1u<<controlled_member)))return -1;
    ram[0xCF01u]|=SOM_FLIGHT_NOCLIP;
    som_flight.active=1;
    return 1;
}

/* Run before and after the SNES frame so gravity cannot pull the player down.
 * D-pad movement, camera scroll and animation remain owned by the game. */
static inline void som_flight_frame(uint8_t *ram){
    if(!som_flight.active)return;
    if(!som_flight_field(ram)||som_flight_map(ram)!=som_flight.map){
        som_flight_end(ram);return;
    }
    ram[0xCF01u]|=SOM_FLIGHT_NOCLIP;
    for(unsigned who=0;who<3u;who++)if(som_flight.member_mask&(1u<<who)){
        uint8_t *hero=ram+0xE000u+0x200u*who;
        if(hero[0]!=1u){som_flight_end(ram);return;}
        hero[0x008u]=0;
        hero[0x045u]=SOM_FLIGHT_HEIGHT;
    }
}

static inline int16_t som_flight_filter(unsigned port,unsigned device,unsigned index,
                                         unsigned id,int16_t actual){
    if(!som_flight.consume_chord||port>=2u||index!=0u||
       (device&RETRO_DEVICE_MASK)!=RETRO_DEVICE_JOYPAD)return actual;
    const uint16_t chord=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
        (1u<<RETRO_DEVICE_ID_JOYPAD_R2)|(1u<<RETRO_DEVICE_ID_JOYPAD_START));
    if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)((uint16_t)actual&~chord);
    if(id<16u&&(chord&(1u<<id)))return 0;
    return actual;
}

static inline int som_flight_input(uint16_t current,uint16_t previous,uint8_t *ram,
                                   unsigned controlled_member){
    const uint16_t chord=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
        (1u<<RETRO_DEVICE_ID_JOYPAD_R2)|(1u<<RETRO_DEVICE_ID_JOYPAD_START));
    if(som_flight.consume_chord&&!(current&chord))som_flight.consume_chord=0;
    if((current&chord)==chord&&(previous&chord)!=chord){
        const int result=som_flight_toggle(ram,controlled_member);
        som_flight.consume_chord=1;
        return result;
    }
    return 0;
}

#endif
