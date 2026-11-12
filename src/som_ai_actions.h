#ifndef FLXTR_SOM_AI_ACTIONS_H
#define FLXTR_SOM_AI_ACTIONS_H
/* One-shot Libretro actions, no game-memory writes and no dependencies. */
#include <stdint.h>
#include <string.h>
#include "vendor/libretro.h"

typedef enum {
    SOM_ACT_NONE, SOM_ACT_RIGHT, SOM_ACT_LEFT, SOM_ACT_UP, SOM_ACT_DOWN,
    SOM_ACT_ATTACK, SOM_ACT_OPEN_RING, SOM_ACT_CONFIRM, SOM_ACT_CANCEL,
    SOM_ACT_NEXT_RING, SOM_ACT_PREV_RING, SOM_ACT_SNAPSHOT
} SomAction;

static inline SomAction som_parse_action(const char *command){
    struct SomCommand {const char *name;SomAction action;};
    static const struct SomCommand choices[]={
        {"right",SOM_ACT_RIGHT},{"left",SOM_ACT_LEFT},
        {"up",SOM_ACT_UP},{"down",SOM_ACT_DOWN},
        {"attack",SOM_ACT_ATTACK},{"open_ring",SOM_ACT_OPEN_RING},
        {"confirm",SOM_ACT_CONFIRM},{"cancel",SOM_ACT_CANCEL},
        {"next_ring",SOM_ACT_NEXT_RING},{"prev_ring",SOM_ACT_PREV_RING},
        {"snapshot",SOM_ACT_SNAPSHOT}
    };
    for(unsigned i=0;i<sizeof(choices)/sizeof(choices[0]);i++)
        if(!strcmp(command,choices[i].name))return choices[i].action;
    return SOM_ACT_NONE;
}
static inline unsigned som_action_button(SomAction action){
    switch(action){
    case SOM_ACT_RIGHT:return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    case SOM_ACT_LEFT:return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case SOM_ACT_UP:return RETRO_DEVICE_ID_JOYPAD_UP;
    case SOM_ACT_DOWN:return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case SOM_ACT_ATTACK:return RETRO_DEVICE_ID_JOYPAD_B;
    case SOM_ACT_OPEN_RING:return RETRO_DEVICE_ID_JOYPAD_Y;
    case SOM_ACT_CONFIRM:return RETRO_DEVICE_ID_JOYPAD_B;
    case SOM_ACT_CANCEL:return RETRO_DEVICE_ID_JOYPAD_A;
    case SOM_ACT_NEXT_RING:return RETRO_DEVICE_ID_JOYPAD_R;
    case SOM_ACT_PREV_RING:return RETRO_DEVICE_ID_JOYPAD_L;
    default:return 16u;
    }
}
static inline unsigned som_action_frames(SomAction action){
    switch(action){
    case SOM_ACT_RIGHT:case SOM_ACT_LEFT:case SOM_ACT_UP:case SOM_ACT_DOWN:return 30;
    case SOM_ACT_NONE:case SOM_ACT_SNAPSHOT:return 0;
    default:return 2; /* one short button pulse, not a hold */
    }
}
static inline const char *som_action_name(SomAction action){
    static const char *const names[]={"none","right","left","up","down","attack","open_ring",
        "confirm","cancel","next_ring","prev_ring","snapshot"};
    return (unsigned)action<sizeof(names)/sizeof(names[0])?names[action]:"invalid";
}
#endif
