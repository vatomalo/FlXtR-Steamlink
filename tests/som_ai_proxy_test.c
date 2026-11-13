/* Runtime action tests without a RetroArch process. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/som_ai_proxy.c"

static unsigned pressed=100;
static uint8_t mocked_wram[SOM_WRAM_SIZE];
static int16_t mocked_input(unsigned port,unsigned device,unsigned index,unsigned id){
    (void)index;
    return port==0&&device==RETRO_DEVICE_JOYPAD&&id==pressed?1:0;
}
static void *mocked_memory_data(unsigned id){
    return id==RETRO_MEMORY_SYSTEM_RAM?mocked_wram:NULL;
}
static size_t mocked_memory_size(unsigned id){
    return id==RETRO_MEMORY_SYSTEM_RAM?sizeof(mocked_wram):0;
}
static const void *shown_frame;
static void mocked_video(const void *data,unsigned width,unsigned height,size_t pitch){
    (void)width;(void)height;(void)pitch;shown_frame=data;
}
static uint8_t saved_wram[SOM_WRAM_SIZE];
static int unserialized;
static size_t mocked_serialize_size(void){return sizeof(saved_wram);}
static bool mocked_serialize(void *data,size_t size){
    if(size!=sizeof(saved_wram))return false;
    memcpy(data,mocked_wram,size);return true;
}
static bool mocked_unserialize(const void *data,size_t size){
    if(size!=sizeof(saved_wram))return false;
    memcpy(mocked_wram,data,size);unserialized++;return true;
}
static void reset_proxy(void){
    physical_state=mocked_input;physical_poll=NULL;
    frame_number=180;start_frame=0;injected_queries=0;
    active_action=SOM_ACT_NONE;action_frames=0;
    armed=0;test_enabled=1;on_turbo=1;game_loaded=1;completed=0;cancelled=0;
    pressed=100;
}
int main(void){
    char path[]="/tmp/flxtr-ai-test-XXXXXX";
    char wram_path[]="/tmp/flxtr-wram-test-XXXXXX";
    char wram_tmp_path[]="/tmp/flxtr-wram-temp-test-XXXXXX";
    char ring_ai_path[]="/tmp/flxtr-ring-ai-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);close(fd);unlink(path);
    fd=mkstemp(wram_path);assert(fd>=0);close(fd);unlink(wram_path);
    fd=mkstemp(wram_tmp_path);assert(fd>=0);close(fd);unlink(wram_tmp_path);
    fd=mkstemp(ring_ai_path);assert(fd>=0);close(fd);unlink(ring_ai_path);
    reset_proxy();
    trigger_file=path;
    action_file=path;
    assert(som_parse_action("open_ring")==SOM_ACT_OPEN_RING);
    assert(som_action_button(SOM_ACT_OPEN_RING)==RETRO_DEVICE_ID_JOYPAD_Y);
    assert(som_action_frames(SOM_ACT_OPEN_RING)==2);
    assert(som_action_frames(SOM_ACT_RIGHT)==30);
    assert(som_parse_action("step_right")==SOM_ACT_STEP_RIGHT);
    assert(som_action_button(SOM_ACT_STEP_RIGHT)==RETRO_DEVICE_ID_JOYPAD_RIGHT);
    assert(som_action_frames(SOM_ACT_STEP_RIGHT)==6);
    assert(som_parse_action("wait")==SOM_ACT_WAIT);
    assert(som_parse_action("menu_x")==SOM_ACT_MENU_X);
    assert(som_action_button(SOM_ACT_MENU_X)==RETRO_DEVICE_ID_JOYPAD_X);
    assert(som_action_frames(SOM_ACT_MENU_X)==2);
    assert(som_action_frames(SOM_ACT_WAIT)==0);
    assert(som_parse_action("charge")==SOM_ACT_CHARGE);
    assert(som_action_button(SOM_ACT_CHARGE)==RETRO_DEVICE_ID_JOYPAD_B);
    assert(som_action_frames(SOM_ACT_CHARGE)==SOM_CHARGE_MAX_FRAMES);
    assert(!strcmp(som_action_name(SOM_ACT_CHARGE),"charge"));
    som_check_trigger();
    assert(!armed);
    FILE *touch=fopen(path,"w");
    assert(touch!=NULL && fputs("open_ring\n",touch)>0 && fclose(touch)==0);
    /* In real launches legacy and action files are different. */
    trigger_file="/tmp/nonexistent-som-ai-test-trigger";
    frame_number=186;
    som_check_trigger();
    assert(armed && active_action==SOM_ACT_OPEN_RING && access(path,F_OK)!=0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==1);
    assert(injected_queries==1);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
    frame_number=start_frame+action_frames;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
    /* snes9x2005 queries the whole pad with one bitmask when supported. */
    reset_proxy();som_start_action(SOM_ACT_STEP_DOWN);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==
           (1<<RETRO_DEVICE_ID_JOYPAD_DOWN));
    assert(injected_queries==1);
    assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    frame_number=start_frame+action_frames;
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    reset_proxy();som_start_action(SOM_ACT_WAIT);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    reset_proxy();som_start_action(SOM_ACT_RIGHT);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==1);
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(cancelled);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
    reset_proxy();on_turbo=0;
    som_start_action(SOM_ACT_LEFT);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)==0);
    backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    wram_file=wram_path;wram_temp_file=wram_tmp_path;
    for(size_t i=0;i<sizeof(mocked_wram);i++)mocked_wram[i]=(uint8_t)(i&0xffu);
    assert(som_export_wram());
    assert(access(wram_tmp_path,F_OK)!=0);
    FILE *wram=fopen(wram_path,"rb");
    assert(wram!=NULL);
    uint8_t first[3]={0};
    assert(fread(first,1,sizeof(first),wram)==sizeof(first));
    assert(first[0]==0&&first[1]==1&&first[2]==2);
    assert(fseek(wram,0,SEEK_END)==0&&ftell(wram)==(long)SOM_WRAM_SIZE);
    assert(fclose(wram)==0);
    on_turbo=1;
    ring_ai_file=ring_ai_path;
    ring_ai_selector_open=ring_ai_pending=ring_ai_enabled=0;
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0x007D]=1;mocked_wram[0xA20F]=4;
    mocked_wram[0xA212]=0x20;mocked_wram[0xA136]=7;
    som_ring_ai_tick();
    assert(ring_ai_selector_open&&!ring_ai_pending);
    mocked_wram[0xA212]=0;som_ring_ai_tick();
    assert(!ring_ai_selector_open&&ring_ai_pending);
    mocked_wram[0x007D]=0;som_ring_ai_tick();
    assert(ring_ai_enabled&&!ring_ai_pending&&access(ring_ai_path,F_OK)==0);
    ring_ai_grace_until=0;pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);
    pressed=100;mocked_wram[0x007D]=1;mocked_wram[0xA20F]=4;
    mocked_wram[0xA212]=0x20;mocked_wram[0xA136]=2;som_ring_ai_tick();
    mocked_wram[0xA212]=0;mocked_wram[0xA136]=3;som_ring_ai_tick();
    mocked_wram[0x007D]=0;som_ring_ai_tick();
    assert(!ring_ai_pending&&!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);
    /* A newer planner command preempts an unfinished hold. */
    reset_proxy();trigger_file="/tmp/nonexistent-som-ai-test-trigger";
    som_start_action(SOM_ACT_RIGHT);
    FILE *steer=fopen(path,"w");
    assert(steer!=NULL&&fputs("step_up\n",steer)>0&&fclose(steer)==0);
    frame_number=182;som_check_trigger();
    assert(armed&&active_action==SOM_ACT_STEP_UP&&start_frame==182);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP)==1);
    steer=fopen(path,"w");
    assert(steer!=NULL&&fputs("wait\n",steer)>0&&fclose(steer)==0);
    frame_number=184;som_check_trigger();
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP)==0);
    FILE *manual=fopen(path,"w");
    assert(manual!=NULL&&fputs("ai_on\n",manual)>0&&fclose(manual)==0);
    frame_number=180;som_check_trigger();
    assert(ring_ai_enabled&&access(ring_ai_path,F_OK)==0);
    manual=fopen(path,"w");
    assert(manual!=NULL&&fputs("ai_off\n",manual)>0&&fclose(manual)==0);
    som_check_trigger();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);
    reset_proxy();autonomous_mode=1;auto_deadline=frame_number+600;
    auto_step=0;start_frame=0;action_frames=0;
    som_auto_tick();
    assert(armed && active_action==SOM_ACT_RIGHT);
    assert(auto_step==1);
    frame_number=auto_deadline;
    som_auto_tick();
    assert(!autonomous_mode);
    reset_proxy();autonomous_mode=1;
    pressed=RETRO_DEVICE_ID_JOYPAD_B;
    som_input_poll();
    assert(!autonomous_mode);
    /* Bridge messages are drawn on a private copy of the frame only. */
    reset_proxy();
    char message_path[]="/tmp/flxtr-message-test-XXXXXX";
    fd=mkstemp(message_path);assert(fd>=0);close(fd);
    message_file=message_path;frontend_video=mocked_video;pixel_format=2;
    static uint16_t core_frame[224][256];
    som_video_refresh(core_frame,256,224,512);
    assert(shown_frame==core_frame);
    FILE *note=fopen(message_path,"w");
    assert(note!=NULL&&fputs("AI: using Candy|line two\n",note)>0&&fclose(note)==0);
    frame_number=SOM_MESSAGE_POLL_FRAMES*20u;som_check_message();
    assert(access(message_path,F_OK)!=0&&overlay.line_count==2);
    assert(!strcmp(overlay.lines[0],"AI: using Candy"));
    som_video_refresh(core_frame,256,224,512);
    assert(shown_frame!=core_frame&&shown_frame==overlay_frame);
    size_t lit=0;
    for(size_t i=0;i<256u*224u;i++)lit+=((const uint16_t*)shown_frame)[i]!=0;
    assert(lit>1000);
    for(size_t i=0;i<256u*224u;i++)assert(((uint16_t*)core_frame)[i]==0);
    overlay.frames_left=0;
    som_video_refresh(core_frame,256,224,512);
    assert(shown_frame==core_frame);
    unlink(message_path);
    /* Teaching records physical press edges as replayable action names. */
    reset_proxy();
    char record_path[]="/tmp/flxtr-record-test-XXXXXX";
    fd=mkstemp(record_path);assert(fd>=0);close(fd);unlink(record_path);
    record_file=record_path;recording=1;last_physical_buttons=0;
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();som_input_poll();
    pressed=RETRO_DEVICE_ID_JOYPAD_DOWN;som_input_poll();
    pressed=100;som_input_poll();
    pressed=RETRO_DEVICE_ID_JOYPAD_A;som_input_poll();
    recording=0;pressed=RETRO_DEVICE_ID_JOYPAD_X;som_input_poll();
    FILE *recorded=fopen(record_path,"r");
    assert(recorded!=NULL);
    char recorded_text[64]={0};
    assert(fread(recorded_text,1,sizeof(recorded_text)-1,recorded)>0);
    fclose(recorded);
    assert(!strcmp(recorded_text,"confirm\nstep_down\ncancel\n"));
    assert(!armed);
    unlink(record_path);
    /* Experimental, undoable monster copy into an empty slot. */
    backend_retro_serialize_size=mocked_serialize_size;
    backend_retro_serialize=mocked_serialize;
    backend_retro_unserialize=mocked_unserialize;
    memset(mocked_wram,0,sizeof(mocked_wram));
    assert(!som_clone_enemy());                       /* nothing to copy */
    uint8_t *rabite=mocked_wram+0xE600;
    rabite[0]=1;rabite[0x180]=0;rabite[2]=0x40;rabite[3]=0x01;rabite[4]=0x80;
    for(unsigned slot=1;slot<3;slot++)mocked_wram[0xE600+slot*0x200]=1; /* all busy */
    mocked_wram[0xE800+0x180]=0x90;mocked_wram[0xEA00+0x180]=0x90;
    assert(!som_clone_enemy()&&!undo_ready);         /* no empty slot */
    mocked_wram[0xEA00]=0;                            /* free the last slot */
    uint8_t before_clone[SOM_WRAM_SIZE];
    memcpy(before_clone,mocked_wram,sizeof(before_clone));
    assert(som_clone_enemy()&&undo_ready);
    uint8_t *copy=mocked_wram+0xEA00;
    assert(copy[0]==1&&copy[0x180]==0&&copy[4]==0x80);
    assert((copy[2]|copy[3]<<8)==0x0140+32);
    assert(memcmp(mocked_wram+0xE800,before_clone+0xE800,0x200)==0); /* others untouched */
    assert(som_undo_spawn()&&unserialized==1&&!undo_ready);
    assert(memcmp(mocked_wram,before_clone,sizeof(before_clone))==0);
    assert(!som_undo_spawn());                        /* only once */
    /* Load-time ROM record patches touch memory only and verify the byte. */
    char patch_path[]="/tmp/flxtr-rom-patch-test-XXXXXX";
    fd=mkstemp(patch_path);assert(fd>=0);
    const char *patch_text="patch 087005 2a 00\npatch 087015 2a 00\npatch ffffff 00 01\njunk\n";
    assert(write(fd,patch_text,strlen(patch_text))==(ssize_t)strlen(patch_text));close(fd);
    rom_patch_file=patch_path;
    static uint8_t rom[0x100000+512];
    memset(rom,0,sizeof(rom));
    rom[512+0x087005]=0x2a;rom[512+0x087015]=0x2b;         /* second byte differs */
    assert(som_apply_rom_patches(rom,sizeof(rom))==1);
    assert(rom[512+0x087005]==0x00&&rom[512+0x087015]==0x2b);
    static uint8_t bare_rom[0x100000];
    memset(bare_rom,0,sizeof(bare_rom));bare_rom[0x087005]=0x2a;
    assert(som_apply_rom_patches(bare_rom,sizeof(bare_rom))==1&&bare_rom[0x087005]==0);
    unlink(patch_path);
    assert(som_apply_rom_patches(bare_rom,sizeof(bare_rom))==0);
    unlink(path);unlink(wram_path);unlink(wram_tmp_path);unlink(ring_ai_path);
    puts("PASS: Turbo actions, tile steps, ring AI signal, override, and WRAM export");
    return 0;
}
