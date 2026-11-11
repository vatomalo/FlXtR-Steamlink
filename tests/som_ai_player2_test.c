#include <assert.h>
#include <stdio.h>
#include "../src/som_ai_player2.h"

static SomAiObservation ready(void){
    SomAiObservation o={0};
    o.verified_state=1;o.in_game=1;o.partner_controllable=1;o.partner_alive=1;
    return o;
}

int main(void){
    SomAiStrategy s=som_ai_default_strategy();
    SomAiObservation o=ready();
    SomAiDecision d;

    /* First live milestone: follow leader without requiring an LLM. */
    o.leader_x=100;o.leader_y=100;o.partner_x=20;o.partner_y=20;
    d=som_ai_decide(o,s);
    assert(d.valid&&d.buttons==(SOM_PAD_RIGHT|SOM_PAD_DOWN));

    o.partner_x=200;o.partner_y=100;
    d=som_ai_decide(o,s);
    assert(d.buttons==SOM_PAD_LEFT);

    o.partner_x=92;o.partner_y=100;
    assert(som_ai_decide(o,s).buttons==0); /* follow radius */

    /* An approaching enemy is a higher-priority target. */
    o.partner_x=0;o.partner_y=0;o.leader_x=200;o.leader_y=0;
    o.enemy_valid=1;o.enemy_x=12;o.enemy_y=0;o.can_attack=1;
    d=som_ai_decide(o,s);
    assert(d.valid&&d.targeting_enemy&&d.buttons==SOM_PAD_B);

    o.enemy_x=50;o.can_attack=0;
    d=som_ai_decide(o,s);
    assert(d.targeting_enemy&&d.buttons==SOM_PAD_RIGHT);

    o.can_attack=1;o.enemy_x=6;
    d=som_ai_decide(o,s);
    assert((d.buttons&SOM_PAD_B)!=0);

    /* Invalid state must never issue phantom controller inputs. */
    o.verified_state=0;
    d=som_ai_decide(o,s);
    assert(!d.valid&&!d.buttons);
    o.verified_state=1;o.partner_controllable=0;
    assert(!som_ai_decide(o,s).valid);
    o.partner_controllable=1;o.in_game=0;
    assert(!som_ai_decide(o,s).valid);
    o.in_game=1;o.menu_open=1;
    assert(!som_ai_decide(o,s).valid);
    o.menu_open=0;o.partner_alive=0;
    assert(!som_ai_decide(o,s).valid);
    o.partner_alive=1;s.follow_radius=-1;
    assert(!som_ai_decide(o,s).valid);

    puts("PASS: Secret of Mana Player 2 follow/attack and fail-closed states");
    return 0;
}
