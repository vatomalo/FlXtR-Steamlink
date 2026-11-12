/* Test one-shot runtime triggering and actual Libretro callback decisions. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../src/som_ai_proxy.c"

static unsigned pressed;
static int16_t mocked_input(unsigned port,unsigned device,unsigned index,unsigned id){
    (void)index;
    return port==0&&device==RETRO_DEVICE_JOYPAD&&id==pressed?1:0;
}
static void test_reset(void){
    physical_state=mocked_input;physical_poll=NULL;
    frame_number=180;start_frame=0;injected_right_queries=0;
    armed=0;test_enabled=1;on_turbo=1;game_loaded=1;completed=0;cancelled=0;
    pressed=100; /* no pressed button */
}
int main(void){
    char path[]="/tmp/flxtr-ai-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    unlink(path);
    trigger_file=path;
    test_reset();
    som_check_trigger();
    assert(!armed);
    FILE *touch=fopen(path,"w");
    assert(touch!=NULL && fclose(touch)==0);
    /* Create a file, as an SSH touch command would. */
    frame_number=186;
    som_check_trigger();
    assert(armed && start_frame==186 && access(path,F_OK)!=0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==1);
    assert(injected_right_queries==1);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)==0);
    assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    frame_number=start_frame+SOM_HOLD_FRAMES-1;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==1);
    frame_number=start_frame+SOM_HOLD_FRAMES;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    test_reset();armed=1;start_frame=180;pressed=RETRO_DEVICE_ID_JOYPAD_B;
    som_input_poll();
    assert(cancelled);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
    test_reset();on_turbo=0;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    test_reset();test_enabled=0;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    puts("PASS: Turbo proxy one-shot trigger, 30-frame RIGHT, callback count, override");
    return 0;
}
