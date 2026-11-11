/* Mock the front-end input callback; test the actual proxy input decisions. */
#include <assert.h>
#include <stdio.h>
#include "../src/som_ai_proxy.c"

static unsigned pressed;
static int16_t mocked_input(unsigned port,unsigned device,unsigned index,unsigned id){
    (void)index;
    return port==0&&device==RETRO_DEVICE_JOYPAD&&id==pressed?1:0;
}
static void test_reset(void){
    physical_state=mocked_input;
    frame_number=SOM_START_FRAME;
    armed=1;on_turbo=1;game_loaded=1;completed=0;cancelled=0;
    pressed=100; /* unassigned button */
}
int main(void){
    test_reset();
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==1);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)==0);
    assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    frame_number=SOM_START_FRAME-1;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    frame_number=SOM_START_FRAME+SOM_HOLD_FRAMES;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    test_reset();pressed=RETRO_DEVICE_ID_JOYPAD_B;
    som_input_poll();
    assert(cancelled);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
    test_reset();on_turbo=0;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    test_reset();armed=0;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    puts("PASS: Turbo proxy 30-frame RIGHT window, port isolation, fail-closed and manual override");
    return 0;
}
