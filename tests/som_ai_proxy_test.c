/* Runtime action tests without a RetroArch process. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/som_ai_proxy.c"

static unsigned pressed=100,pressed_too=100;
static uint8_t mocked_wram[SOM_WRAM_SIZE];
static int16_t mocked_input(unsigned port,unsigned device,unsigned index,unsigned id){
    (void)index;
    return port==0&&device==RETRO_DEVICE_JOYPAD&&(id==pressed||id==pressed_too)?1:0;
}
static uint16_t mocked_buttons;
static int16_t mocked_input_mask(unsigned port,unsigned device,unsigned index,unsigned id){
    (void)index;
    if(port!=0||device!=RETRO_DEVICE_JOYPAD)return 0;
    if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)mocked_buttons;
    return id<16&&(mocked_buttons&(uint16_t)(1u<<id))?1:0;
}
static uint8_t mocked_vram[0x10000];
static int mocked_has_vram;
static void *mocked_memory_data(unsigned id){
    if(id==RETRO_MEMORY_VIDEO_RAM)return mocked_has_vram?mocked_vram:NULL;
    return id==RETRO_MEMORY_SYSTEM_RAM?mocked_wram:NULL;
}
static size_t mocked_memory_size(unsigned id){
    if(id==RETRO_MEMORY_VIDEO_RAM)return mocked_has_vram?sizeof(mocked_vram):0;
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
    disc_file="/nonexistent/flxtr-disciplines";
    physical_state=mocked_input;physical_poll=NULL;
    frame_number=180;start_frame=0;injected_queries=0;
    active_action=SOM_ACT_NONE;action_frames=0;
    armed=0;test_enabled=1;on_turbo=1;game_loaded=1;completed=0;cancelled=0;
    pressed=100;
    memset(&overlay,0,sizeof(overlay));
    switch_holding=switch_spoiled=0;
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
    /* Manual-play Class Progress menu via L2+R2. */
    reset_proxy();
    som_disc_init(&disciplines);
    som_manual_disciplines=0;
    ring_ai_enabled=0;
    last_physical_buttons=0;
    physical_state=mocked_input_mask;
    mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                               (1u<<RETRO_DEVICE_ID_JOYPAD_R2));
    som_input_poll();
    assert(som_manual_disciplines&&disciplines.open&&!ring_ai_enabled);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN);som_input_poll();
    assert(disciplines.cursor==1);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN)==0);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
    assert(disciplines.selected==1);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);som_input_poll();
    assert(!som_manual_disciplines&&!disciplines.open);
    mocked_buttons=0;
    /* Start is the new manual Class Progress shortcut. */
    som_manual_disciplines=0;disciplines.open=0;last_physical_buttons=0;
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);
    som_input_poll();
    assert(som_manual_disciplines&&disciplines.open);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
    som_input_poll();
    assert(!som_manual_disciplines&&!disciplines.open);
    assert(som_disc_weapons[0][0]==1&&som_disc_weapons[0][1]==2);
    assert(som_disc_weapons[1][0]==0&&som_disc_weapons[1][1]==3);
    assert(som_disc_weapons[2][0]==1&&som_disc_weapons[2][1]==6);
    assert(som_disc_weapons[10][0]==1&&som_disc_weapons[10][1]==5);
    /* Grant only missing weapons on selection, retaining tiers/ownership. */
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;
    for(unsigned i=0;i<8;i++)mocked_wram[0xCC54+i]=0xFF;
    mocked_wram[0xCC55]=0x43; /* already-owned tier-3 sword */
    assert(som_class_supply(mocked_wram,2)==1); /* Ninja gains chakram */
    assert(mocked_wram[0xCC55]==0x43&&mocked_wram[0xCC5A]==0);
    assert(som_class_supply(mocked_wram,2)==0); /* idempotent */
    assert(som_class_supply(mocked_wram,10)==1); /* Samurai gains yumi */
    assert(mocked_wram[0xCC59]==0&&mocked_wram[0xCC55]==0x43);
    mocked_wram[0xE4]=0;mocked_wram[0xCC56]=0xFF;
    assert(som_class_supply(mocked_wram,0)==0); /* no changes outside field */
    assert(mocked_wram[0xCC56]==0xFF);

    /* Four distinct sword swings with observed HP loss queue the finisher.
     * Button presses without damage must not count. */
    backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;
    mocked_wram[0xE6]=0x0F;mocked_wram[0xE1E4]=1;
    mocked_wram[0xE19C]=1;
    mocked_wram[0xE600]=1;mocked_wram[0xE780]=7;
    mocked_wram[0xE782]=100;mocked_wram[0xE783]=0;
    som_combo_clear();frame_number=500;
    som_combo_frame();
    for(unsigned h=0;h<4;h++){
        frame_number+=20;som_combo_swing();
        mocked_wram[0xE782]--;
        som_combo_frame();
        assert(combo_count==(h==3?0:h+1));
    }
    assert(combo_queued);
    combo_input_held=0;armed=0;som_combo_frame();
    assert(combo_charging);
    mocked_wram[0xE19B]=1;frame_number++;som_combo_frame();
    assert(!combo_charging&&!combo_queued);
    som_combo_clear();mocked_wram[0xE19B]=0;
    frame_number+=20;som_combo_swing();som_combo_frame();
    assert(!combo_count);
    frame_number+=SOM_COMBO_HIT_WINDOW+1;som_combo_frame();
    assert(!combo_pending&&!combo_count);
    memset(mocked_wram,0,sizeof(mocked_wram));
    physical_state=mocked_input;
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
    /* A press while the AI plays opens the gambit menu instead of taking
     * control; the menu's Exit entry gives the controls back. */
    char gambit_path[]="/tmp/flxtr-gambits-test-XXXXXX";
    {int gfd=mkstemp(gambit_path);assert(gfd>=0);close(gfd);unlink(gambit_path);}
    gambit_file=gambit_path;som_gambit_defaults_into(&gambits);gambits.open=0;
    ring_ai_grace_until=0;pressed=100;som_input_poll();             /* buttons up */
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(ring_ai_enabled&&gambits.open&&access(ring_ai_path,F_OK)==0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0); /* not to game */
    pressed=100;som_input_poll();
    pressed=RETRO_DEVICE_ID_JOYPAD_DOWN;som_input_poll();pressed=100;som_input_poll();
    pressed=RETRO_DEVICE_ID_JOYPAD_RIGHT;som_input_poll();pressed=100;som_input_poll();
    assert(ring_ai_enabled&&gambits.weight[1]==4);                       /* inn 3 -> 4 */
    pressed=RETRO_DEVICE_ID_JOYPAD_LEFT;som_input_poll();pressed=100;som_input_poll();
    pressed=RETRO_DEVICE_ID_JOYPAD_LEFT;som_input_poll();pressed=100;som_input_poll();
    assert(gambits.weight[1]==2);
    {FILE *g=fopen(gambit_path,"r");char t[16]={0};assert(g&&fgets(t,sizeof(t),g)&&fclose(g)==0);
     assert(!strcmp(t,"423341\n"));}
    {SomGambits loaded;som_gambit_parse(&loaded,"423341\n");assert(loaded.weight[1]==2);
     som_gambit_parse(&loaded,"9x");assert(loaded.weight[1]==3);}       /* bad: defaults */
    pressed=RETRO_DEVICE_ID_JOYPAD_A;som_input_poll();pressed=100;som_input_poll();
    assert(ring_ai_enabled&&!gambits.open);                               /* A: close */
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();pressed=100;som_input_poll();
    pressed=RETRO_DEVICE_ID_JOYPAD_UP;som_input_poll();pressed=100;som_input_poll();
    assert(gambits.cursor==SOM_GAMBIT_COUNT);                             /* wraps to Exit */
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(!ring_ai_enabled&&!gambits.open&&access(ring_ai_path,F_OK)!=0);
    assert(strstr(overlay.lines[0],"You play"));
    unlink(gambit_path);
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
    assert(clone_no_slot);                            /* retryable */
    /* Waiting request: fires on the first frame a slot is free. */
    frame_number=1000;clone_pending_until=frame_number+SOM_CLONE_WAIT_FRAMES;
    som_clone_tick();assert(clone_pending_until&&!undo_ready);
    mocked_wram[0xEA00]=0;                            /* free the last slot */
    /* Per-slot drawing setup: slot 3's own fields, slot 1's tiles and colours. */
    rabite[0x8B]=0x2A;rabite[0xF8]=0xA2;rabite[0x92]=0x40;
    mocked_wram[0xEA00+0x8B]=0x2E;mocked_wram[0xEA00+0xF8]=0xE2;mocked_wram[0xEA00+0x92]=0xA0;
    memset(mocked_wram+0x5000,0x11,0x1000);memset(mocked_wram+0x7000,0,0x1000);
    memset(mocked_wram+0xDFA0,0x22,0x20);memset(mocked_wram+0xDFE0,0,0x20);
    uint8_t before_clone[SOM_WRAM_SIZE];
    memcpy(before_clone,mocked_wram,sizeof(before_clone));
    frame_number+=1;som_clone_tick();
    assert(!clone_pending_until&&undo_ready);
    uint8_t *copy=mocked_wram+0xEA00;
    assert(copy[0]==1&&copy[0x180]==0&&copy[4]==0x80);
    assert((copy[2]|copy[3]<<8)==0x0140+32);
    assert(copy[0x8B]==0x2E&&copy[0xF8]==0xE2&&copy[0x92]==0xA0);   /* slot 3's own */
    assert(mocked_wram[0x7000]==0x11&&mocked_wram[0x7FFF]==0x11);  /* tiles copied */
    assert(mocked_wram[0xDFE0]==0x22&&mocked_wram[0xDFFF]==0x22);  /* palette copied */
    assert(mocked_wram[0xDFC0]==0&&mocked_wram[0x6000]==0);        /* slot 2 untouched */
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
    /* Living world: native NPC walk (ACTOR_WALK write set, all gates). */
    assert(som_fnv1a((const uint8_t*)"a",1)==0xe40c292cu);
    assert(!som_npc_rom_verified(bare_rom,sizeof(bare_rom)));
    assert(!som_npc_rom_verified(rom,sizeof(rom)));
    assert(!som_npc_rom_verified(NULL,0));
    SomNpcWalk walk;
    assert(som_npc_parse_walk("npc_walk 131 8 199 472 404 3 16",&walk));
    assert(walk.map==131&&walk.slot==8&&walk.kind==0xC7&&walk.x==472&&walk.y==404&&
           walk.dir==3&&walk.frames==16);
    assert(!som_npc_parse_walk("npc_walk 131 8 199 472 404 3 16 9",&walk));
    assert(!som_npc_parse_walk("npc_walk 131 8 199 472 404 3 16 x",&walk));
    assert(!som_npc_parse_walk("npc_walk 131 8",&walk));
    assert(!som_npc_parse_walk("step_left",&walk));
    assert(som_npc_parse_walk("npc_walk 131 8 199 472 404 3 16",&walk));
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0x00E4]=1;mocked_wram[0x00E6]=0x0F;
    mocked_wram[0x00DC]=131;
    uint8_t *npc=mocked_wram+0xF000;               /* slot 8 */
    npc[0]=1;npc[0x3D]=0xC7;npc[0x44]=0xC0;npc[0x71]=0x0A;npc[0x30]=0x01;
    npc[2]=472&0xFF;npc[3]=472>>8;npc[4]=404&0xFF;npc[5]=404>>8;
    assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk)==NULL);
    SomNpcWalk bad=walk;bad.slot=2;
    assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad)!=NULL); /* hero slot */
    bad=walk;bad.slot=12;assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad)!=NULL);
    bad=walk;bad.kind=0x50;assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad)!=NULL);
    bad=walk;bad.frames=0;assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad)!=NULL);
    bad=walk;bad.frames=33;assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad)!=NULL);
    bad=walk;bad.dir=4;assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad)!=NULL);
    bad=walk;bad.x=473;assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad),"stale position"));
    bad=walk;bad.map=130;assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&bad),"different map"));
    assert(som_npc_walk_refusal(mocked_wram,0x1000,&walk)!=NULL);
    npc[0x44]=0x40;   /* wandering NPC: its native script would compete */
    assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk),"NPC has native AI (not static)"));
    npc[0x44]=0xC0;
    mocked_wram[0x00D0]=0x81;   /* dialogue running */
    assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk),"event running"));
    mocked_wram[0x00D0]=0;
    mocked_wram[0x003A]=0x00;mocked_wram[0x003B]=0xF0;  /* talk target is this NPC */
    assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk),"NPC is the talk target"));
    mocked_wram[0x003B]=0;
    mocked_wram[0x00E4]=0;
    assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk),"map transition"));
    mocked_wram[0x00E4]=1;
    mocked_wram[0x00FF]=0x40;
    assert(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk)!=NULL);
    mocked_wram[0x00FF]=0;
    npc[0x3D]=0xC6;assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk),"kind changed"));
    npc[0x3D]=0xC7;
    uint8_t before_walk[SOM_WRAM_SIZE];
    memcpy(before_walk,mocked_wram,sizeof(before_walk));
    som_npc_apply_walk(mocked_wram,&walk);
    assert(npc[0x0A]==16&&npc[0x42]==1&&npc[0x06]==0x82&&npc[0x07]==0&&npc[0x10]==0x82&&
           npc[0x30]==0xFF&&npc[0x11]==1&&npc[0x1C]==0&&npc[0x13]==0&&npc[0x67]==0);
    {   /* nothing outside the ten ACTOR_WALK fields of this object changes */
        static const unsigned fields[]={0x06,0x07,0x0A,0x10,0x11,0x13,0x1C,0x30,0x42,0x67};
        for(size_t i=0;i<sizeof(mocked_wram);i++){
            int field=0;
            for(size_t f=0;f<sizeof(fields)/sizeof(fields[0]);f++)if(i==0xF000u+fields[f])field=1;
            if(!field)assert(mocked_wram[i]==before_walk[i]);
        }
    }
    assert(!strcmp(som_npc_walk_refusal(mocked_wram,sizeof(mocked_wram),&walk),"NPC busy"));
    memcpy(mocked_wram,before_walk,sizeof(before_walk));
    static const uint8_t want_vx[4]={0,0,2,0x82},want_vy[4]={0x82,2,0,0},want_face[4]={0,1,2,0x82};
    for(unsigned dir=0;dir<4;dir++){
        memcpy(mocked_wram,before_walk,sizeof(before_walk));
        SomNpcWalk turn=walk;turn.dir=dir;
        som_npc_apply_walk(mocked_wram,&turn);
        assert(npc[0x06]==want_vx[dir]&&npc[0x07]==want_vy[dir]&&npc[0x10]==want_face[dir]);
    }
    memcpy(mocked_wram,before_walk,sizeof(before_walk));
    npc[0x71]=0x40;   /* presentation flag 0x40: facing 0 and no walk pose, as ACTOR_WALK */
    som_npc_apply_walk(mocked_wram,&walk);
    assert(npc[0x10]==0&&npc[0x11]==0);
    memcpy(mocked_wram,before_walk,sizeof(before_walk));
    npc[0x71]=0x20;som_npc_apply_walk(mocked_wram,&walk);
    assert(npc[0x10]==0x82&&npc[0x11]==0);
    memcpy(mocked_wram,before_walk,sizeof(before_walk));
    /* The command path: ROM, enable marker and rate limit all gate it. */
    char enable_path[]="/tmp/flxtr-npc-enable-XXXXXX";
    fd=mkstemp(enable_path);assert(fd>=0);close(fd);unlink(enable_path);
    npc_enable_file=enable_path;
    reset_proxy();npc_have_walked=0;
    npc_rom_verified=0;
    assert(!som_npc_walk_command("npc_walk 131 8 199 472 404 3 16"));
    npc_rom_verified=1;
    assert(!som_npc_walk_command("npc_walk 131 8 199 472 404 3 16")); /* not enabled */
    assert(memcmp(mocked_wram,before_walk,sizeof(before_walk))==0);
    fd=open(enable_path,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
    assert(!som_npc_walk_command("npc_walk 131 8 199 472 404"));
    assert(som_npc_walk_command("npc_walk 131 8 199 472 404 3 16"));
    assert(npc[0x0A]==16);
    memcpy(mocked_wram,before_walk,sizeof(before_walk));
    frame_number+=2;
    assert(!som_npc_walk_command("npc_walk 131 8 199 472 404 3 16")); /* rate limit */
    frame_number+=SOM_NPC_MIN_GAP_FRAMES;
    assert(som_npc_walk_command("npc_walk 131 8 199 472 404 1 8"));
    /* The command file is consumed once and never touches the action file. */
    char npc_cmd_path[]="/tmp/flxtr-npc-cmd-XXXXXX";
    fd=mkstemp(npc_cmd_path);assert(fd>=0);close(fd);
    npc_command_file=npc_cmd_path;
    memcpy(mocked_wram,before_walk,sizeof(before_walk));
    frame_number+=SOM_NPC_MIN_GAP_FRAMES*2u;frame_number&=~1ul;
    FILE *cmd=fopen(npc_cmd_path,"w");assert(cmd);fputs("npc_walk 131 8 199 472 404 2 4\n",cmd);fclose(cmd);
    som_check_npc_command();
    assert(access(npc_cmd_path,F_OK)!=0&&npc[0x0A]==4&&npc[0x06]==2&&active_action==SOM_ACT_NONE);
    unlink(enable_path);
    /* Idle auto-start: one minute without physical input raises the AI
     * signal once; a press stops it and starts a new idle period. */
    reset_proxy();backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    memset(mocked_wram,0,sizeof(mocked_wram));
    ring_ai_file=ring_ai_path;unlink(ring_ai_path);
    ring_ai_enabled=ring_ai_pending=0;
    char idle_off_path[]="/tmp/flxtr-idle-off-test-XXXXXX";
    fd=mkstemp(idle_off_path);assert(fd>=0);close(fd);unlink(idle_off_path);
    idle_disable_file=idle_off_path;
    frame_number=SOM_MESSAGE_POLL_FRAMES*1000ul;last_input_frame=frame_number;idle_started=0;
    gambit_file="/nonexistent/flxtr-gambits";gambits.open=0;gambits.cursor=0;
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();pressed=100;
    assert(last_input_frame==frame_number);
    frame_number+=SOM_IDLE_AUTOSTART_FRAMES-SOM_MESSAGE_POLL_FRAMES;som_idle_tick();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);      /* 59.75 s: not yet */
    frame_number+=SOM_MESSAGE_POLL_FRAMES;
    mocked_wram[0x007D]=1;som_idle_tick();
    assert(!ring_ai_enabled&&!idle_started);                     /* ring open: wait */
    mocked_wram[0x007D]=0;som_input_poll();som_idle_tick();       /* no buttons held */
    assert(ring_ai_enabled&&idle_started&&access(ring_ai_path,F_OK)==0);
    frame_number+=1;pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();pressed=100;
    assert(ring_ai_enabled);                                     /* 30-frame grace */
    frame_number+=40;pressed=RETRO_DEVICE_ID_JOYPAD_UP;som_input_poll();pressed=100;
    assert(ring_ai_enabled&&gambits.open);                       /* press: gambit menu */
    frame_number+=1;som_input_poll();
    frame_number+=1;pressed=RETRO_DEVICE_ID_JOYPAD_UP;som_input_poll();pressed=100;
    frame_number+=1;som_input_poll();
    frame_number+=1;pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();pressed=100;
    assert(!ring_ai_enabled&&!idle_started&&access(ring_ai_path,F_OK)!=0); /* Exit */
    /* Bridge cleared the marker itself: no restart within the same period. */
    frame_number=(frame_number/SOM_MESSAGE_POLL_FRAMES+1)*SOM_MESSAGE_POLL_FRAMES;
    last_input_frame=frame_number;
    frame_number+=SOM_IDLE_AUTOSTART_FRAMES;som_idle_tick();
    assert(ring_ai_enabled);
    ring_ai_enabled=0;unlink(ring_ai_path);
    frame_number+=SOM_IDLE_AUTOSTART_FRAMES;som_idle_tick();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);
    /* ai_off keeps it off until a physical press. */
    pressed=RETRO_DEVICE_ID_JOYPAD_A;som_input_poll();pressed=100;
    FILE *off=fopen(path,"w");assert(off&&fputs("ai_off\n",off)>0&&fclose(off)==0);
    action_file=path;frame_number+=SOM_TRIGGER_POLL_FRAMES-frame_number%SOM_TRIGGER_POLL_FRAMES;
    som_check_trigger();assert(idle_started);
    frame_number+=SOM_IDLE_AUTOSTART_FRAMES*2;frame_number-=frame_number%SOM_MESSAGE_POLL_FRAMES;
    som_idle_tick();assert(!ring_ai_enabled);
    /* Disable marker. */
    pressed=RETRO_DEVICE_ID_JOYPAD_A;som_input_poll();pressed=100;
    fd=open(idle_off_path,O_WRONLY|O_CREAT,0600);assert(fd>=0);close(fd);
    frame_number+=SOM_IDLE_AUTOSTART_FRAMES;frame_number-=frame_number%SOM_MESSAGE_POLL_FRAMES;
    som_idle_tick();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);
    unlink(idle_off_path);
    /* R2 or R3 held alone for one second, then released: AI on (solo hero,
     * no Action Grid; Select is RetroArch's hotkey button on the Steam Link).
     * Short holds, other buttons and holds begun while the AI runs never
     * start it; any other button still stops it. */
    reset_proxy();backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    ring_ai_file=ring_ai_path;unlink(ring_ai_path);
    frame_number=500000ul;
    pressed=RETRO_DEVICE_ID_JOYPAD_R3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES-1;som_input_poll();
    pressed=100;som_input_poll();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);      /* too short */
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_R3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES;
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();           /* another button */
    pressed=100;frame_number+=1;som_input_poll();
    assert(!ring_ai_enabled);
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_R2;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES;som_input_poll();
    frame_number+=1;pressed=100;som_input_poll();
    assert(ring_ai_enabled&&access(ring_ai_path,F_OK)==0);       /* long hold: on */
    assert(overlay.frames_left>0&&strstr(overlay.lines[0],"AI ON"));
    frame_number+=40;pressed=RETRO_DEVICE_ID_JOYPAD_R3;som_input_poll();
    assert(ring_ai_enabled&&gambits.open);                       /* press: gambit menu */
    frame_number+=SOM_SWITCH_HOLD_FRAMES*2;som_input_poll();
    frame_number+=1;pressed=100;som_input_poll();
    assert(ring_ai_enabled&&gambits.open);     /* a long hold while it plays: no toggle */
    frame_number+=1;pressed=RETRO_DEVICE_ID_JOYPAD_UP;som_input_poll();
    frame_number+=1;pressed=100;som_input_poll();
    frame_number+=1;pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    frame_number+=1;pressed=100;som_input_poll();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);      /* Exit: you play */
    assert(strstr(overlay.lines[0],"You play"));
    /* The bridge removing the marker gives the controls back too. */
    assert(som_write_ring_ai_marker());ring_ai_enabled=1;ring_ai_grace_until=0;
    unlink(ring_ai_path);
    frame_number=(frame_number/30u+1u)*30u;som_input_poll();
    assert(!ring_ai_enabled&&strstr(overlay.lines[0],"AI stopped"));
    /* Select (RetroArch's hotkey button here) is not the switch. */
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_SELECT;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES*2;som_input_poll();
    frame_number+=1;pressed=100;som_input_poll();
    assert(!ring_ai_enabled);
    /* Gambit menu text and drawing. */
    {
        SomGambits g={1,1,{0,5,3,3,4,1}};
        char text[33];
        assert(som_gambit_line(&g,0,text)&&strstr(text,"GAMBITS"));
        assert(som_gambit_line(&g,1,text)&&strstr(text,"Fight monsters")&&strstr(text,"0 ....."));
        assert(som_gambit_line(&g,2,text)&&text[0]=='>'&&strstr(text,"5 ooooo"));
        assert(strlen(text)<=32);
        assert(som_gambit_line(&g,SOM_GAMBIT_ITEMS,text)&&strstr(text,"Exit gambits"));
        assert(!som_gambit_line(&g,SOM_GAMBIT_ITEMS+1,text));
        static uint16_t frame[224][256];
        memset(frame,0,sizeof(frame));
        som_gambit_draw((uint8_t*)frame,256,224,512,2,&g);
        assert(frame[10][10]!=0);                                 /* menu box drawn */
        g.open=0;memset(frame,0,sizeof(frame));
        som_gambit_draw((uint8_t*)frame,256,224,512,2,&g);
        assert(frame[10][10]==0);
    }
    /* Familiar: fights the nearest monster with the player's rules, never
     * targets heroes, follows the hero when idle, goes when dead/map changes. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));
        ram[0xDC]=131;
        uint8_t *hero=ram+0xE000,*me=ram+0xE600,*foe=ram+0xE800;
        hero[0]=1;som_fam_put16(hero+2,100);som_fam_put16(hero+4,100);
        me[0]=1;me[0x180]=0;som_fam_put16(me+2,100);som_fam_put16(me+4,130);
        me[0x1AC]=0;me[0x1ED]=0;me[6]=0x82;
        foe[0]=1;foe[0x180]=0;som_fam_put16(foe+2,140);som_fam_put16(foe+4,130);
        SomFamiliar f={0,-1,0,131};
        assert(som_familiar_tick(&f,ram,1000));
        assert(me[0x1AC]==0xFF&&me[0x1ED]==0x7F&&me[6]==0);      /* no aggression */
        assert(f.target==1&&som_fam_u16(me+2)==102);             /* closes in, 2 px */
        som_fam_put16(me+0x184,40);me[0x182]=12;me[0x1F1]=30;      /* hero hit it */
        som_familiar_tick(&f,ram,1000);
        assert(me[0x1F1]==0&&som_fam_u16(me+0x182)==40);           /* immune, healed */
        for(unsigned long t=1001;t<1020;t++)som_familiar_tick(&f,ram,t);
        assert(foe[0x1F1]==SOM_FAMILIAR_DAMAGE);                   /* one paced strike */
        for(unsigned long t=1020;t<1036+1001;t++)som_familiar_tick(&f,ram,t);
        assert(foe[0x1F1]>=2*SOM_FAMILIAR_DAMAGE);
        foe[0]=0;                                                  /* foe gone */
        som_fam_put16(me+2,200);som_fam_put16(me+4,200);
        som_familiar_tick(&f,ram,5000);
        assert(f.target==-1&&som_fam_u16(me+2)==198&&som_fam_u16(me+4)==198); /* follows */
        /* Carried to the next map: saved, then restored into an empty slot
         * next to the hero with that slot's own drawing fields. */
        static SomFamiliarCarry carry;
        memset(ram+0x5000,0x33,0x1000);memset(ram+0xDFA0,0x44,0x20);
        som_fam_put16(me+0x184,40);me[0x8B]=0x2A;
        som_familiar_save(&carry,ram,0);
        ram[0xDC]=130;                                             /* map changed */
        assert(som_familiar_tick(&f,ram,5001)==SOM_FAMILIAR_LEFT_MAP&&f.slot==-1);
        memset(ram+0xE600,0,0x600);                                /* new map, slots */
        ram[0xE600]=1;ram[0xE600+0x3D]=0xC7;                       /* villager in slot 1 */
        ram[0xE800+0x8B]=0x2C;ram[0xE800+0xF8]=0xC2;               /* slot 2's fields */
        assert(som_familiar_restore(&carry,ram,NULL)==1);
        uint8_t *back=ram+0xE800;
        assert(back[0]==1&&back[0x8B]==0x2C&&back[0xF8]==0xC2);    /* own fields kept */
        assert(ram[0x6000]==0x33&&ram[0xDFC0]==0x44);              /* tiles, colours */
        assert(som_fam_u16(back+2)==som_fam_u16(ram+0xE002)+24u);  /* by the hero */
        assert(ram[0xE600+0x3D]==0xC7);                            /* villager kept */
        /* VRAM capture: its exact sprite tiles and palette go with it and
         * are written back with its home layout on the next map. */
        {
            static uint8_t vram[0x10000],slots_before[0x600],pal_before[0x20];
            static SomFamiliarCarry vc;
            memcpy(slots_before,ram+0xE600,sizeof(slots_before));
            memcpy(pal_before,ram+0xDFE0,sizeof(pal_before));
            memset(vram,0,sizeof(vram));
            uint8_t home[0x200];memset(home,0,sizeof(home));
            home[0]=1;som_fam_put16(home+0x8A,0x2EA0);som_fam_put16(home+0x92,0x7EA2);
            for(unsigned t=0;t<512;t++)vram[0xC000+t*32]=(uint8_t)t;
            memset(ram+0xDFE0,0x66,0x20);                        /* palette 7 */
            memcpy(vc.object,home,sizeof(home));vc.valid=1;
            som_familiar_capture_vram(&vc,ram,vram,home,130);
            assert(vc.has_vram&&vc.tile_count==8&&vc.palette_address==0xDFE0);
            /* sprites at 0xA0 and 0xA2: A0 A1 B0 B1 A2 A3 B2 B3; zero fields skipped */
            memset(vram+0xC000,0,0x4000);memset(ram+0xDFE0,0,0x20);  /* new map */
            memset(ram+0xE600,0,0x600);
            int slot=som_familiar_restore(&vc,ram,vram);
            assert(slot==0&&som_fam_u16(ram+0xE600+0x8A)==0x2EA0);   /* home layout */
            assert(vram[0xC000+0xA0*32]==0xA0&&vram[0xC000+0xB1*32]==0xB1);
            assert(vram[0xC000+0x60*32]==0);                         /* others untouched */
            assert(ram[0xDFE0]==0x66&&ram[0x07E0]==0x66);           /* shadow + working */
            memcpy(ram+0xE600,slots_before,sizeof(slots_before));
            memcpy(ram+0xDFE0,pal_before,sizeof(pal_before));
        }
        ram[0xEA00]=1;
        assert(som_familiar_restore(&carry,ram,NULL)==-1);              /* no empty slot */
        ram[0xE800+0x190]=0;ram[0xE800+0x191]=0x80;                /* now dead */
        f.slot=1;f.map=130;
        assert(som_familiar_tick(&f,ram,5002)==SOM_FAMILIAR_DIED);
    }
    /* summon_familiar: clone, bind, publish the slot; dismiss ends it. */
    {
        char fam_path[]="/tmp/flxtr-familiar-test-XXXXXX";
        int ffd=mkstemp(fam_path);assert(ffd>=0);close(ffd);unlink(fam_path);
        familiar_file=fam_path;
        backend_retro_get_memory_data=mocked_memory_data;
        backend_retro_get_memory_size=mocked_memory_size;
        memset(mocked_wram,0,sizeof(mocked_wram));
        mocked_wram[0xDC]=129;
        mocked_wram[0xE600]=1;mocked_wram[0xE600+0x180]=0;          /* live monster */
        mocked_wram[0xDFA0+2]=0x1F;mocked_wram[0xDFA0+3]=0x00;       /* slot 1: pure red */
        familiar_pending=1;assert(som_clone_enemy());som_familiar_bind();
        /* Celestial tint maps pure red (0x001F) to blue-white shaded BGR555
         * (0x2506), rather than the old green channel-rotation (0x03E0). */
        assert(mocked_wram[0xDFC0+2]==0x06&&mocked_wram[0xDFC0+3]==0x25);
        assert(familiar.slot==1&&familiar.map==129&&!familiar_pending);
        FILE *ff=fopen(fam_path,"r");int n=0;assert(ff&&fscanf(ff,"%d",&n)==1&&fclose(ff)==0);
        assert(n==2);                                              /* bridge slot number */
        som_familiar_dismiss();
        assert(familiar.slot==-1&&access(fam_path,F_OK)!=0);
        assert(mocked_wram[0xE800+0x1F1]==0xFF);
        undo_ready=0;
    }
    /* Only summoners summon. */
    {
        uint8_t boy[0x200]={0},girl[0x200]={0},sprite[0x200]={0};
        boy[0x188]=19;boy[0x189]=19;boy[0x18A]=18;boy[0x18B]=8;boy[0x18C]=9;
        girl[0x188]=11;girl[0x189]=11;girl[0x18A]=11;girl[0x18B]=7;girl[0x18C]=15;
        sprite[0x188]=8;sprite[0x189]=13;sprite[0x18A]=9;sprite[0x18B]=15;sprite[0x18C]=5;
        assert(!som_familiar_summoner(boy)&&som_familiar_summoner(girl)&&
               som_familiar_summoner(sprite));
    }
    /* summon_boss: a saved monster package comes as a tinted familiar after
     * the map settles; summoners only unless the override marker exists. */
    {
        char pkg_path[]="/tmp/flxtr-summon-test-XXXXXX",any_path[]="/tmp/flxtr-anyone-test-XXXXXX";
        int pfd=mkstemp(pkg_path);assert(pfd>=0);
        static uint8_t pkg[0x200+0x1000+0x20];
        memset(pkg,0,sizeof(pkg));
        pkg[0]=1;pkg[0x180]=0x57;pkg[0x184]=0x2C;pkg[0x185]=0x01;   /* boss, 300 max HP */
        memset(pkg+0x200,0x55,0x1000);
        pkg[0x1200+2]=0x1F;                                          /* red in colour 1 */
        assert(write(pfd,pkg,sizeof(pkg))==(ssize_t)sizeof(pkg));close(pfd);
        int afd=mkstemp(any_path);assert(afd>=0);close(afd);unlink(any_path);
        summon_file=pkg_path;summon_anyone_file=any_path;
        backend_retro_get_memory_data=mocked_memory_data;
        backend_retro_get_memory_size=mocked_memory_size;
        memset(mocked_wram,0,sizeof(mocked_wram));
        mocked_wram[0xE000]=1;mocked_wram[0xE188]=19;mocked_wram[0xE189]=19;   /* the boy */
        mocked_wram[0xE18A]=18;mocked_wram[0xE18B]=8;mocked_wram[0xE18C]=9;
        mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;mocked_wram[0xDC]=131;
        familiar.slot=-1;familiar_carry.valid=0;undo_ready=0;
        assert(!som_summon_boss()&&!familiar_carry.valid);           /* not a summoner */
        afd=open(any_path,O_WRONLY|O_CREAT,0600);assert(afd>=0);close(afd);
        assert(som_summon_boss()&&familiar_carry.valid&&undo_ready); /* override */
        assert(!som_summon_boss());                                  /* one at a time */
        frame_number=900000ul;som_familiar_frame();                  /* starts settling */
        assert(familiar.slot<0);
        frame_number+=SOM_FAMILIAR_SETTLE_FRAMES;som_familiar_frame();
        assert(familiar.slot==0&&mocked_wram[0xE600+0x180]==0x57);   /* arrived */
        assert(mocked_wram[0x5000]==0x55);                            /* its tiles */
        assert(mocked_wram[0xDFA0+2]!=0x1F||mocked_wram[0xDFA0+3]!=0); /* tinted */
        som_familiar_dismiss();undo_ready=0;
        pfd=open(pkg_path,O_WRONLY|O_TRUNC);assert(pfd>=0&&write(pfd,"junk",4)==4);close(pfd);
        assert(!som_summon_boss()&&!familiar_carry.valid);           /* bad package */
        unlink(pkg_path);unlink(any_path);
    }
    /* Regression: no wrong sprite data is written by class weapon switching.
     * The native ring must first equip the weapon. */
    {
        backend_retro_get_memory_data=mocked_memory_data;
        backend_retro_get_memory_size=mocked_memory_size;
        mocked_has_vram=1;
        memset(mocked_wram,0,sizeof(mocked_wram));
        memset(mocked_vram,0x5A,sizeof(mocked_vram));
        uint8_t *ram=mocked_wram,*hero=ram+0xE000;
        ram[0xE4]=1;ram[0xE6]=0x0F;hero[0]=1;
        memset(ram+0xCC54,0xFF,8);
        ram[0xCC55]=0x40;ram[0xCC56]=0;ram[0xCC59]=0;
        hero[0x1E4]=1;hero[0x19C]=1;
        uint8_t hero_before[SOM_FAMILIAR_OBJECT];
        memcpy(hero_before,hero,sizeof(hero_before));
        assert(!som_equip_weapon(ram,2)); /* no unsafe instant swap */
        assert(hero[0x1E4]==1);
        assert(memcmp(hero,hero_before,sizeof(hero_before))==0);
        assert(mocked_vram[0xC000]==0x5A&&mocked_vram[0xFFFF]==0x5A);
        assert(!som_cycle_weapon(ram));
        som_disc_init(&disciplines);disciplines.selected=0;
        ring_ai_enabled=0;som_manual_disciplines=0;
        on_turbo=1;special_stage=special_suppress=0;
        physical_state=mocked_input_mask;mocked_buttons=0;last_physical_buttons=0;
        som_input_poll();
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        som_input_poll();
        assert(y_weapon_blocked&&!y_weapon_swap);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
        assert(hero[0x1E4]==1);
        mocked_buttons=0;som_input_poll();
        /* A real game-ring equip changes the equipped type and level. */
        hero[0x1E4]=2;hero[0x19C]=2;hero[0x19B]=0;
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        som_input_poll();
        assert(y_weapon_swap&&!y_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
        mocked_buttons=0;som_input_poll();
        /* Level-2 secondary special can charge the already-native-equipped axe. */
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_R2)|
                                   (1u<<RETRO_DEVICE_ID_JOYPAD_Y));
        som_input_poll();
        assert(special_stage==2&&special_weapon==2);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==
               (1u<<RETRO_DEVICE_ID_JOYPAD_B));
        mocked_buttons=0;som_input_poll();
        hero[0x19B]=2;som_special_tick();
        assert(!special_stage);
        hero[0x19B]=0;som_input_poll();
        /* Level-1 primary special is not allowed until the native ring equips sword. */
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_R2)|
                                   (1u<<RETRO_DEVICE_ID_JOYPAD_B));
        som_input_poll();
        assert(!special_stage&&hero[0x1E4]==2);
        mocked_buttons=0;som_input_poll();
        hero[0x1E4]=1;hero[0x19C]=1;
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_R2)|
                                   (1u<<RETRO_DEVICE_ID_JOYPAD_B));
        som_input_poll();
        assert(special_stage==1&&special_weapon==1);
        mocked_buttons=0;som_input_poll();
        hero[0x19B]=1;som_special_tick();
        assert(!special_stage);
        hero[0x19B]=0;mocked_buttons=0;som_input_poll();
        physical_state=mocked_input;mocked_has_vram=0;
        pressed=100;pressed_too=100;
        som_disc_init(&disciplines);
    }
    /* give_<weapon>: only an unowned weapon, tier 0, with an undo snapshot. */
    {
        backend_retro_serialize_size=mocked_serialize_size;
        backend_retro_serialize=mocked_serialize;
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));memset(ram+0xCC54,0xFF,8);ram[0xCC55]=0x40;
        ram[0xE4]=1;ram[0xE6]=0x0F;
        undo_ready=0;
        assert(som_give_weapon(ram,0)&&ram[0xCC54]==0x00&&undo_ready);   /* glove */
        assert(!som_give_weapon(ram,0)&&ram[0xCC54]==0x00);               /* owned now */
        assert(!som_give_weapon(ram,1)&&ram[0xCC55]==0x40);               /* sword kept */
        ram[0xD0]=0x81;assert(!som_give_weapon(ram,2)&&ram[0xCC56]==0xFF); /* dialogue */
        undo_ready=0;
    }
    /* use_candy: one Candy fewer, 100 HP queued for the game; never the last. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));memset(ram+0xCC48,0xFF,11);
        ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xE000]=1;
        ram[0xE182]=40;ram[0xE184]=117;
        ram[0xCC48]=0x43;ram[0xCC49]=0x60;                     /* 2 of item 3, 3 Candy */
        assert(som_use_candy(ram)&&ram[0xCC49]==0x40&&ram[0xE1F3]==100&&ram[0xCC48]==0x43);
        ram[0xE1F3]=200;assert(som_use_candy(ram)&&ram[0xE1F3]==255&&ram[0xCC49]==0x20);
        assert(!som_use_candy(ram)&&ram[0xCC49]==0x20);        /* last Candy: ring */
        ram[0xCC49]=0xE0;assert(som_use_candy(ram)&&ram[0xCC49]==0xC0); /* Turbo: 7 */
        ram[0xCC49]=0x60;ram[0xD0]=0x81;assert(!som_use_candy(ram)); /* dialogue */
        ram[0xD0]=0;ram[0xE182]=0;assert(!som_use_candy(ram));      /* KO */
    }
    /* pal_trace: per-frame low WRAM + palette shadow, written when done. */
    {
        char trace_path[]="/tmp/flxtr-paltrace-test-XXXXXX";
        int tfd=mkstemp(trace_path);assert(tfd>=0);close(tfd);unlink(trace_path);
        pal_trace_file=trace_path;
        static uint8_t ram[SOM_WRAM_SIZE];memset(ram,0,sizeof(ram));
        pal_trace_frames=0;pal_trace_on=1;
        for(unsigned f=0;f<SOM_PAL_TRACE_FRAMES;f++){ram[0x10]=(uint8_t)f;ram[0xDF20]=(uint8_t)(f*3);
            ram[0xE01C]=(uint8_t)(f+1);som_pal_trace_frame(ram);}
        assert(!pal_trace_on);
        FILE *tf=fopen(trace_path,"rb");assert(tf);
        fseek(tf,0,SEEK_END);assert(ftell(tf)==(long)((SOM_PAL_TRACE_LOW+0x400u)*SOM_PAL_TRACE_FRAMES));
        uint8_t first[SOM_PAL_TRACE_LOW+0x400u];fseek(tf,(long)(SOM_PAL_TRACE_LOW+0x400u)*5,SEEK_SET);
        assert(fread(first,1,sizeof(first),tf)==sizeof(first)&&fclose(tf)==0);
        assert(first[0x10]==5&&first[SOM_PAL_TRACE_LOW+0x120]==15);
        assert(first[SOM_PAL_TRACE_LOW+0x200+0x1C]==6);           /* hero object */
        unlink(trace_path);
    }
    /* vram_dump: read-only copy of the core's video RAM, if exposed. */
    {
        char vram_path[]="/tmp/flxtr-vram-test-XXXXXX";
        int vfd=mkstemp(vram_path);assert(vfd>=0);close(vfd);unlink(vram_path);
        vram_file=vram_path;
        backend_retro_get_memory_data=mocked_memory_data;
        backend_retro_get_memory_size=mocked_memory_size;
        mocked_has_vram=0;som_vram_dump();assert(access(vram_path,F_OK)!=0);
        mocked_has_vram=1;for(unsigned i=0;i<sizeof(mocked_vram);i++)mocked_vram[i]=(uint8_t)i;
        som_vram_dump();
        FILE *vf=fopen(vram_path,"rb");static uint8_t back[0x10000];
        assert(vf&&fread(back,1,sizeof(back),vf)==sizeof(back)&&fclose(vf)==0);
        assert(!memcmp(back,mocked_vram,sizeof(back)));
        unlink(vram_path);mocked_has_vram=0;
    }
    /* The hero's palette follows the chosen discipline, never stacking. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));
        ram[0xE000]=1;ram[0xE08B]=0x22;                       /* palette 1 -> 7EDF20 */
        for(unsigned i=0;i<32;i++)ram[0xDF20+i]=ram[0x0720+i]=(uint8_t)(0x40+i);
        uint8_t original[32];memcpy(original,ram+0xDF20,32);
        som_disc_init(&disciplines);hero_pal_have=0;
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF20,original,32));               /* no discipline */
        disciplines.selected=0;                               /* Swordmaster frost */
        som_hero_palette_frame(ram);
        uint8_t once[32];memcpy(once,ram+0xDF20,32);
        assert(!memcmp(once,original,16)&&!memcmp(once+30,original+30,2)); /* 0-7, 15 kept */
        assert(memcmp(once+16,original+16,14));                    /* clothes 8-14 tinted */
        assert(!memcmp(ram+0x0720,once,32));                       /* working buffer too */
        memset(ram+0x0720,0x77,32);                                /* a flash is running */
        som_hero_palette_frame(ram);
        assert(ram[0x0720+2]==0x77);                               /* left alone */
        memcpy(ram+0x0720,once,32);
        assert(memcmp(once,original,32)&&once[0]==original[0]&&once[1]==original[1]);
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF20,once,32));                   /* does not stack */
        memcpy(ram+0xDF20,original,32);                       /* game rewrote it */
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF20,once,32));
        disciplines.selected=SOM_DISCIPLINE_NONE;
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF20,original,32));               /* colours restored */
        som_disc_init(&disciplines);hero_pal_have=0;
    }
    /* Disciplines survive restarts; a damaged file starts fresh. */
    {
        char disc_path[]="/tmp/flxtr-disc-test-XXXXXX";
        int dfd=mkstemp(disc_path);assert(dfd>=0);close(dfd);
        disc_file=disc_path;
        som_disc_init(&disciplines);
        disciplines.selected=6;disciplines.level[6]=7;disciplines.experience[6]=42;
        som_disc_save();
        som_disc_init(&disciplines);som_disc_load();
        assert(disciplines.selected==6&&disciplines.level[6]==7&&disciplines.experience[6]==42);
        assert(disciplines.level[0]==1);
        /* A save from before rogue/samurai/archer (9 disciplines) loads. */
        FILE *old=fopen(disc_path,"w");
        assert(old&&fputs("v1 2 3 10 1 0 4 20 1 0 1 0 1 0 1 0 1 0 1 0\n",old)>=0&&fclose(old)==0);
        som_disc_init(&disciplines);som_disc_load();
        assert(disciplines.selected==2&&disciplines.level[0]==3&&disciplines.level[2]==4&&
               disciplines.level[9]==1&&disciplines.level[11]==1);
        FILE *bad=fopen(disc_path,"w");assert(bad&&fputs("v1 200 junk\n",bad)>=0&&fclose(bad)==0);
        som_disc_init(&disciplines);som_disc_load();
        assert(disciplines.selected==SOM_DISCIPLINE_NONE);
        unlink(disc_path);som_disc_init(&disciplines);
    }
    /* Bubble anchor: hero screen position under a hero-centred camera that
     * stops at the map edges. */
    backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0x00E4]=1;mocked_wram[0x00E6]=0x0F;
    mocked_wram[0xC0]=0x00;mocked_wram[0xC1]=0x04;              /* 1024 px wide */
    mocked_wram[0xC2]=0x00;mocked_wram[0xC3]=0x03;              /* 768 px high */
    int sx_,sy_;
    mocked_wram[0xE002]=0xF4;mocked_wram[0xE003]=0x01;          /* x 500 */
    mocked_wram[0xE004]=0x58;mocked_wram[0xE005]=0x02;          /* y 600 */
    assert(som_hero_screen(&sx_,&sy_)&&sx_==128&&sy_==112);      /* centred */
    mocked_wram[0xE002]=50;mocked_wram[0xE003]=0;mocked_wram[0xE004]=30;mocked_wram[0xE005]=0;
    assert(som_hero_screen(&sx_,&sy_)&&sx_==50&&sy_==30);        /* top-left edge */
    mocked_wram[0xE002]=0xE8;mocked_wram[0xE003]=0x03;          /* x 1000 */
    mocked_wram[0xE004]=0xF0;mocked_wram[0xE005]=0x02;          /* y 752 */
    assert(som_hero_screen(&sx_,&sy_)&&sx_==1000-768&&sy_==752-544); /* far edge */
    mocked_wram[0x00D0]=0x81;assert(!som_hero_screen(&sx_,&sy_)); /* dialogue */
    /* Native-ring transition trace must remember the pre-menu graphics and
     * record the actual native WRAM and VRAM differences without changing RAM. */
    {
        mocked_has_vram=1;
        memset(mocked_wram,0,sizeof(mocked_wram));
        memset(mocked_vram,0,sizeof(mocked_vram));
        mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;
        mocked_wram[0xE1E4]=1;
        assert(som_weapon_trace_allocate());
        weapon_trace_on=1;som_weapon_trace_reset();
        som_weapon_trace_frame();
        assert(weapon_trace_has_prev&&!weapon_trace_busy);
        mocked_wram[0xE4]=2; /* native ring opened */
        mocked_wram[0xE1E4]=2; /* native ring changed equipped weapon */
        mocked_vram[0xC020]=0x77;
        som_weapon_trace_frame();
        assert(!weapon_trace_busy); /* preserve pre-ring frame */
        mocked_wram[0xE4]=1;
        som_weapon_trace_frame();
        assert(weapon_trace_busy&&weapon_trace_from==1&&weapon_trace_to==2);
        for(unsigned t=0;t<SOM_WEAPON_TRACE_FRAMES;t++)som_weapon_trace_frame();
        assert(!weapon_trace_on&&!weapon_trace_busy);
        FILE *report=fopen("/tmp/flxtr-weapon-trace.txt","r");
        assert(report);
        char header[160]={0};
        assert(fgets(header,sizeof(header),report));
        assert(strstr(header,"sword")&&strstr(header,"axe"));
        fclose(report);
        const char *const artifacts[]={
            "/tmp/flxtr-weapon-trace.txt",
            "/tmp/flxtr-weapon-ram-before.bin","/tmp/flxtr-weapon-ram-after.bin",
            "/tmp/flxtr-weapon-vram-before.bin","/tmp/flxtr-weapon-vram-after.bin"
        };
        for(unsigned t=0;t<sizeof(artifacts)/sizeof(artifacts[0]);t++)unlink(artifacts[t]);
        weapon_trace_on=0;som_weapon_trace_reset();mocked_has_vram=0;
    }
    /* A long ring interaction retains the LAST 32 VRAM-changing frames,
     * including the critical sprite upload just before equipment changes. */
    {
        mocked_has_vram=1;memset(mocked_wram,0,sizeof(mocked_wram));
        memset(mocked_vram,0,sizeof(mocked_vram));
        mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;
        mocked_wram[0xE6]=0x0F;mocked_wram[0xE1E4]=1;
        weapon_trace_on=1;som_weapon_trace_reset();
        som_weapon_trace_frame();
        for(unsigned t=1;t<=40;t++){
            mocked_vram[0xC100]=(uint8_t)t;
            som_weapon_trace_frame();
        }
        assert(weapon_trace_vram_dumps==40);
        assert(access("/tmp/flxtr-weapon-frame-001.vram",F_OK)!=0);
        assert(access("/tmp/flxtr-weapon-frame-009.vram",F_OK)==0);
        assert(access("/tmp/flxtr-weapon-frame-040.vram",F_OK)==0);
        weapon_trace_on=0;som_weapon_trace_reset();mocked_has_vram=0;
        for(unsigned t=1;t<=40;t++){
            char filename[128];
            snprintf(filename,sizeof(filename),"/tmp/flxtr-weapon-frame-%03u.vram",t);
            unlink(filename);
        }
        unlink("/tmp/flxtr-weapon-timeline.txt");
    }
    reset_proxy();direct_transport=1;ring_ai_enabled=0;
    assert(flxtr_som_ai_command("ai_on")&&flxtr_som_ai_enabled());
    assert(flxtr_som_ai_command("step_right")&&armed&&active_action==SOM_ACT_STEP_RIGHT);
    assert(flxtr_som_ai_message("AI: Android relay")&&overlay.frames_left);
    char direct_gambits[SOM_GAMBIT_COUNT+1]={0};
    flxtr_som_ai_gambits(direct_gambits);
    assert(strlen(direct_gambits)==SOM_GAMBIT_COUNT);
    assert(flxtr_som_ai_command("ai_off")&&!flxtr_som_ai_enabled());
    direct_transport=0;
    unlink(path);unlink(wram_path);unlink(wram_tmp_path);unlink(ring_ai_path);
    puts("PASS: Turbo actions, tile steps, ring AI signal, idle auto-start, override, and WRAM export");
    return 0;
}
