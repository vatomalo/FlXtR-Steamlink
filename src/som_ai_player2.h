#ifndef FLXTR_SOM_AI_PLAYER2_H
#define FLXTR_SOM_AI_PLAYER2_H

/*
 * Secret of Mana (SNES) AI Player 2, prototype policy.
 *
 * IMPORTANT: observation fields are NOT ROM addresses. A future Snes9x
 * Libretro adapter must supply VERIFIED values for the exact ROM revision.
 * A missing/invalid observation outputs zero buttons, letting the game
 * retain control of its normal companion AI.
 *
 * Masks follow Libretro's SNES joypad ID bit positions, not SDL keycodes.
 * This is deterministic and allocation-free; no LLM/network dependency.
 */
#include <stdint.h>

enum {
    SOM_PAD_B      = 1u << 0,
    SOM_PAD_Y      = 1u << 1,
    SOM_PAD_SELECT = 1u << 2,
    SOM_PAD_START  = 1u << 3,
    SOM_PAD_UP     = 1u << 4,
    SOM_PAD_DOWN   = 1u << 5,
    SOM_PAD_LEFT   = 1u << 6,
    SOM_PAD_RIGHT  = 1u << 7,
    SOM_PAD_A      = 1u << 8,
    SOM_PAD_X      = 1u << 9,
    SOM_PAD_L      = 1u << 10,
    SOM_PAD_R      = 1u << 11
};

typedef struct {
    int32_t leader_x, leader_y;
    int32_t partner_x, partner_y;
    int32_t enemy_x, enemy_y;
    /* Game-specific RAM adapter must verify and populate every flag. */
    uint8_t verified_state, in_game, partner_controllable, enemy_valid;
    uint8_t can_attack, menu_open, partner_alive;
} SomAiObservation;

typedef struct {
    int32_t follow_radius;   /* pixels or verified coordinate units */
    int32_t attack_radius;
    int32_t axis_deadzone;
    uint8_t defend_leader;
} SomAiStrategy;

typedef struct {
    uint16_t buttons;
    uint8_t valid;
    uint8_t targeting_enemy;
} SomAiDecision;

static inline uint64_t som_ai_distance_squared(int32_t dx,int32_t dy){
    int64_t x=dx,y=dy;
    return (uint64_t)(x*x+y*y);
}

static inline SomAiStrategy som_ai_default_strategy(void){
    SomAiStrategy s={24,30,6,1};
    return s;
}

/* Caller must already have validated coordinates, including units and bounds.
 * This function NEVER presses Start to hijack an unjoined controller port.
 */
static inline SomAiDecision som_ai_decide(SomAiObservation o,SomAiStrategy s){
    SomAiDecision out={0,0,0};
    if(!o.verified_state||!o.in_game||!o.partner_controllable||
       !o.partner_alive||o.menu_open)return out;
    if(s.follow_radius<0||s.attack_radius<0||s.axis_deadzone<0)return out;
    out.valid=1;

    int32_t dx=o.leader_x-o.partner_x;
    int32_t dy=o.leader_y-o.partner_y;
    int32_t radius=s.follow_radius;

    if(s.defend_leader&&o.enemy_valid){
        int32_t ex=o.enemy_x-o.partner_x,ey=o.enemy_y-o.partner_y;
        if(som_ai_distance_squared(ex,ey)<
           som_ai_distance_squared(dx,dy)){
            dx=ex;dy=ey;radius=s.attack_radius;out.targeting_enemy=1;
        }
    }
    if(som_ai_distance_squared(dx,dy)>(uint64_t)radius*(uint64_t)radius){
        if(dx>s.axis_deadzone)out.buttons|=SOM_PAD_RIGHT;
        else if(dx< -s.axis_deadzone)out.buttons|=SOM_PAD_LEFT;
        if(dy>s.axis_deadzone)out.buttons|=SOM_PAD_DOWN;
        else if(dy< -s.axis_deadzone)out.buttons|=SOM_PAD_UP;
    }
    /* SNES Secret of Mana's primary weapon attack uses B. This prototype
     * never guesses at spells, radial menus, or item usage. */
    if(o.enemy_valid&&o.can_attack&&
       som_ai_distance_squared(o.enemy_x-o.partner_x,
                               o.enemy_y-o.partner_y)<=
           (uint64_t)s.attack_radius*(uint64_t)s.attack_radius)
        out.buttons|=SOM_PAD_B;
    return out;
}
#endif
