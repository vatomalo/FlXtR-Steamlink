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
static uint16_t mocked_buttons,mocked_pad2;
static int16_t mocked_input_mask(unsigned port,unsigned device,unsigned index,unsigned id){
    (void)index;
    if(port>1||device!=RETRO_DEVICE_JOYPAD)return 0;
    const uint16_t buttons=port==0?mocked_buttons:mocked_pad2;
    if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)buttons;
    return id<16&&(buttons&(uint16_t)(1u<<id))?1:0;
}
static uint8_t mocked_vram[0x10000];
static int mocked_has_vram;
static uint8_t mocked_sram[8192];
static void *mocked_memory_data(unsigned id){
    if(id==RETRO_MEMORY_SAVE_RAM)return mocked_sram;
    if(id==RETRO_MEMORY_VIDEO_RAM)return mocked_has_vram?mocked_vram:NULL;
    return id==RETRO_MEMORY_SYSTEM_RAM?mocked_wram:NULL;
}
static size_t mocked_memory_size(unsigned id){
    if(id==RETRO_MEMORY_SAVE_RAM)return sizeof(mocked_sram);
    if(id==RETRO_MEMORY_VIDEO_RAM)return mocked_has_vram?sizeof(mocked_vram):0;
    return id==RETRO_MEMORY_SYSTEM_RAM?sizeof(mocked_wram):0;
}
static uint8_t fake_core_state[16];
static size_t fake_serialize_size(void){return sizeof(fake_core_state);}
static bool fake_serialize(void *data,size_t size){
    if(size<sizeof(fake_core_state))return false;
    memcpy(data,fake_core_state,sizeof(fake_core_state));return true;
}
static bool fake_unserialize(const void *data,size_t size){
    if(size!=sizeof(fake_core_state))return false;
    memcpy(fake_core_state,data,size);return true;
}
static void prog_enter(int slot_pattern){
    /* leave field play, show the game state of a slot, enter field play */
    mocked_wram[0xE4]=0;frame_number+=30;som_prog_frame();
    for(unsigned i=0;i<0x40;i++)mocked_wram[0xCC01+i]=(uint8_t)(slot_pattern+i);
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;mocked_wram[0xD0]=0;
    frame_number+=30;som_prog_frame();
}
static void prog_game_saves(unsigned slot,int slot_pattern,uint8_t salt){
    for(unsigned i=0;i<0x40;i++)mocked_sram[slot*0x400+4+i]=(uint8_t)(slot_pattern+i);
    mocked_sram[slot*0x400+0x300]=salt;            /* the rest of the block */
    frame_number+=30;som_prog_frame();
}
static int16_t captured[64];
static size_t captured_frames;
static size_t capture_audio(const int16_t *data,size_t frames){
    for(size_t i=0;i<frames*2&&captured_frames*2+i<64;i++)captured[captured_frames*2+i]=data[i];
    captured_frames+=frames;return frames;
}
static const void *shown_frame;
static size_t shown_pitch;
static void mocked_video(const void *data,unsigned width,unsigned height,size_t pitch){
    (void)width;(void)height;shown_frame=data;shown_pitch=pitch;
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
    frame_number=180;start_ring_held=0;start_ring_until=0;start_frame=0;injected_queries=0;
    active_action=SOM_ACT_NONE;action_frames=0;
    armed=0;test_enabled=1;on_turbo=1;game_loaded=1;completed=0;cancelled=0;
    pressed=100;
    memset(&overlay,0,sizeof(overlay));
    switch_holding=switch_spoiled=0;
    menu_hold_until_release=menu_was_open=0;
}
int main(void){
    energy_free=1;              /* moves cost nothing here; the cost test turns it off */
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
    /* Title screen (no verified field play): Start and L2+R2 go to the game,
     * no overlay. Same with unreadable memory. */
    reset_proxy();
    som_disc_init(&disciplines);
    som_manual_disciplines=0;
    ring_ai_enabled=0;
    last_physical_buttons=0;
    physical_state=mocked_input_mask;
    backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);som_input_poll();
    assert(!som_manual_disciplines&&!disciplines.open);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START)==1);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_R2));
    som_input_poll();assert(!som_manual_disciplines);
    mocked_buttons=0;som_input_poll();
    /* Field play but an event runs, or the ring is open: still native. */
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;mocked_wram[0xD0]=0x82;
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);som_input_poll();
    assert(!som_manual_disciplines);mocked_buttons=0;som_input_poll();
    /* Start inside the ring does not open the class editor. */
    mocked_wram[0xD0]=0;mocked_wram[0x7D]=1;
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);som_input_poll();
    assert(!som_manual_disciplines);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START)==0);
    som_manual_disciplines=0;disciplines.open=0;menu_hold_until_release=menu_was_open=0;
    mocked_buttons=0;som_input_poll();mocked_wram[0x7D]=0;
    backend_retro_get_memory_data=NULL;                      /* unreadable */
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);som_input_poll();
    assert(!som_manual_disciplines);mocked_buttons=0;som_input_poll();
    backend_retro_get_memory_data=mocked_memory_data;mocked_wram[0x7D]=0;
    /* L2+R2 does not open either menu. */
    mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                               (1u<<RETRO_DEVICE_ID_JOYPAD_R2));
    som_input_poll();assert(!som_manual_disciplines);
    mocked_buttons=0;som_input_poll();
    /* R3 in the field opens the game's ring (player, 2026-11-25): the
     * game sees a short Y and never the R3; our menu stays shut. */
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R3);
    som_input_poll();
    assert(!som_manual_disciplines);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R3)==0);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==1);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==
           (int16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y));
    frame_number+=SOM_START_RING_FRAMES;          /* the pulse ends; Start still held */
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    mocked_buttons=0;som_input_poll();
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    /* B on the native Classes descriptor opens the editor. */
    classes_ring_enabled=1;
    mocked_wram[0x7D]=1;mocked_wram[0x1809]=2;mocked_wram[0x180B]=SOM_RING_CONFIG_ENTRIES;
    mocked_wram[0x180A]=6;
    som_put_word(mocked_wram+0x1880+2*6,SOM_RING_CLASSES_DESCRIPTOR);
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(som_manual_disciplines&&disciplines.open&&!ring_ai_enabled);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
    mocked_buttons=0;som_input_poll();disciplines.cursor=0;
    const unsigned old_class=disciplines.selected;
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_SELECT;som_input_poll();
    assert(class_guide_open&&som_manual_disciplines);
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_UP;som_input_poll();
    assert(disciplines.cursor==SOM_DISCIPLINE_COUNT-1u);
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(disciplines.selected==old_class);
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_SELECT;som_input_poll();
    assert(!class_guide_open&&som_manual_disciplines);
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;som_input_poll();
    assert(disciplines.cursor==0u);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN);som_input_poll();
    assert(disciplines.cursor==1);
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN)==0);
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
    assert(disciplines.selected==1);
    {   /* R1: Music, then the three AI pages, then back to Classes */
        const uint16_t r1=1u<<RETRO_DEVICE_ID_JOYPAD_R,l1=1u<<RETRO_DEVICE_ID_JOYPAD_L;
        for(unsigned k=0;k<5u;k++){
            mocked_buttons=0;som_input_poll();mocked_buttons=r1;som_input_poll();
            assert(som_manual_disciplines&&jukebox.open==(k==0u)&&act_ai==(k>=1u&&k<4u)&&disciplines.open==(k==4u));
            if(act_ai)assert(gambits.open&&gambits.resting&&gambits.page==k-1u);
            else assert(!gambits.open);
        }
        mocked_buttons=0;som_input_poll();mocked_buttons=l1;som_input_poll();
        assert(act_ai&&gambits.page==SOM_PAGE_QUEST);
        char row[33];assert(som_gambit_line(&gambits,SOM_QUEST_COUNT+1u,row)&&strstr(row,"Start AI"));
        for(unsigned k=0;k<2u;k++){mocked_buttons=0;som_input_poll();mocked_buttons=l1;som_input_poll();}
        assert(act_ai&&gambits.page==SOM_PAGE_ACTIONS&&!ring_ai_enabled);
        for(unsigned k=0;k<2u;k++){mocked_buttons=0;som_input_poll();mocked_buttons=l1;som_input_poll();}
        assert(!act_ai&&disciplines.open&&!jukebox.open&&!gambits.open&&!gambits.resting);
        assert(disciplines.selected==1);
    }
    mocked_buttons=0;som_input_poll();
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);som_input_poll();
    assert(!som_manual_disciplines&&!disciplines.open);
    mocked_buttons=0;som_input_poll();
    /* The menu, then R1: the MUSIC page. Down, B tunes the radio station (a
     * request line for the Steam Link's music bridge); A closes the menu. */
    {
        char juke_req[]="/tmp/flxtr-juke-req-test-XXXXXX";
        const int fd=mkstemp(juke_req);assert(fd>=0);close(fd);
        juke_request_file=juke_req;juke_list_file="/nonexistent/flxtr-juke-list";
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_R2));
        som_input_poll();mocked_buttons=0;som_input_poll();
        assert(!som_manual_disciplines);                     /* L2+R2 no longer opens it */
        assert(som_ring_on_classes(mocked_wram));            /* returned to Classes */
        mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(som_manual_disciplines&&disciplines.open);
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(jukebox.open&&!disciplines.open);
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN);som_input_poll();
        mocked_buttons=0;som_input_poll();
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
        mocked_buttons=0;som_input_poll();
        FILE *sent=fopen(juke_req,"r");char got[64]={0};
        assert(sent&&fgets(got,sizeof(got),sent));fclose(sent);
        assert(!strcmp(got,"radio 6\n"));
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(!som_manual_disciplines&&!jukebox.open&&!disciplines.open);
        unlink(juke_req);juke_request_file=SOM_JUKE_REQUEST;juke_list_file=SOM_JUKE_LIST;
    }
    mocked_buttons=0;
    memset(mocked_wram,0,sizeof(mocked_wram));
    /* P2's class modifiers never send native ring buttons. Both stick
     * clicks generate an independent Y pulse, and only R2 arms guard. */
    mocked_wram[0xE000]=mocked_wram[0xE200]=mocked_wram[0xE4]=1;
    mocked_wram[0xE6]=0x0F;mocked_wram[0xD9]=1;mocked_wram[0xDA]=2;
    mocked_wram[0xE382]=40;
    mocked_pad2=(1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X);
    som_pad2_poll();
    assert(pad2_combo_actor==1u&&pad2_wanted==0);
    mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_R2;som_pad2_poll();
    assert(pad2_guard_hp==40u&&pad2_wanted==(1u<<RETRO_DEVICE_ID_JOYPAD_R));
    mocked_wram[0xE382]=30;som_pad2_guard_finish();
    assert(mocked_wram[0xE382]==35u);
    mocked_pad2=(1u<<RETRO_DEVICE_ID_JOYPAD_L3)|(1u<<RETRO_DEVICE_ID_JOYPAD_START);
    som_pad2_poll();
    /* Start reaches the game, which hands the hero back (leave). */
    assert(pad2_wanted==((1u<<RETRO_DEVICE_ID_JOYPAD_L3)|(1u<<RETRO_DEVICE_ID_JOYPAD_START)));
    mocked_pad2=0;som_pad2_poll();
    /* The press that stops the AI must not also make player 2 leave. */
    ring_ai_enabled=1;mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_START;som_pad2_poll();
    ring_ai_enabled=0;som_pad2_poll();assert(pad2_wanted==0);
    mocked_pad2=0;som_pad2_poll();
    mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_START;som_pad2_poll();
    assert(pad2_wanted==(1u<<RETRO_DEVICE_ID_JOYPAD_START));
    mocked_pad2=0;som_pad2_poll();
    /* Start is the game's own for player 1 too, also with both bound. */
    mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_START;som_input_poll();
    assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START)==1);
    mocked_buttons=0;som_input_poll();
    {   /* Pads by permanent id: seats, reconnecting and the quit chord. */
        SomPads t;som_pads_init(&t);
        t.pad[0].fd=t.pad[1].fd=100;strcpy(t.pad[0].id,"aa");strcpy(t.pad[1].id,"bb");
        assert(!som_pads_seats(&t)&&!t.seat[0][0]);          /* nobody pressed */
        som_pads_event(&t.pad[1],EV_KEY,0x131,1);assert(!som_pads_seats(&t));
        assert(!strcmp(t.seat[0],"bb")&&!t.seat[1][0]&&som_pads_buttons(&t,0)==1u);
        som_pads_event(&t.pad[1],EV_KEY,0x131,0);
        som_pads_event(&t.pad[0],EV_ABS,ABS_HAT0X,-1);(void)som_pads_seats(&t);
        assert(!strcmp(t.seat[1],"aa")&&som_pads_buttons(&t,1)==(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT));
        som_pads_event(&t.pad[0],EV_ABS,ABS_HAT0X,0);
        /* Player 1's pad drops and returns on another node and slot. */
        t.pad[1].fd=-1;(void)som_pads_seats(&t);
        assert(som_pads_buttons(&t,0)==0&&!strcmp(t.seat[0],"bb"));
        som_pads_event(&t.pad[0],EV_KEY,0x130,1);(void)som_pads_seats(&t);
        assert(!strcmp(t.seat[1],"aa")&&!strcmp(t.seat[0],"bb")); /* 2 does not steal 1 */
        som_pads_event(&t.pad[0],EV_KEY,0x130,0);
        t.pad[3].fd=100;strcpy(t.pad[3].id,"bb");
        som_pads_event(&t.pad[3],EV_ABS,ABS_RX,255);(void)som_pads_seats(&t);
        assert(som_pads_buttons(&t,0)==(1u<<RETRO_DEVICE_ID_JOYPAD_L2));
        som_pads_event(&t.pad[3],EV_ABS,ABS_RX,0);
        /* A third pad takes the seat of one that is gone. */
        t.pad[3].fd=-1;t.pad[2].fd=100;strcpy(t.pad[2].id,"cc");
        som_pads_event(&t.pad[2],EV_KEY,0x132,1);(void)som_pads_seats(&t);
        assert(!strcmp(t.seat[0],"cc")&&!strcmp(t.seat[1],"aa"));
        som_pads_event(&t.pad[2],EV_KEY,0x132,0);
        /* Either pad quits; the game does not see the chord. */
        som_pads_event(&t.pad[0],EV_KEY,0x138,1);som_pads_event(&t.pad[0],EV_KEY,0x139,1);
        assert(som_pads_buttons(&t,1)==0);
        for(unsigned i=1;i<SOM_PADS_QUIT_FRAMES;i++)assert(!som_pads_seats(&t));
        assert(som_pads_seats(&t)==1&&!som_pads_seats(&t));
    }
    {   /* Both play: pad 2 borrows the acting hero for its evade. */
        const uint16_t a_key=1u<<RETRO_DEVICE_ID_JOYPAD_A,left=1u<<RETRO_DEVICE_ID_JOYPAD_LEFT;
        const uint8_t saved=disciplines.party[0];
        mocked_wram[0xD9]=1;mocked_wram[0xDA]=2;disciplines.party[0]=SOM_DISC_MARTIAL_ARTS;
        mocked_pad2=0;som_input_poll();
        assert(som_actor==0u&&!evade_active);
        mocked_pad2=left|a_key;som_input_poll();
        assert(som_actor==1u&&evade_active);
        mocked_buttons=left;som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)==1); /* P1 free */
        assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        mocked_buttons=mocked_pad2=0;
        evade_active=0;ukemi_draw.active=0;som_input_poll();
        assert(som_actor==0u);                             /* handed back */
        som_energy_init(som_actor_energy());
        disciplines.party[0]=saved;
    }
    {   /* Player 1 gone: pad 2 leads and drives the game's port 2. */
        const uint16_t r3=1u<<RETRO_DEVICE_ID_JOYPAD_R3,st=1u<<RETRO_DEVICE_ID_JOYPAD_START;
        frontend_state=mocked_input_mask;physical_state=som_lead_state;
        mocked_wram[0xD9]=0;mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT;som_input_poll();
        assert(lead_swapped&&som_pad1_member_in(mocked_wram)==1u);
        assert(som_pad2_member(mocked_wram)==SOM_PARTY_MEMBERS);
        assert(last_physical_buttons==(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)==0);
        mocked_pad2=r3;som_input_poll();                  /* the ring, on port 2 */
        assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
        mocked_pad2=0;frame_number+=SOM_START_RING_FRAMES;som_input_poll();
        mocked_buttons=st;som_input_poll();               /* pad 1 can rejoin */
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START)==1);
        mocked_wram[0xD9]=1;som_input_poll();             /* back: no press from the held Start */
        assert(!lead_swapped&&last_physical_buttons==st&&som_pad1_member_in(mocked_wram)==0u);
        mocked_buttons=0;som_input_poll();
        physical_state=mocked_input_mask;frontend_state=NULL;
    }
    mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_R3;som_pad2_poll();
    assert(pad2_wanted==(1u<<RETRO_DEVICE_ID_JOYPAD_R3)); /* no synthetic Y */
    mocked_pad2=(1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_R);
    const unsigned boy_class=disciplines.selected;
    const unsigned girl_class=disciplines.party[0];
    som_pad2_poll();
    assert(disciplines.party[0]!=girl_class&&disciplines.selected==boy_class);
    assert((pad2_wanted&((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                         (1u<<RETRO_DEVICE_ID_JOYPAD_R)))==0);
    mocked_pad2=0;som_pad2_poll();
    /* Player 2 can open and exit AI play from the second controller. */
    ring_ai_file=ring_ai_path;ring_ai_enabled=1;ring_ai_grace_until=0;gambits.open=0;gambits.cursor=0;
    mocked_buttons=0;mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(gambits.open&&ring_ai_enabled);
    assert(som_input_state(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
    mocked_pad2=0;som_input_poll();
    for(unsigned i=0;i<SOM_GAMBIT_EXIT_ITEM;i++){
        mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;som_input_poll();
        mocked_pad2=0;som_input_poll();
    }
    assert(gambits.cursor==SOM_GAMBIT_EXIT_ITEM);
    mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();
    assert(!ring_ai_enabled&&!gambits.open);
    mocked_pad2=0;som_input_poll();
    ring_ai_enabled=1;gambits.open=0;mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_START;
    som_input_poll();assert(ring_ai_enabled); /* Start alone cannot quit */
    mocked_pad2=(1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_START);
    som_input_poll();assert(!ring_ai_enabled&&!gambits.open); /* L2+Start is the explicit P2 exit */
    mocked_pad2=0;som_input_poll();
    /* The same chord on slot 1; split across two pads it does nothing. */
    ring_ai_enabled=1;gambits.open=0;mocked_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_L2;
    mocked_pad2=1u<<RETRO_DEVICE_ID_JOYPAD_START;som_input_poll();assert(ring_ai_enabled);
    mocked_pad2=0;mocked_buttons|=1u<<RETRO_DEVICE_ID_JOYPAD_START;
    som_input_poll();assert(!ring_ai_enabled&&!gambits.open);
    mocked_buttons=0;som_input_poll();
    assert(!ring_ai_enabled); /* letting go of L2 must not start the AI */
    memset(mocked_wram,0,sizeof(mocked_wram));
    {
        /* Scaled savestates can enter on any frame. Keep damage proportion
         * and never stack the same stat floor on a second pass. */
        uint8_t enemy[SOM_BUN_OBJECT]={0};
        uint8_t rabite[29]={1,20,0,0,3,1,1,1};
        enemy[0x181]=1;enemy[0x182]=10;enemy[0x184]=20;
        som_enemy_scale(enemy,rabite,13);
        assert(enemy[0x181]==15&&som_word(enemy+0x184)==125&&som_word(enemy+0x182)==63);
        assert(enemy[0x188]==35&&som_word(enemy+0x1A5)==9);
        uint8_t prior[SOM_BUN_OBJECT];memcpy(prior,enemy,sizeof(prior));
        som_enemy_scale(enemy,rabite,13);
        assert(!memcmp(prior,enemy,sizeof(prior)));
        som_enemy_scale(enemy,rabite,20);
        assert(enemy[0x181]==22&&som_word(enemy+0x184)==174&&som_word(enemy+0x182)==88);
        som_put_word(enemy+0x182,0);
        som_enemy_scale(enemy,rabite,25);
        assert(!som_word(enemy+0x182));
    }
    assert(som_disc_weapons[0][0]==1&&som_disc_weapons[0][1]==2);
    assert(som_disc_weapons[1][0]==0&&som_disc_weapons[1][1]==3);
    assert(som_disc_weapons[2][0]==1&&som_disc_weapons[2][1]==6);
    assert(som_disc_weapons[10][0]==1&&som_disc_weapons[10][1]==5);
    /* Grant only missing weapons on selection, retaining tiers/ownership. */
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;
    for(unsigned i=0;i<8;i++)mocked_wram[0xCC54+i]=0xFF;
    mocked_wram[0xCC55]=0x43; /* already-owned tier-3 sword */
    assert(som_class_supply(mocked_wram,2)==3); /* Ninja: chakram, Taijutsu glove, Kusarigama flail */
    assert(mocked_wram[0xCC55]==0x43&&mocked_wram[0xCC5A]==0&&mocked_wram[0xCC54]==0&&
           mocked_wram[0xCC58]==0);
    assert(som_class_supply(mocked_wram,2)==0); /* idempotent */
    assert(som_class_supply(mocked_wram,10)==1); /* Samurai gains yumi */
    assert(mocked_wram[0xCC59]==0&&mocked_wram[0xCC55]==0x43);
    mocked_wram[0xE4]=0;mocked_wram[0xCC56]=0xFF;
    assert(som_class_supply(mocked_wram,0)==0); /* no changes outside field */
    assert(mocked_wram[0xCC56]==0xFF);

    /* Taijutsu: glove, glove, sword, then again; a pause restarts it. */
    frame_number=1000;taijutsu_last=0;taijutsu_step=0;
    const uint8_t keep_chain_class=disciplines.selected;
    disciplines.selected=SOM_DISC_NINJUTSU;
    assert(som_taijutsu_peek()==0u);som_taijutsu_advance();
    assert(som_taijutsu_peek()==0u);som_taijutsu_advance();
    assert(som_taijutsu_peek()==1u);som_taijutsu_advance();
    assert(som_taijutsu_peek()==0u);som_taijutsu_advance();
    frame_number+=SOM_TAIJUTSU_RESET+1ul;
    assert(som_taijutsu_peek()==0u&&taijutsu_step==0u);
    /* Weapon Flow's B flows through other weapons: sword, axe, whip, javelin. */
    disciplines.selected=SOM_DISC_WEAPON_FLOW;taijutsu_step=0;taijutsu_last=frame_number;
    {
        /* ...and the spear only as the finisher, at its top power level. */
        static const unsigned flow[]={1,2,4,7,3,1},flow_levels[]={0,0,0,0,SOM_CHAIN_TOP,0};
        for(unsigned k=0;k<6u;k++){
            assert(som_taijutsu_peek()==flow[k]&&som_taijutsu_peek_level()==flow_levels[k]);
            som_taijutsu_advance();
        }
    }
    /* The monk's B is no weapon chain: it is the glove power chain. */
    disciplines.selected=SOM_DISC_MARTIAL_ARTS;taijutsu_step=0;
    assert(som_taijutsu_peek()==SOM_NO_WEAPON);
    disciplines.selected=SOM_DISC_SWORD;
    assert(som_taijutsu_peek()==SOM_NO_WEAPON);                  /* no chain */
    disciplines.selected=keep_chain_class;taijutsu_step=0;
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;
    for(unsigned i=0;i<8;i++)mocked_wram[0xCC54+i]=0xFF;
    som_class_supply(mocked_wram,SOM_DISC_WEAPON_FLOW);
    assert(mocked_wram[0xCC56]==0&&mocked_wram[0xCC58]==0&&mocked_wram[0xCC5B]==0); /* axe whip javelin */
    /* Styles: Samurai's three swords; style commands need a class. */
    {
        unsigned index;
        som_styles_init(&styles);
        const SomWeaponStyle *st=som_style_of(&styles,10,&index);
        assert(st&&!strcmp(st->name,"Katana")&&st->weapon==1u);
        assert(!strcmp(som_weapon_styles[som_style_index(10,0)].name,"Tanto"));
        assert(!strcmp(som_weapon_styles[som_style_index(10,2)].name,"Dai Katana"));
        assert(som_weapon_styles[som_style_index(10,2)].reach_bonus>0);
        assert(som_weapon_styles[som_style_index(10,0)].speed_pct>100u);
        /* The balanced style is the class's B weapon; another style may be a
         * weapon of its own (the ninja's Kusarigama is a flail). */
        for(unsigned c=0;c<SOM_DISCIPLINE_COUNT;c++)
            assert(som_weapon_styles[som_style_index(c,1)].weapon==som_disc_weapons[c][0]);
        assert(som_weapon_styles[som_style_index(2,2)].weapon==4u);    /* Kusarigama: whip */
        /* Mentalist: B is Shadow Blast (Dark Force) in the default spell set. */
        assert(som_caster_spell(SOM_DISC_PSIONICS,0)==0x25u&&
               !strcmp(som_class_spell_label(SOM_DISC_PSIONICS,0x25),"Shadow Bolt")&&
               !strcmp(som_class_spell_label(5u,0x25),"Shadow Bolt"));
        assert(som_button_spell_quick(0u)&&!som_button_spell_quick(2u));   /* B: always quick */
    }
    /* Throw: back is the D-pad away from the facing. */
    mocked_wram[0xE010]=0x02u;   /* facing right */
    assert(som_throw_back_pressed(mocked_wram,(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)));
    assert(!som_throw_back_pressed(mocked_wram,(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)));
    mocked_wram[0xE010]=0x00u;   /* facing up */
    assert(som_throw_back_pressed(mocked_wram,(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)));
    frame_number=0;

    /* Progression belongs to a save (src/som_progress.h). */
    {
        char dir[]="/tmp/som-progress-test-XXXXXX";assert(mkdtemp(dir));
        prog_dir=dir;
        char legacy_disc[64];snprintf(legacy_disc,sizeof(legacy_disc),"%s/v1",dir);
        FILE *v1=fopen(legacy_disc,"w");
        assert(v1&&fputs("v1 2 1 0 1 0 5 40 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0\n",v1)>=0&&!fclose(v1));
        disc_file=legacy_disc;
        backend_retro_get_memory_data=mocked_memory_data;backend_retro_get_memory_size=mocked_memory_size;
        memset(mocked_wram,0,sizeof(mocked_wram));memset(mocked_sram,0,sizeof(mocked_sram));
        som_disc_init(&disciplines);som_prog_preserve_legacy();
        som_prog_fresh(&prog,7);prog_baseline=0;prog_was_field=0;prog_session_first=1;
        frame_number=10000;som_prog_frame();                 /* title: baseline */
        /* New game: no save matches, fresh progression; nothing written yet. */
        prog_enter(0x10);
        assert(!prog.owner&&disciplines.level[2]==1);
        disciplines.level[0]=3;disciplines.experience[0]=20;
        assert(!som_prog_write());
        /* The game saves to slot 1: a record for that exact block. */
        prog_game_saves(0,0x10,1);
        assert(prog.owner==som_prog_slot_fingerprint(mocked_sram,0)&&prog.slot==0);
        const uint64_t slot1=prog.owner;
        SomProgress check;assert(som_prog_read(slot1,&check)==SOM_PROG_OK&&check.level[0]==3);
        /* Another playthrough in slot 2: no inherited XP. */
        for(unsigned i=4;i<0x400;i++)mocked_sram[0x400+i]=(uint8_t)(0x80+i);
        frame_number+=30;som_prog_frame();                   /* a slot changed, not ours */
        assert(prog.owner==slot1);
        prog_enter(0x80+4);
        assert(prog.slot==1&&prog.owner!=slot1&&disciplines.level[0]==1);
        disciplines.level[1]=4;
        prog_game_saves(1,0x80+4,2);
        /* Back to slot 1: its own progression, not slot 2's. */
        prog_enter(0x10);
        assert(prog.owner==slot1&&disciplines.level[0]==3&&disciplines.level[1]==1);
        /* A damaged record falls back to the previous generation. */
        disciplines.level[0]=5;assert(som_prog_write());       /* now .prev holds level 3 */
        char path[300];som_prog_path(slot1,"",path,sizeof(path));
        FILE *bad=fopen(path,"w");assert(bad&&fputs("som-progress 2\ngarbage\n",bad)>=0&&!fclose(bad));
        assert(som_prog_read(slot1,&check)==SOM_PROG_OK&&check.level[0]==3);
        /* An unknown version is never overwritten. */
        FILE *future=fopen(path,"w");assert(future&&fputs("som-progress 9\n",future)>=0&&!fclose(future));
        assert(!som_prog_write());
        future=fopen(path,"r");char line[32]={0};assert(future&&fgets(line,sizeof(line),future));fclose(future);
        assert(!strcmp(line,"som-progress 9\n"));
        unlink(path);
        /* Save states carry the progression; older states without it load. */
        backend_retro_serialize_size=fake_serialize_size;backend_retro_serialize=fake_serialize;
        backend_retro_unserialize=fake_unserialize;backend=(void*)1;
        assert(retro_serialize_size()==16+SOM_PROG_TRAILER_SIZE);
        static uint8_t state[16+SOM_PROG_TRAILER_SIZE];
        disciplines.level[3]=7;fake_core_state[0]=42;
        assert(retro_serialize(state,sizeof(state)));
        disciplines.level[3]=1;fake_core_state[0]=0;
        assert(retro_unserialize(state,sizeof(state))&&fake_core_state[0]==42&&disciplines.level[3]==7);
        assert(retro_unserialize(state,16)&&disciplines.level[3]==7);        /* old state */
        state[16+20]^=1;assert(retro_unserialize(state,sizeof(state)));      /* bad trailer */
        assert(disciplines.level[3]==7);
        backend_retro_serialize_size=NULL;backend_retro_serialize=NULL;backend_retro_unserialize=NULL;
        backend=NULL;
        /* Old device-wide levels: kept, applied only on request. */
        assert(disciplines.level[2]==1);
        assert(som_handle_command("progress_claim_legacy")&&disciplines.level[2]==5);
        assert(!som_handle_command("progress_claim_legacy"));           /* once */
        char cmd[128];snprintf(cmd,sizeof(cmd),"rm -rf %s",dir);assert(system(cmd)==0);
        prog_dir=SOM_PROG_DIR;disc_file=SOM_DISC_FILE;
        memset(mocked_wram,0,sizeof(mocked_wram));memset(mocked_sram,0,sizeof(mocked_sram));
        som_disc_init(&disciplines);
    }
    /* Soundtrack mixing from a decoded file named in the control file. */
    {
        char dir[]="/tmp/som-music-test-XXXXXX";assert(mkdtemp(dir));
        static char control[80],pcm[80];
        snprintf(control,sizeof(control),"%s/play",dir);snprintf(pcm,sizeof(pcm),"%s/a.raw",dir);
        music_play_file=control;som_music_stop();music_check_frame=0;
        captured_frames=0;frontend_audio_batch=capture_audio;
        int16_t game[8]={1000,-1000,2000,-2000,0,0,7,7};
        frame_number=50000;som_music_pump();assert(music_fd<0);   /* no control file */
        assert(som_audio_batch(game,4)==4&&captured_frames==4&&captured[0]==1000);
        int16_t music[6]={256,-256,512,-512,1024,0};
        FILE *f=fopen(pcm,"wb");assert(f&&fwrite(music,1,sizeof(music),f)==sizeof(music)&&!fclose(f));
        f=fopen(control,"w");assert(f&&fprintf(f,"%s\n",pcm)>0&&!fclose(f));
        frame_number+=30;som_music_pump();
        assert(music_fd>=0&&music_ring.count==3);
        som_music_pump();assert(music_ring.count==6);                 /* looped */
        captured_frames=0;assert(som_audio_batch(game,4)==4&&captured_frames==4);
        assert(captured[0]==1000+256*(int)SOM_MIX_GAIN_DEFAULT/256&&captured[1]==-1000-256*(int)SOM_MIX_GAIN_DEFAULT/256);
        assert(captured[6]==7+256*(int)SOM_MIX_GAIN_DEFAULT/256);  /* looped: frame 4 = frame 1 */
        unlink(control);frame_number+=30;som_music_pump();
        assert(music_fd<0&&!music_ring.count);                    /* no control file: stop */
        f=fopen(control,"w");assert(f&&fprintf(f,"%s/missing.raw\n",dir)>0&&!fclose(f));
        frame_number+=30;som_music_pump();assert(music_fd<0);    /* not a file: nothing */
        unlink(control);unlink(pcm);rmdir(dir);music_play_file=SOM_MUSIC_PLAY;
        som_music_stop();frontend_audio_batch=NULL;
    }
    /* Opt-in priority: off without the file, bad values ignored, and only
     * threads named retroarch.exec at nice 0 are touched (a fake task tree;
     * setpriority on our own tid with a positive-only check). */
    {
        char dir[]="/tmp/som-prio-test-XXXXXX";assert(mkdtemp(dir));
        static char file[96];snprintf(file,sizeof(file),"%s/prio",dir);
        priority_file=file;
        assert(som_priority_wanted()==0);
        const char *bad[]={"5\n","-11\n","x\n","0\n"};
        for(unsigned i=0;i<4;i++){
            FILE *f=fopen(file,"w");assert(f&&fputs(bad[i],f)>=0&&!fclose(f));
            assert(som_priority_wanted()==0);
        }
        FILE *f=fopen(file,"w");assert(f&&fputs("-5\n",f)>=0&&!fclose(f));
        assert(som_priority_wanted()==-5);
        assert(som_priority_apply(0)==0);
        /* fake /proc/self/task: one thread of another name, one missing comm */
        static char tasks[96],sub[140],comm[160];
        snprintf(tasks,sizeof(tasks),"%s/task",dir);assert(!mkdir(tasks,0700));
        snprintf(sub,sizeof(sub),"%s/%d",tasks,(int)getpid());assert(!mkdir(sub,0700));
        snprintf(comm,sizeof(comm),"%s/comm",sub);
        f=fopen(comm,"w");assert(f&&fputs("SDLAudioP1\n",f)>=0&&!fclose(f));
        priority_tasks=tasks;
        assert(som_priority_apply(-5)==0);                        /* wrong name */
        assert(getpriority(PRIO_PROCESS,0)==0||getpriority(PRIO_PROCESS,0)>0);
        unlink(comm);rmdir(sub);rmdir(tasks);unlink(file);rmdir(dir);
        priority_tasks="/proc/self/task";priority_file=SOM_PRIORITY_FILE;
    }
    /* Frame timing: percentiles, late streaks, off means no clock reads,
     * the same audio either way, and one JSON report per window. */
    {
        SomPerfWindow w;memset(&w,0,sizeof(w));w.nominal_us=16639;
        for(unsigned i=0;i<100;i++)som_perf_add(&w,SOM_PERF_RUN,i<90?1000u:20000u);
        assert(som_perf_percentile(&w.phase[SOM_PERF_RUN],50)==1250u);
        assert(som_perf_percentile(&w.phase[SOM_PERF_RUN],95)==20250u);
        assert(w.phase[SOM_PERF_RUN].max_us==20000u);
        som_perf_add(&w,SOM_PERF_RUN,900000u);                       /* overflow bucket */
        assert(som_perf_percentile(&w.phase[SOM_PERF_RUN],100)==900000u);
        som_perf_interval(&w,16600);som_perf_interval(&w,30000);som_perf_interval(&w,40000);
        som_perf_interval(&w,16700);som_perf_interval(&w,26000);
        assert(w.late==3&&w.late_streak_max==2&&w.late_streak==1);
        char text[4096];
        assert(som_perf_json(&w,text,sizeof(text),1,0.5,0.1,1,0,2,3)>0);
        assert(strstr(text,"\"late\":3")&&strstr(text,"\"run\":{\"n\":101")&&
               strstr(text,"\"nominal_fps\":60.100"));
        assert(som_perf_json(&w,text,64,1,0,0,0,0,0,0)<0);                 /* never overruns */

        int16_t game[8]={1000,-1000,2000,-2000,0,0,7,7};
        frontend_audio_batch=capture_audio;
        perf_env=0;perf_on=0;perf_check=0;
        assert(PERF_NOW()==0u&&som_perf_frame_begin(70000)==0u);
        captured_frames=0;assert(som_audio_batch(game,4)==4);
        int16_t off[8];memcpy(off,captured,sizeof(off));
        char perf_path[]="/tmp/som-perf-test-XXXXXX";
        int pfd=mkstemp(perf_path);assert(pfd>=0);close(pfd);unlink(perf_path);
        perf_file=perf_path;perf_env=1;perf_check=0;
        uint64_t t0=0;
        for(unsigned long f=0;f<=SOM_PERF_WINDOW;f++){
            t0=som_perf_frame_begin(80000+f);assert(t0);
            captured_frames=0;assert(som_audio_batch(game,4)==4);
            som_perf_frame_end(t0,0);
        }
        assert(!memcmp(off,captured,sizeof(off)));                  /* same samples */
        FILE *in=fopen(perf_path,"r");assert(in);
        char report[4096];size_t got=fread(report,1,sizeof(report)-1,in);fclose(in);report[got]=0;
        assert(strstr(report,"\"frames\":600,")&&strstr(report,"\"audio_frames\":2400,")&&
               strstr(report,"\"audio\":{\"n\":600")&&strstr(report,"\"interval\":{\"n\":600"));   /* first frame has none; 600th closes */
        unlink(perf_path);perf_file=SOM_PERF_FILE;perf_env=0;perf_on=0;perf_check=0;
        frontend_audio_batch=NULL;
    }
    /* The exit auto-state goes beside the ROM, as RetroArch names it. */
    som_auto_state_path_set("/roms/Snes/Secret of Mana, Turbo.smc");
    assert(!strcmp(som_auto_state_path,"/roms/Snes/Secret of Mana, Turbo.state.auto"));
    som_auto_state_path_set("/roms/v1.2/game");
    assert(!strcmp(som_auto_state_path,"/roms/v1.2/game.state.auto"));
    setenv("FLXTR_SOM_AUTO_STATE","0",1);som_auto_state_path_set("/a/b.smc");
    assert(!som_auto_state_path[0]);unsetenv("FLXTR_SOM_AUTO_STATE");
    /* Combo ladder: swings with observed HP loss climb the level; taps
     * without damage do not count, and a whiff drops the chain. */
    backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;
    mocked_wram[0xE6]=0x0F;mocked_wram[0xE1E4]=1;
    mocked_wram[0xE19C]=1;
    mocked_wram[0xE600]=1;mocked_wram[0xE780]=7;
    mocked_wram[0xE782]=100;mocked_wram[0xE783]=0;
    som_combo_clear();frame_number=500;combo_input_held=0;
    som_combo_frame();
    for(unsigned h=0;h<5;h++){
        frame_number+=20;som_combo_swing(h==4?1u:0u);   /* the 5th hit mixes in Y */
        mocked_wram[0xE782]--;
        som_combo_frame();
        assert(combo.hits==h+1);
    }
    assert(combo.mixes==1&&som_combo_level(&combo)==3);  /* 5 hits = 2, +1 mix */
    /* Each confirmed hit trained the weapon's discipline (sword, no class:
     * Swordsmanship, with diminishing returns on the same species); the
     * switch hit trained Weapon Flow too. */
    {   const unsigned sel=disciplines.selected;
        const unsigned trained=som_xp_weapon_discipline(1,sel,
            sel<SOM_DISCIPLINE_COUNT?som_disc_weapons[sel]:NULL);
        assert(disciplines.experience[trained]>=10u+8u);
        assert(disciplines.experience[8]>0u&&disciplines.experience[8]<SOM_XP_FLOW);
    }
    {   char text[24];som_combo_text(&combo,text);assert(!strcmp(text,"5 HIT MIX LV3"));}
    frame_number+=20;som_combo_swing(0);som_combo_frame();   /* no HP lost yet */
    assert(combo.hits==5&&combo.pending);
    combo_input_held=1;frame_number+=SOM_COMBO_HIT_WINDOW+1;som_combo_frame();
    assert(combo.hits==5);                                   /* holding: kept */
    combo_input_held=0;som_combo_frame();
    assert(!combo.pending&&!combo.hits);                     /* whiff: dropped */
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
    /* Unsupported ROMs get no features: only the Turbo file name enables them. */
    assert(som_rom_is_turbo("/roms/Snes/Secret of Mana, Turbo.smc"));
    assert(!som_rom_is_turbo("/roms/Snes/SecretofMana.smc"));
    assert(!som_rom_is_turbo("/roms/Snes/LegendofZelda.smc")&&!som_rom_is_turbo(NULL));
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
    assert(gambits.cursor==SOM_GAMBIT_EXIT_ITEM);                         /* wraps to Exit */
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
    assert(access(message_path,F_OK)!=0&&!overlay.line_count); /* debug hidden by default */
    note=fopen(message_path,"w");
    assert(note!=NULL&&fputs("^Hello there!|speech remains\n",note)>0&&fclose(note)==0);
    som_check_message();assert(!overlay.line_count);        /* bubbles: debug only */
    gambits.debug_messages=1;
    som_overlay_say(&overlay,"^Hello there!|speech remains",60);
    assert(overlay.speech_bubble&&overlay.line_count==2);
    gambits.debug_messages=0;
    som_overlay_say(&overlay,"Plain box",60);assert(overlay.line_count==1&&!overlay.speech_bubble);
    overlay.frames_left=0;
    note=fopen(message_path,"w");
    assert(note!=NULL&&fputs("AI: using Candy|line two\n",note)>0&&fclose(note)==0);
    gambits.debug_messages=1;som_check_message();
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
    /* Snes9x 2005's real pitch is 1024 bytes for 256 pixels: the decorated
     * copy is tightly packed (512-byte rows) and keeps every visible pixel. */
    {
        static uint16_t wide[224][512];
        for(unsigned y=0;y<224;y++)for(unsigned x=0;x<512;x++)
            wide[y][x]=x<256?(uint16_t)(y*256+x+1):0u;
        som_overlay_set(&overlay,"AI: padded pitch",30);
        som_video_refresh(wide,256,224,1024);
        assert(shown_frame==overlay_frame&&shown_pitch==512);
        const uint16_t *out=(const uint16_t*)shown_frame;
        size_t same=0;
        for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)same+=out[y*256u+x]==wide[y][x];
        assert(same>256u*224u-6000u);                      /* only the message box differs */
        for(unsigned y=0;y<224;y+=37)assert(out[y*256u]==wide[y][0]);   /* 512-byte rows */
        assert(out[223u*256u+255u]==wide[223][255]);
        som_video_refresh(wide,256,224,200);              /* pitch < row: untouched */
        assert(shown_frame==wide&&shown_pitch==200);
        overlay.frames_left=0;
    }
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
    /* L2 or R3 held alone for one second, then released: AI on (solo hero,
     * no Action Grid; Select is RetroArch's hotkey button on the Steam Link).
     * Short holds, other buttons and holds begun while the AI runs never
     * start it; any other button still stops it. */
    reset_proxy();backend_retro_get_memory_data=mocked_memory_data;
    backend_retro_get_memory_size=mocked_memory_size;
    ring_ai_file=ring_ai_path;unlink(ring_ai_path);
    frame_number=500000ul;
    pressed=RETRO_DEVICE_ID_JOYPAD_L3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES-1;som_input_poll();
    pressed=100;som_input_poll();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);      /* too short */
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_L3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES;
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();           /* another button */
    pressed=100;frame_number+=1;som_input_poll();
    assert(!ring_ai_enabled);
    static const unsigned never[]={RETRO_DEVICE_ID_JOYPAD_R3,RETRO_DEVICE_ID_JOYPAD_R2,RETRO_DEVICE_ID_JOYPAD_L2};
    for(unsigned k=0;k<3u;k++){
        frame_number+=10;pressed=never[k];som_input_poll();
        frame_number+=SOM_SWITCH_HOLD_FRAMES;som_input_poll();
        frame_number+=1;pressed=100;som_input_poll();
        assert(!ring_ai_enabled);                                /* only L3 starts it */
    }
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_L3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES;som_input_poll();
    frame_number+=1;pressed=100;som_input_poll();
    assert(ring_ai_enabled&&access(ring_ai_path,F_OK)==0);       /* long hold: on */
    assert(overlay.frames_left>0&&strstr(overlay.lines[0],"AI ON"));
    frame_number+=40;pressed=RETRO_DEVICE_ID_JOYPAD_L3;som_input_poll();
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
    /* Passive skills: unlock levels, ranks, caps and two-class requirements. */
    {
        SomDisciplines d;memset(&d,0,sizeof(d));d.selected=2u;
        assert(!som_perk_value(SOM_EFFECT_REELING_BONUS,&d,1));
        d.level[2]=4;assert(som_perk_value(SOM_EFFECT_REELING_BONUS,&d,1)==25);   /* Coup */
        d.level[2]=9;assert(som_perk_value(SOM_EFFECT_REELING_BONUS,&d,1)==50);
        d.level[2]=30;assert(som_perk_value(SOM_EFFECT_REELING_BONUS,&d,1)==50);  /* cap */
        assert(!som_perk_value(SOM_EFFECT_REELING_BONUS,&d,0));                  /* off */
        assert(!som_perk_value(SOM_EFFECT_BLINK_COOLDOWN_CUT,&d,1));   /* needs Rogue 5 */
        d.level[9]=5;assert(som_perk_value(SOM_EFFECT_BLINK_COOLDOWN_CUT,&d,1)==50);
        assert(som_perk_value(SOM_EFFECT_FIRST_HIT_CUT,&d,1)==25);    /* Rogue 5: rank 1 */
        assert(!som_perk_value(SOM_EFFECT_FUMA_LEVEL,&d,1));          /* needs Bushido 5 */
        d.level[10]=5;assert(som_perk_value(SOM_EFFECT_FUMA_LEVEL,&d,1)==2);
        assert(som_perk_value(SOM_EFFECT_IAI_COOLDOWN_CUT,&d,1)==15);
        d.level[1]=3;assert(som_perk_value(SOM_EFFECT_GUARD_COUNTER,&d,1)==1);
        /* Class perks follow the chosen class's level. */
        assert(som_perk_value(SOM_EFFECT_KILL_HEAL,&d,1)==9);        /* Ninjutsu 30: top rank */
        d.level[2]=4;assert(som_perk_value(SOM_EFFECT_KILL_HEAL,&d,1)==3);
        assert(!som_perk_value(SOM_EFFECT_LOW_HP_BONUS,&d,1));         /* class 4 < 6 */
        d.selected=1u;assert(!som_perk_value(SOM_EFFECT_KILL_HEAL,&d,1));  /* Martial 3 */
    }
    /* Gambit menu text and drawing. */
    {
        SomGambits g={1,1,{0,5,3,3,4,1},0,SOM_PAGE_ACTIONS,
            {3,2,2,2,3,3,3,3},{3,3},0,0,1,0,0,0,2,0};
        char text[33];
        assert(som_gambit_line(&g,0,text)&&strstr(text,"GAMBITS"));
        /* Quest page: two weights + exit, changed with Right, saved as digits. */
        {
            SomGambits q=g;q.page=SOM_PAGE_QUEST;q.cursor=1;
            char qt[33],digits[SOM_QUEST_COUNT+1];
            assert(som_gambit_line(&q,0,qt)&&strstr(qt,"QUEST"));
            assert(som_gambit_line(&q,1,qt)&&strstr(qt,"Advance the story"));
            assert(som_gambit_line(&q,2,qt)&&strstr(qt,"Level up"));
            assert(som_gambit_line(&q,3,qt)&&strstr(qt,"Exit"));
            assert(!som_gambit_line(&q,4,qt));
            assert(som_gambit_press(&q,1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)==SOM_GAMBIT_CHANGED);
            som_quest_format(&q,digits);assert(!strcmp(digits,"34"));
            som_quest_parse(&q,"x9");assert(q.quest[0]==3&&q.quest[1]==3);
            som_quest_parse(&q,"50");assert(q.quest[0]==5&&q.quest[1]==0);
        }
        assert(som_gambit_line(&g,1,text)&&strstr(text,"Fight monsters")&&strstr(text,"0 ....."));
        assert(som_gambit_line(&g,2,text)&&text[0]=='>'&&strstr(text,"5 ooooo"));
        assert(strlen(text)<=32);
        g.cursor=SOM_GAMBIT_MUSIC_ITEM;g.music_genre=0;
        assert(som_gambit_line(&g,SOM_GAMBIT_MUSIC_ITEM+1u,text)&&strstr(text,"Music genre: Lo-Fi"));
        assert(som_gambit_press(&g,1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)==SOM_GAMBIT_GENRE_CHANGED);
        assert(g.music_genre==1);
        assert(som_gambit_line(&g,SOM_GAMBIT_MUSIC_ITEM+1u,text)&&strstr(text,"Jazz"));
        /* The new brain rows must not alias the music row. */
        g.cursor=SOM_GAMBIT_MODEL_ITEM;
        assert(som_gambit_line(&g,SOM_GAMBIT_MODEL_ITEM+1u,text)&&strstr(text,"Qwen3 0.6B"));
        assert(som_gambit_press(&g,1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)==SOM_GAMBIT_BRAIN_CHANGED);
        assert(g.ai_model==1&&g.music_genre==1);
        assert(som_gambit_line(&g,SOM_GAMBIT_MODEL_ITEM+1u,text)&&strstr(text,"Qwen3 1.7B"));
        g.cursor=SOM_GAMBIT_CRITIC_ITEM;
        assert(som_gambit_line(&g,SOM_GAMBIT_CRITIC_ITEM+1u,text)&&strstr(text,"ON"));
        assert(som_gambit_press(&g,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_GAMBIT_BRAIN_CHANGED);
        assert(g.critic==0);
        assert(som_gambit_line(&g,SOM_GAMBIT_CRITIC_ITEM+1u,text)&&strstr(text,"off"));
        char brain[16];som_brain_format(&g,brain);assert(!strcmp(brain,"1 0"));
        som_brain_parse(&g,"14 1");assert(g.ai_model==14&&g.critic==1);
        som_brain_parse(&g,"15 0");assert(g.ai_model==0&&g.critic==1);
        som_brain_parse(&g,"5 2");assert(g.ai_model==0&&g.critic==1);
        assert(som_gambit_line(&g,SOM_GAMBIT_ITEMS,text)&&strstr(text,"Exit gambits"));
        assert(som_gambit_line(&g,SOM_GAMBIT_INFINITE_MP+1,text)&&strstr(text,"Infinite MP")&&
               strstr(text,"off"));
        g.cursor=SOM_GAMBIT_DEBUG_ITEM;
        assert(som_gambit_line(&g,SOM_GAMBIT_DEBUG_ITEM+1u,text)&&strstr(text,"AI debug messages")&&strstr(text,"off"));
        assert(som_gambit_press(&g,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_GAMBIT_DEBUG_CHANGED);
        assert(g.debug_messages&&som_gambit_line(&g,SOM_GAMBIT_DEBUG_ITEM+1u,text)&&strstr(text,"ON"));
        /* Infinite MP test switch: B toggles; weights and file text unchanged. */
        {
            SomGambits m;memset(&m,0,sizeof(m));som_gambit_parse(&m,"433341");
            m.open=1;m.cursor=SOM_GAMBIT_INFINITE_MP;
            m.infinite_mp=0;
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_GAMBIT_TEST_SWITCH);
            assert(m.infinite_mp&&m.open);
            char f[SOM_GAMBIT_COUNT+1];som_gambit_format(&m,f);assert(strcmp(f,"433341")==0);
            assert(som_gambit_line(&m,SOM_GAMBIT_INFINITE_MP+1,text)&&strstr(text,"ON"));
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)==SOM_GAMBIT_TEST_SWITCH);
            assert(!m.infinite_mp);
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_GAMBIT_NONE);
            assert(m.cursor==SOM_GAMBIT_DEBUG_ITEM);
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_GAMBIT_NONE);
            assert(m.cursor==SOM_GAMBIT_P2_ITEM&&!m.player2);
            assert(som_gambit_line(&m,SOM_GAMBIT_P2_ITEM+1u,text)&&strstr(text,"Player 2 AI")&&strstr(text,"off"));
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)==SOM_GAMBIT_P2_CHANGED&&m.player2);
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_GAMBIT_NONE);
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_GAMBIT_WHACK_CHANGED&&m.no_whack);
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_GAMBIT_NONE);
            assert(m.cursor==SOM_GAMBIT_FIGHT_ITEM);
            assert(som_gambit_press(&m,1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)==SOM_GAMBIT_NONE);
            assert(m.cursor==SOM_GAMBIT_EXIT_ITEM);
        }
        assert(!som_gambit_line(&g,SOM_GAMBIT_ITEMS+1,text));
        /* Combat page: its own weights, digits and exit row. */
        {
            SomGambits c;memset(&c,0,sizeof(c));som_gambit_parse(&c,"433341");
            som_combat_parse(&c,"x");                              /* invalid: defaults */
            char f[SOM_COMBAT_COUNT+1];som_combat_format(&c,f);assert(!strcmp(f,"32223333"));
            som_combat_parse(&c,"50123450");som_combat_format(&c,f);assert(!strcmp(f,"50123450"));
            c.open=1;som_gambit_turn_page(&c,1);
            assert(c.page==SOM_PAGE_COMBAT&&c.cursor==0);
            assert(som_gambit_line(&c,0,text)&&strstr(text,"COMBAT"));
            assert(som_gambit_line(&c,1,text)&&strstr(text,"Press the attack")&&strstr(text,"5 ooooo"));
            assert(som_gambit_press(&c,1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)==SOM_GAMBIT_CHANGED);
            assert(c.combat[0]==4&&c.weight[0]==4);                /* actions untouched */
            assert(som_gambit_press(&c,1u<<RETRO_DEVICE_ID_JOYPAD_UP)==SOM_GAMBIT_NONE);
            assert(c.cursor==SOM_COMBAT_EXIT_ITEM);
            assert(som_gambit_line(&c,SOM_COMBAT_ITEMS,text)&&strstr(text,"Exit gambits"));
            assert(!som_gambit_line(&c,SOM_COMBAT_ITEMS+1,text));
            assert(som_gambit_press(&c,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_GAMBIT_EXIT&&!c.open);
            som_gambit_turn_page(&c,1);assert(c.page==SOM_PAGE_QUEST);
            som_gambit_turn_page(&c,1);assert(c.page==SOM_PAGE_ACTIONS);
            som_gambit_turn_page(&c,0);assert(c.page==SOM_PAGE_QUEST);
            som_gambit_turn_page(&c,0);assert(c.page==SOM_PAGE_COMBAT);
            /* Saved to its own file, loaded back. */
            char path[]="/tmp/flxtr-combat-XXXXXX";int cfd=mkstemp(path);assert(cfd>=0);close(cfd);
            const char *saved_combat=combat_file,*saved_gambit=gambit_file;
            const char *saved_quest=quest_file,*saved_brain=brain_file;
            char gpath[]="/tmp/flxtr-gambit-XXXXXX";int gfd=mkstemp(gpath);assert(gfd>=0);close(gfd);
            char qpath[]="/tmp/flxtr-quest-XXXXXX";int qfd=mkstemp(qpath);assert(qfd>=0);close(qfd);
            char bpath[]="/tmp/flxtr-brain-XXXXXX";int bfd=mkstemp(bpath);assert(bfd>=0);close(bfd);
            combat_file=path;gambit_file=gpath;quest_file=qpath;brain_file=bpath;
            SomGambits keep=gambits;gambits=c;som_gambit_save();
            memset(&gambits,0,sizeof(gambits));som_gambit_load();
            assert(gambits.combat[0]==4&&gambits.combat[1]==0&&gambits.weight[0]==4);
            assert(gambits.page==SOM_PAGE_ACTIONS);
            gambits=keep;combat_file=saved_combat;gambit_file=saved_gambit;
            quest_file=saved_quest;brain_file=saved_brain;
            unlink(path);unlink(gpath);unlink(qpath);unlink(bpath);
        }
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
        /* VRAM capture: source art must be remapped to the destination's
         * own tile IDs. The previous map's VRAM/palette must stay intact. */
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
            memset(vc.tiles,0x33,sizeof(vc.tiles));
            memset(vc.palette,0x66,sizeof(vc.palette));
            som_familiar_capture_vram(&vc,ram,vram,home,130);
            assert(vc.has_vram&&vc.tile_count==8&&vc.palette_address==0xDFE0);
            /* sprites at 0xA0 and 0xA2: A0 A1 B0 B1 A2 A3 B2 B3; zero fields skipped */
            memset(vram+0xC000,0,0x4000);memset(ram+0xDFE0,0,0x20);  /* new map */
            memset(ram+0xE600,0,0x600);
            som_fam_put16(ram+0xE600+0x8A,0x2A40);  /* reserved OBJ tile 40, palette 5 */
            int slot=som_familiar_restore(&vc,ram,vram);
            assert(slot==0&&som_fam_u16(ram+0xE600+0x8A)==0x2A40);  /* destination layout */
            assert(vram[0xC000+0x40*32]==0xA0&&vram[0xC000+0x51*32]==0xB1);
            assert(vram[0xC000+0xA0*32]==0);                         /* old map untouched */
            assert(vram[0xC000+0x60*32]==0);                         /* other slots untouched */
            assert(ram[0xDFA0]==0x66&&ram[0x07A0]==0x66);           /* target palette */
            assert(ram[0xDFE0]==0&&ram[0x07E0]==0);                 /* old palette untouched */
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
    /* Direct weapon switching reproduces the game's equip refresh ($C0:EA41)
     * from the ROM: table pointers plus the 64 effect tiles in 7E:2000 (the
     * missing tiles were the garbled weapons). Synthetic Turbo-layout ROM. */
    {
        static uint8_t rom[0x200000];
        memset(rom,0,sizeof(rom));
        som_rom=rom;som_rom_size=sizeof(rom);
        static const uint8_t power[8]={2,3,4,4,2,3,2,2};
        for(unsigned row=0;row<72;row++){
            uint8_t *r=rom+0x101000+row*12;
            r[0]=(uint8_t)(row/9);r[2]=0xC0;r[4]=(uint8_t)(0x10+row/9);
            r[7]=0x4B;r[8]=power[row/9];r[11]=0x21;
        }
        rom[0x101000+10*12+1]=0x40;              /* tier-1 sword: Agi bonus */
        rom[0x101000+54*12+3]=3;                 /* boomerang graphics set 3 */
        rom[0x101000+45*12+3]=0xF0;              /* bow: Turbo alternate bank */
        for(unsigned g=0;g<8;g++){
            uint8_t *t=rom+0x14C000+4*g;
            const unsigned w=0x30u*g+1u;          /* tile index into bank D4 */
            t[0]=(uint8_t)w;t[1]=(uint8_t)(w>>8);t[2]=0xE2;t[3]=(uint8_t)(0x30+2*g);
            rom[0x10FFD0+2*g]=0x40;rom[0x10FFD0+2*g+1]=(uint8_t)(0x20+2*g);
            for(unsigned i=0;i<64*24;i++)
                rom[0x140000+24*w+i]=(uint8_t)(i*7u+g*31u+(i>>4));
        }
        for(unsigned i=0;i<64*24;i++)rom[0x14BA00+i]=(uint8_t)(0xA5^i);
        rom[0x102AD0+0x0C*64+8]=0x81;rom[0x102AD0+0x0C*64+15]=2; /* Fire Ball */
        rom[0x102AD0+0x07*64+8]=0x01;            /* an ally spell */
        /* Independent reference for Turbo's converter: per tile 8 (a,b)
         * row pairs then 8 c bytes; colour = a|b<<1|c<<2 unless b&c. */
        static uint8_t expect[0x800];
        #define EXPECT_TILES(src) do{ \
            for(unsigned t_=0;t_<64;t_++)for(unsigned y_=0;y_<8;y_++){ \
                const uint8_t *s_=rom+(src)+t_*24; \
                uint8_t a_=s_[2*y_],b_=s_[2*y_+1],c_=s_[16+y_],k_=(uint8_t)~(b_&c_); \
                expect[t_*32+2*y_]=a_&k_;expect[t_*32+2*y_+1]=b_&k_; \
                expect[t_*32+16+2*y_]=c_&k_;expect[t_*32+17+2*y_]=0;}}while(0)

        backend_retro_get_memory_data=mocked_memory_data;
        backend_retro_get_memory_size=mocked_memory_size;
        mocked_has_vram=1;
        memset(mocked_wram,0,sizeof(mocked_wram));
        memset(mocked_vram,0x5A,sizeof(mocked_vram));
        uint8_t *ram=mocked_wram,*hero=ram+0xE000;
        ram[0xE4]=1;ram[0xE6]=0x0F;hero[0]=1;
        memset(ram+0xCC54,0xFF,8);
        ram[0xCC55]=0x40;ram[0xCC56]=0;ram[0xCC59]=0;ram[0xCC5A]=0;
        hero[0x1C0]=0x12;                 /* glove level 1, sword level 2 */
        hero[0x1C1]=0x30;                 /* axe 3, spear 0 */
        hero[0x077]=0x40;hero[0x078]=0x30;
        hero[0x188]=22;hero[0x197]=80;hero[0x198]=25;hero[0x069]=0x18;
        /* The game holds the sword: its refresh loaded these. */
        hero[0x1E4]=1;hero[0x1E3]=hero[0x1E8]=9;hero[0x19C]=2;
        hero[0x068]=1;hero[0x065]=0x30;hero[0x066]=0x31;
        hero[0x06A]=0xE2;hero[0x06B]=0x32;hero[0x07E]=0x40;hero[0x07F]=0x11;
        hero[0x02A]=0x40;hero[0x02B]=0x22;
        frame_number=1000;weapon_gfx_verified=0;weapon_gfx_last_check=0;
        /* Fail closed: the model is not trusted until it reproduces the
         * tiles the game itself loaded. */
        som_weapon_gfx_verify_frame(ram);
        assert(!weapon_gfx_verified);
        uint8_t hero_before[0x200];memcpy(hero_before,hero,sizeof(hero_before));
        assert(som_equip_weapon_direct(ram,2)==SOM_EQUIP_REFUSED);
        assert(memcmp(hero,hero_before,sizeof(hero_before))==0);
        EXPECT_TILES(0x140000+24*(0x30u*1+1));
        memcpy(ram+0x2000,expect,sizeof(expect));
        frame_number+=30;som_weapon_gfx_verify_frame(ram);
        assert(weapon_gfx_verified);
        /* Busy hero (mid-swing): retry later, change nothing. */
        hero[0x01C]=0x20;
        assert(som_equip_weapon_direct(ram,2)==SOM_EQUIP_BUSY&&hero[0x1E4]==1);
        hero[0x01C]=0;
        /* Sword -> axe. */
        assert(som_equip_weapon_direct(ram,2)==SOM_EQUIP_OK);
        assert(hero[0x1E4]==2&&hero[0x1E3]==18&&hero[0x1E8]==18&&hero[0x068]==2);
        assert(hero[0x198]==26&&hero[0x197]==80&&hero[0x19C]==3&&hero[0x196]==9);
        assert(hero[0x065]==(uint8_t)(0x3040+240*2)&&hero[0x066]==(0x3040+240*2)>>8);
        assert(hero[0x06A]==0xE2&&hero[0x06B]==0x34&&hero[0x07E]==0x40&&hero[0x07F]==0x12);
        assert(hero[0x02A]==0x40&&hero[0x02B]==0x24&&ram[0xD030]==0);
        assert(hero[0x069]==0x18);                 /* palette id kept when set */
        EXPECT_TILES(0x140000+24*(0x30u*2+1));
        assert(memcmp(ram+0x2000,expect,sizeof(expect))==0);
        assert(ram[0xCC55]==0x00&&ram[0xCC56]==0x40);
        assert(mocked_vram[0xC000]==0x5A&&mocked_vram[0xFFFF]==0x5A); /* no VRAM writes */
        /* Back to the sword restores its stats and tiles exactly. */
        assert(som_equip_weapon_direct(ram,1)==SOM_EQUIP_OK);
        assert(hero[0x198]==25&&hero[0x19C]==2&&hero[0x196]==6&&ram[0xCC55]==0x40);
        EXPECT_TILES(0x140000+24*(0x30u*1+1));
        assert(memcmp(ram+0x2000,expect,sizeof(expect))==0);
        assert(som_weapon_gfx_matches(ram));
        /* Boomerang: graphics set 3, crit halved for types above 4. */
        hero[0x1C3]=0x50;                           /* boomerang level 5 */
        assert(som_equip_weapon_direct(ram,6)==SOM_EQUIP_OK);
        assert(hero[0x196]==7&&ram[0xD030]==3&&hero[0x07F]==0x16);
        assert(som_equip_weapon_direct(ram,1)==SOM_EQUIP_OK);
        /* Refused, unchanged: unowned, Turbo alternate bank, stat-bonus row. */
        memcpy(hero_before,hero,sizeof(hero_before));
        assert(som_equip_weapon_direct(ram,3)==SOM_EQUIP_REFUSED);   /* unowned */
        ram[0xCC59]=0;
        assert(som_equip_weapon_direct(ram,5)==SOM_EQUIP_REFUSED);   /* bow F0 */
        ram[0xCC55]=0x41;hero[0x1E3]=hero[0x1E8]=10;                 /* tier-1 sword */
        memcpy(hero_before,hero,sizeof(hero_before));
        assert(som_equip_weapon_direct(ram,2)==SOM_EQUIP_REFUSED);
        assert(memcmp(hero,hero_before,sizeof(hero_before))==0);
        ram[0xCC55]=0x40;hero[0x1E3]=hero[0x1E8]=9;
        assert(som_cycle_weapon(ram)&&hero[0x1E4]==2);               /* next owned */
        assert(som_equip_weapon_direct(ram,1)==SOM_EQUIP_OK);

        /* Buttons: Swordsmanship = sword on B, axe on Y. */
        som_disc_init(&disciplines);disciplines.selected=0;
        ring_ai_enabled=0;som_manual_disciplines=0;
        on_turbo=1;special_stage=special_suppress=0;
        physical_state=mocked_input_mask;mocked_buttons=0;last_physical_buttons=0;
        som_input_poll();
        /* R2 guards and holds the game's R (block pose). R1 is an
         * edge-triggered style cycle and never reaches the SNES core.
         * Per-button and bitmask reads must agree. */
        som_styles_init(&styles);
        assert(som_custom_weapon_name(&styles,SOM_DISC_BUSHIDO,0u)[0]=='K');
        assert(!strcmp(som_custom_weapon_name(&styles,SOM_DISC_BUSHIDO,0u),"Katana"));
        assert(!strcmp(som_custom_weapon_name(&styles,SOM_DISC_BUSHIDO,1u),"Yumi"));
        assert(!strcmp(som_custom_weapon_name(&styles,SOM_DISC_ARTIFICER,1u),"Crossbow"));
        assert(!strcmp(som_custom_weapon_name(&styles,SOM_DISC_FLOW,2u),"War Bow"));
        const uint16_t guard_r2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R2);
        const uint16_t style_r1=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);
        mocked_buttons=guard_r2;som_input_poll();
        assert(som_guarding());
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2)==0);
        assert((uint16_t)som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==style_r1);
        physical_state=mocked_input;pressed=RETRO_DEVICE_ID_JOYPAD_R2;
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2)==0);
        pressed=100;physical_state=mocked_input_mask;
        mocked_buttons=0;som_input_poll();assert(!som_guarding());
        /* Guard absorbs half of the selected actor's HP loss and leaves
         * the other actor's damage and both animation bytes alone. */
        const uint8_t boy_bind=ram[0xD9],girl_bind=ram[0xDA];
        const uint16_t boy_hp=som_word(hero+0x182),girl_hp=som_word(ram+0xE382);
        const uint8_t girl_active=ram[0xE200];
        ram[0xD9]=2;ram[0xDA]=1;ram[0xE200]=1;
        som_put_word(hero+0x182,40);som_put_word(ram+0xE382,40);
        const uint8_t girl_pose=ram[0xE211],boy_pose=hero[0x11];
        mocked_buttons=guard_r2;som_input_poll();
        som_put_word(hero+0x182,30);som_put_word(ram+0xE382,30);
        som_guard_finish();
        assert(som_word(hero+0x182)==30&&som_word(ram+0xE382)==35);
        assert(hero[0x11]==boy_pose&&ram[0xE211]==girl_pose);
        ram[0xD9]=boy_bind;ram[0xDA]=girl_bind;ram[0xE200]=girl_active;
        som_put_word(hero+0x182,boy_hp);som_put_word(ram+0xE382,girl_hp);
        mocked_buttons=0;som_input_poll();
        for(unsigned i=0;i<3u;i++){
            mocked_buttons=style_r1;som_input_poll();
            assert(styles.selected[0]==(i==0u?2u:i==1u?0u:1u));
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==0);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
            som_input_poll(); /* holding R1 must not cycle again */
            assert(styles.selected[0]==(i==0u?2u:i==1u?0u:1u));
            mocked_buttons=0;som_input_poll();
        }
        {   /* Select gave pad 1 to the girl (7E:00D9 bit 1): R1 turns her
             * class's style, L2+R1 / L2+L1 her class; the boy's stay. */
            const uint8_t keep_d9=ram[0xD9],keep_da=ram[0xDA],keep_party=disciplines.party[0];
            ram[0xD9]=2;ram[0xDA]=1;
            disciplines.party[0]=SOM_DISC_BUSHIDO;styles.selected[SOM_DISC_BUSHIDO]=SOM_STYLE_DEFAULT;
            assert(som_pad1_member()==1u);
            mocked_buttons=style_r1;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(styles.selected[SOM_DISC_BUSHIDO]==2u&&styles.selected[0]==1u);
            assert(ally_equip_pending&2u);
            const uint16_t l2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
            mocked_buttons=l2;som_input_poll();
            mocked_buttons=(uint16_t)(l2|style_r1);som_input_poll();mocked_buttons=0;som_input_poll();
            assert(disciplines.party[0]==(SOM_DISC_BUSHIDO+1u)%SOM_DISCIPLINE_COUNT&&
                   disciplines.selected==0u&&styles.selected[SOM_DISC_BUSHIDO]==2u);
            mocked_buttons=l2;som_input_poll();
            mocked_buttons=(uint16_t)(l2|(1u<<RETRO_DEVICE_ID_JOYPAD_L));som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(disciplines.party[0]==SOM_DISC_BUSHIDO&&disciplines.selected==0u);
            disciplines.party[0]=SOM_DISCIPLINE_NONE;
            mocked_buttons=style_r1;som_input_poll();
            mocked_buttons=(uint16_t)(style_r1|l2);som_input_poll();
            assert(disciplines.party[0]==disciplines.order[0]&&disciplines.selected==0u);
            mocked_buttons=0;som_input_poll();

            /* Y equips a missing secondary weapon from the companion's
             * own verified art, even when the Boy's art is unverified. */
            disciplines.party[0]=SOM_DISC_SWORD;
            uint8_t *girl=ram+0xE200;
            uint8_t saved_girl[0x200],saved_art[SOM_HERO_EFFECT_BYTES];
            memcpy(saved_girl,girl,sizeof(saved_girl));
            memcpy(saved_art,ram+0x3000,sizeof(saved_art));
            const uint8_t saved_gfx=ram[0xD070],saved_axe=ram[0xCC56];
            const unsigned keep_boy_weapon=hero[0x1E4];
            const int saved_verified=weapon_gfx_verified;
            memcpy(girl,hero,sizeof(saved_girl));
            girl[0x182]=girl[0x184]=50;
            SomWeaponModel girl_model;
            assert(som_weapon_model_for(girl,girl[0x1E3],&girl_model,1u));
            ram[0xD070]=girl_model.graphics;
            memcpy(ram+0x3000,girl_model.tiles,sizeof(girl_model.tiles));
            styles.selected[SOM_DISC_SWORD]=SOM_STYLE_DEFAULT;
            ram[0xCC56]=0xFF;weapon_gfx_verified=0;
            ally_equip_pending|=2u;ally_equip_tried=0;
            som_ally_equip_frame(ram);
            assert(ram[0xCC56]==0&&!(ally_equip_pending&2u));
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
            assert(y_weapon_swap&&girl[0x1E4]==2&&hero[0x1E4]==keep_boy_weapon);
            mocked_buttons=0;som_input_poll();
            ram[0xD070]=saved_gfx;ram[0xCC56]=saved_axe;weapon_gfx_verified=saved_verified;
            memcpy(girl,saved_girl,sizeof(saved_girl));
            memcpy(ram+0x3000,saved_art,sizeof(saved_art));
            const uint8_t keep_girl_weapon=ram[0xE3E4];
            ram[0xE3E4]=som_disc_weapons[SOM_DISC_SWORD][1];          /* she holds her Y weapon */
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
            assert(y_weapon_swap&&!y_weapon_blocked&&hero[0x1E4]==keep_boy_weapon);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
            mocked_buttons=0;som_input_poll();
            ram[0xE3E4]=5u;                        /* a bow, not her class's Y: no swap */
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
            assert(y_weapon_blocked&&hero[0x1E4]==keep_boy_weapon);
            mocked_buttons=0;som_input_poll();
            ram[0xE3E4]=keep_girl_weapon;
            ram[0xD9]=keep_d9;ram[0xDA]=keep_da;disciplines.party[0]=keep_party;
        }
        /* Old L1+B style shortcut is moved to L3+B. Its B is hidden,
         * and pressing L1 alone does not send an unrelated native L. */
        const uint16_t l3=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L3);
        mocked_buttons=(uint16_t)(l3|(1u<<RETRO_DEVICE_ID_JOYPAD_B));som_input_poll();
        assert(styles.selected[0]==2u&&b_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        mocked_buttons=0;som_input_poll();
        /* The old L3+X ring chord is consumed. */
        mocked_buttons=(uint16_t)(l3|(1u<<RETRO_DEVICE_ID_JOYPAD_X));som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2)==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
        mocked_buttons=0;som_input_poll();
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        som_input_poll();                           /* Y: axe now, swing as B */
        assert(hero[0x1E4]==2&&y_weapon_swap&&!y_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
        mocked_buttons=0;som_input_poll();
        hero[0x01C]=0x20;                           /* axe swing still playing */
        const unsigned string_hp=som_word(hero+0x182);som_put_word(hero+0x182,50);
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        som_input_poll();                           /* B waits: no axe swing */
        assert(hero[0x1E4]==2&&b_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        hero[0x008]=1;hero[0x045]=10;               /* an earlier jump left over */
        hero[0x01C]=0;som_input_poll();             /* still held, hero free */
        /* The sword, and the string's first strike (B hidden from the game). */
        assert(hero[0x1E4]==1&&b_weapon_blocked&&monk_chain_active&&hero[0x01C]==0x20);
        assert(!hero[0x008]&&!hero[0x045]);              /* a cancelled jump is over */
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
        hero[0x01C]=0;som_monk_chain_frame(ram);som_input_poll();
        assert(!monk_chain_active&&!hero[0x01C]);      /* one press, one strike */
        monk_chain_step=0;hero[0x010]=0;hero[0x011]=0;hero[0x030]=0;som_put_word(hero+0x182,string_hp);
        mocked_buttons=0;som_input_poll();
        /* Y never attacks with the wrong weapon when the switch is refused. */
        ram[0xCC56]=0xFF;
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        som_input_poll();
        assert(y_weapon_blocked&&hero[0x1E4]==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
        mocked_buttons=0;som_input_poll();
        ram[0xCC56]=0;
        /* The old L3+Y ring chord is consumed in both input APIs. */
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L3)|(1u<<RETRO_DEVICE_ID_JOYPAD_Y));
        som_input_poll();
        assert(!special_stage&&!y_weapon_swap);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2)==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        mocked_buttons=0;som_input_poll();

        /* Level-2 secondary special switches to the axe, then charges. */
        hero[0x19B]=0;hero[0x1ED]=0;
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                                   (1u<<RETRO_DEVICE_ID_JOYPAD_Y));
        som_input_poll();
        assert(special_stage==2&&special_weapon==2&&hero[0x1E4]==2);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==
               (1u<<RETRO_DEVICE_ID_JOYPAD_B));
        mocked_buttons=0;som_input_poll();
        hero[0x19B]=2;som_special_tick();
        assert(!special_stage);
        hero[0x19B]=0;som_input_poll();
        /* Level-1 primary special switches back to the sword. */
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                                   (1u<<RETRO_DEVICE_ID_JOYPAD_B));
        som_input_poll();
        assert(special_stage==1&&special_weapon==1&&hero[0x1E4]==1);
        mocked_buttons=0;som_input_poll();
        hero[0x19B]=1;som_special_tick();
        assert(!special_stage);
        hero[0x19B]=0;mocked_buttons=0;som_input_poll();
        /* Seals: L2 + three D-pad presses + B = level 3 without holding. */
        hero[0x19C]=5;
        const uint16_t l2b=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        mocked_buttons=l2b;som_input_poll();
        const unsigned seals[3]={RETRO_DEVICE_ID_JOYPAD_UP,RETRO_DEVICE_ID_JOYPAD_LEFT,
                                 RETRO_DEVICE_ID_JOYPAD_DOWN};
        for(unsigned i=0;i<3u;i++){
            mocked_buttons=(uint16_t)(l2b|(1u<<seals[i]));som_input_poll();
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,seals[i])==0);  /* hidden */
            mocked_buttons=l2b;som_input_poll();
        }
        assert(seal_count==3);
        mocked_buttons=(uint16_t)(l2b|(1u<<RETRO_DEVICE_ID_JOYPAD_B));
        som_input_poll();
        assert(special_stage==3&&seal_count==0);
        mocked_buttons=0;som_input_poll();
        ram[0xE01A]=1;hero[0x01C]=0x20;som_special_tick();    /* swing still on */
        assert(hero[0x19B]==0&&special_stage==3);
        hero[0x01C]=0;som_special_tick();                     /* gauge started */
        assert(ram[0xE01A]==0x2C&&ram[0xE01B]==3&&hero[0x19B]==3);
        assert(special_stage==0);                             /* released */
        ram[0xE01A]=ram[0xE01B]=0;hero[0x19B]=0;som_input_poll();
        /* More seals than learned: capped at the weapon level. */
        hero[0x19C]=2;
        assert(som_special_start(0,6)&&special_stage==2);
        special_stage=0;special_suppress=0;hero[0x19C]=1;
        /* Bushido X: Iaijutsu, katana level 3 on a tap, level 4 held,
         * never a spell, one per SOM_IAI_COOLDOWN_FRAMES; AI iai_3/iai_4. */
        {
            const unsigned keep=disciplines.selected;
            const uint16_t xb=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
            disciplines.selected=SOM_DISC_BUSHIDO;hero[0x19C]=5;hero[0x170]=0;
            assert(som_class_spell_count(SOM_DISC_BUSHIDO)==0);
            som_next_spell();                                 /* no spells: no-op */
            mocked_buttons=xb;som_input_poll();
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
            mocked_buttons=0;som_input_poll();
            assert(special_stage==3&&special_weapon==1&&hero[0x170]==0);
            special_stage=0;special_suppress=0;som_input_poll();
            mocked_buttons=xb;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(!special_stage);                           /* cooling down */
            frame_number+=SOM_IAI_COOLDOWN_FRAMES;
            mocked_buttons=xb;som_input_poll();
            frame_number+=SOM_X_HOLD_FRAMES;som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(special_stage==4);
            special_stage=0;special_suppress=0;som_input_poll();
            assert(!som_handle_command("iai_3"));             /* cooling down */
            frame_number+=SOM_IAI_COOLDOWN_FRAMES;
            assert(som_handle_command("iai_3")&&special_stage==3);
            special_stage=0;special_suppress=0;som_input_poll();
            disciplines.selected=0;frame_number+=SOM_IAI_COOLDOWN_FRAMES;
            assert(!som_handle_command("iai_4"));             /* not a samurai */
            /* Weapon Flow X: the bow (third weapon); the game sees B. */
            const uint8_t bow_owned=ram[0xCC59];ram[0xCC59]=0;
            disciplines.selected=SOM_DISC_WEAPON_FLOW;
            assert(som_class_spell_count(SOM_DISC_WEAPON_FLOW)==0);
            /* Equipping needs the bow's art rows (not in the fixture ROM): with
             * the bow already in hand X must attack with it as B. */
            hero[0x1E4]=5;
            mocked_buttons=xb;som_input_poll();
            assert(hero[0x1E4]==5&&x_weapon_swap);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
            mocked_buttons=0;som_input_poll();
            assert(!x_weapon_swap&&hero[0x170]==0);
            ram[0xCC59]=bow_owned;disciplines.selected=keep;hero[0x1E4]=1;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
            mocked_buttons=0;som_input_poll();
            hero[0x19C]=1;hero[0x01C]=0;hero[0x060]=0;
        }
        /* Combo finisher: a level-3 chain, then hold Y: the secondary
         * weapon charges to level 3, or the level it learned. */
        mocked_buttons=0;som_input_poll();
        som_combo_clear();combo.hits=5;combo.mixes=1;combo.hit_frame=frame_number;hero[0x19C]=2;
        hero[0x1ED]=0;hero[0x19B]=0;hero[0x01C]=0;
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
        assert(!special_stage);                               /* not held long enough */
        frame_number+=SOM_COMBO_HOLD_FRAMES;som_input_poll();
        assert(special_weapon==som_disc_weapons[disciplines.selected][1]&&
               special_stage==(hero[0x19C]<3u?hero[0x19C]:3u));
        assert(!combo.hits&&special_suppress);                /* chain spent */
        mocked_buttons=0;som_input_poll();
        special_stage=0;special_suppress=0;hero[0x19C]=1;
        /* A: a proxy-driven dodge (the game's poses do not play in field
         * play). Divine Arts has no dodge: native A (dash). */
        disciplines.selected=4;
        const uint16_t r1a=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
        mocked_buttons=r1a;som_input_poll();
        assert(!evade_active);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==1);
        mocked_buttons=0;som_input_poll();
        memset(ram+0x10000,0,0x10000);memset(ram+0x1B800,0,0x800);   /* open floor */
        ram[0xC0]=0x00;ram[0xC1]=0x02;ram[0xC2]=0x00;ram[0xC3]=0x02;   /* 512x512 */
        /* Artificer A: the plain evade, 2 px x 12 frames away from the
         * facing, untouchable for the move plus the grace frames; A, D-pad
         * and attacks hidden. No Kawarimi on A. */
        for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
        uint8_t *foe=ram+0xE600;
        disciplines.selected=14;hero[0x191]=0;hero[0x01C]=0;hero[0x011]=0;
        hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;   /* facing right */
        ram[0xA8]=80;ram[0xA9]=0;ram[0xAA]=100;ram[0xAB]=0;             /* camera */
        ram[0xB0]=116;ram[0xB1]=0;ram[0xAC]=30;ram[0xAD]=0;ram[0xB4]=50;ram[0xB5]=1;
        mocked_buttons=r1a;som_input_poll();
        assert(evade_active&&!kawarimi_armed_until&&!(hero[0x191]&0x04));
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==0);
        mocked_buttons=0;som_input_poll();
        {
            const unsigned long hop_start=frame_number;
            hero[0x182]=100;hero[0x183]=0;
            for(unsigned f=0;f<40;f++){
                if(frame_number-hop_start==10u){
                    hero[0x059]=1;hero[0x1F1]=25;som_hop_step();
                    assert(!hero[0x059]&&!hero[0x1F1]);
                    hero[0x182]=80;som_evade_tick();assert(hero[0x182]==100);
                }else{som_hop_step();som_evade_tick();}
                frame_number++;
            }
            assert(som_bun_u16(hero+2)==200-24&&som_bun_u16(hero+4)==200);
            assert(!evade_active&&!kawarimi_log_start&&trail_until>=hop_start+12u);
            /* Screen x 120 -> 96: inside the game's window, so no scroll. */
            assert(som_bun_u16(ram+0xA8)==80&&som_bun_u16(ram+0xB0)==116&&
                   som_bun_u16(ram+0xAC)==30&&som_bun_u16(ram+0xB4)==306&&
                   som_bun_u16(ram+0xAA)==100);
        }
        {   /* Past the window the camera follows as the game's: its layer by
             * the step, the other by its parallax mode; not in a fixed room;
             * a dodge stays on screen when a partner would be left behind. */
            const uint8_t keep_lock=ram[0x10E],keep_b9=ram[0xB9],keep_mode=ram[0xAA0];
            const uint8_t keep_girl=ram[0xE200],keep_sprite=ram[0xE400];
            uint8_t keep_c4[4];memcpy(keep_c4,ram+0xC4,4);
            ram[0xC4]=0;ram[0xC5]=2;ram[0xC6]=0;ram[0xC7]=2;
            ram[0xE200]=ram[0xE400]=0;ram[0xFF]=0;ram[0x10E]=1;ram[0xB9]=1;
            #define CAM_DODGE(camx) do{som_put_word(ram+0xA8,(camx));som_put_word(ram+0xB0,(camx));\
                som_put_word(ram+0xAC,100);som_put_word(ram+0xB4,4);hero[2]=200;hero[0x010]=0x02;\
                mocked_buttons=r1a;som_input_poll();mocked_buttons=0;som_input_poll();\
                for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}}while(0)
            ram[0xAA0]=1;CAM_DODGE(105);          /* screen 95 -> window edge 91: 20 px of scroll */
            assert(som_bun_u16(hero+2)==176&&som_bun_u16(ram+0xA8)==85&&som_bun_u16(ram+0xB0)==85);
            assert(som_bun_u16(ram+0xAC)==100-10&&som_bun_u16(ram+0xB4)==((4u-10u)&0x1FFu));
            ram[0xAA0]=2;CAM_DODGE(105);assert(som_bun_u16(ram+0xAC)==100-20);
            ram[0xAA0]=0;CAM_DODGE(105);assert(som_bun_u16(ram+0xAC)==100&&som_bun_u16(ram+0xA8)==85);
            ram[0x10E]=0;CAM_DODGE(105);assert(som_bun_u16(ram+0xA8)==105&&som_bun_u16(hero+2)==176);
            CAM_DODGE(170);assert(som_bun_u16(hero+2)-170u>=(unsigned)SOM_CAM_EDGE);   /* stays on screen */
            assert(!som_blink_spot_ok(ram,180,200));
            ram[0x10E]=1;ram[0xE200]=1;ram[0xE22C]=1;ram[0xE202]=(uint8_t)(105+246);ram[0xE203]=1;
            ram[0xE204]=200;ram[0xE205]=0;        /* player 2's girl at the screen's right edge */
            CAM_DODGE(105);assert(som_bun_u16(ram+0xA8)==105&&som_bun_u16(hero+2)==176);
            som_blink_bring_party(ram);assert(som_bun_u16(ram+0xE202)!=som_bun_u16(hero+2));  /* in view */
            ram[0xE202]=20;ram[0xE203]=0;som_blink_bring_party(ram);
            assert(som_bun_u16(ram+0xE202)==som_bun_u16(hero+2));
            ram[0xE22C]=0;
            /* A jump out of the window: the camera walks after him. */
            ram[0xE200]=0;som_put_word(ram+0xA8,150);camera_catch_until=frame_number+60;
            for(unsigned f=0;f<20;f++){som_camera_catch_frame(ram);frame_number++;}
            assert(som_bun_u16(hero+2)-som_bun_u16(ram+0xA8)==SOM_CAM_LEFT&&!camera_catch_until);
            #undef CAM_DODGE
            ram[0x10E]=keep_lock;ram[0xB9]=keep_b9;ram[0xAA0]=keep_mode;
            ram[0xE200]=keep_girl;ram[0xE400]=keep_sprite;memcpy(ram+0xC4,keep_c4,4);
            som_put_word(ram+0xB0,116);som_put_word(ram+0xAC,30);som_put_word(ram+0xB4,306);
        }
        /* Rogue A off a combat map: the game has no cartwheel there, so a
         * hop of the cartwheel's own length (2 x 13 = 26 px); A held right
         * moves right. */
        disciplines.selected=9;hero[2]=200;ram[0xA8]=100;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(!cartwheel_native&&!(hero[0x0E]&0x80u));
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(som_bun_u16(hero+2)==200+26&&!evade_active&&som_bun_u16(ram+0xA8)==100);
        /* On a combat map ($ED bit 7) the game's own cartwheel: reaction
         * 0x13 in E1E5, the hero turned to face away from the way held (it
         * goes backwards), no proxy move; the pose plays, nothing falls back. */
        ram[0xED]=0x80;hero[0x1E5]=0xFF;hero[2]=200;hero[3]=0;hero[0x010]=0x02;hero[0x01C]=0;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(cartwheel_native&&hero[0x1E5]==0x13&&hero[0x010]==0x82&&!hop_frames_left);
        assert(evade_frames==30u+SOM_DODGE_GRACE);
        hero[0x1E5]=0xFF;hero[0x01C]=0x40;hero[0x011]=0x93;     /* the combat tick took it */
        for(unsigned f=0;f<20;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(evade_active&&cartwheel_native&&som_bun_u16(hero+2)==200);
        hero[0x01C]=0;hero[0x011]=0;
        for(unsigned f=0;f<20;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(!evade_active&&!cartwheel_native&&som_bun_u16(hero+2)==200);
        /* The game refuses (the pose never starts): after 8 frames the
         * request is withdrawn and the cartwheel-length hop (26 px) runs. */
        hero[0x1E5]=0xFF;hero[0x010]=0x82;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(cartwheel_native&&hero[0x1E5]==0x13&&hero[0x010]==0x02);
        for(unsigned f=0;f<60;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(!evade_active&&!cartwheel_native&&hero[0x1E5]==0xFF&&som_bun_u16(hero+2)==200-26);
        /* Busy (already in a pose): straight to the hop. */
        hero[0x01C]=0x40;hero[2]=200;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(!cartwheel_native&&hero[0x1E5]==0xFF);
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        hero[0x01C]=0;ram[0xED]=0;hero[2]=200;hero[3]=0;
        /* Hold A: the hop fires on the press; once it is over and A is still
         * held the game sees A (its run) until A is let go. A tap never runs.
         * Down, Down + A arms Kawarimi (ninja only) instead of the hop. */
        {
            const uint16_t a=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
            const uint16_t down=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN);
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            const uint8_t keep_class=disciplines.selected,keep_weapon=hero[0x1E4];
            disciplines.selected=SOM_DISC_NINJUTSU;hero[0x1E4]=1;
            hero[2]=200;hero[3]=0;hero[0x01C]=0;
            mocked_buttons=a;som_input_poll();assert(evade_active);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;som_input_poll();}
            assert(!evade_active&&!kawarimi_armed_until&&!a_running);
            for(unsigned cls=SOM_DISC_NINJUTSU,k=0;k<2;k++,cls=11u){     /* ninja, archer */
                disciplines.selected=(uint8_t)cls;
                hero[2]=200;hero[3]=0;hero[0x01C]=0;
                mocked_buttons=a;som_input_poll();assert(evade_active);
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==0);
                for(unsigned f=0;f<60&&!a_running;f++){
                    som_hop_step();som_evade_tick();frame_number++;som_input_poll();
                    assert(a_running||som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==0);
                }
                assert(a_running&&!evade_active&&!kawarimi_armed_until);
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==1);
                mocked_buttons=0;som_input_poll();
                assert(!a_running&&som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==0);
                for(unsigned f=0;f<30;f++){som_hop_step();som_evade_tick();frame_number++;}
            }
            /* Down, Down + A: Kawarimi, no hop. */
            disciplines.selected=SOM_DISC_NINJUTSU;
            hero[2]=200;hero[3]=0;hero[0x01C]=0;
            mocked_buttons=down;som_input_poll();frame_number++;
            mocked_buttons=0;som_input_poll();frame_number++;
            mocked_buttons=down;som_input_poll();frame_number++;
            mocked_buttons=(uint16_t)(down|a);som_input_poll();
            assert(kawarimi_armed_until&&!evade_active);
            mocked_buttons=0;som_input_poll();kawarimi_armed_until=0;
            /* Too slow between the Downs: the plain evade. */
            mocked_buttons=down;som_input_poll();frame_number+=SOM_DDA_FRAMES+5u;
            mocked_buttons=0;som_input_poll();frame_number++;
            mocked_buttons=down;som_input_poll();frame_number++;
            mocked_buttons=(uint16_t)(down|a);som_input_poll();
            assert(!kawarimi_armed_until&&evade_active);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
            /* Any other class: Down, Down + A is the evade. */
            disciplines.selected=11;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x01C]=0;
            mocked_buttons=down;som_input_poll();frame_number++;
            mocked_buttons=0;som_input_poll();frame_number++;
            mocked_buttons=down;som_input_poll();frame_number++;
            mocked_buttons=(uint16_t)(down|a);som_input_poll();
            assert(!kawarimi_armed_until&&evade_active);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            /* Samurai: A with a monster in grab reach grabs and throws it. */
            disciplines.selected=SOM_DISC_BUSHIDO;throw_ready=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x182]=50;
            foe[2]=212;foe[3]=0;foe[4]=200;foe[5]=0;
            mocked_buttons=a;
            for(unsigned f=0;f<60&&!throw_active;f++){
                som_hop_step();som_evade_tick();frame_number++;som_input_poll();
            }
            assert(throw_active&&!kawarimi_armed_until);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<90;f++){som_throw_frame(ram);frame_number++;}
            foe[0]=0;foe[0x1F1]=0;throw_active=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            disciplines.selected=keep_class;hero[0x1E4]=keep_weapon;
        }
        /* Samurai A: Ukemi tumbles a collision-checked 54 px without
         * relying on the unreliable native 0x93 cartwheel reaction. */
        {
            const uint8_t keep=disciplines.selected,keep_ed=ram[0xED];
            disciplines.selected=SOM_DISC_BUSHIDO;
            for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
            ram[0xED]|=0x80u;
            /* Up and down: the drawn tumble (facing down; back = up). */
            for(unsigned forward=0;forward<2u;forward++){
                hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;
                hero[0x010]=0x01;hero[0x01C]=0;hero[0x011]=0;
                hero[0x060]=0;hero[0x190]=hero[0x191]=0;hero[0x1E5]=0xFF;
                const uint16_t a=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
                mocked_buttons=(uint16_t)(a|(forward?1u<<RETRO_DEVICE_ID_JOYPAD_DOWN:0u));
                som_input_poll();mocked_buttons=0;som_input_poll();
                assert(evade_active&&!cartwheel_native&&hero[0x1E5]==0xFF);
                assert(hop_dx==0&&hop_dy==(forward?2:-2));
                for(unsigned f=0;f<40u;f++){
                    som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;
                }
                assert(!evade_active&&hero[0x010]==0x01);
                assert(som_bun_u16(hero+4u)==(unsigned)(200+(forward?28:-28)));
            }
            /* Left and right: the game's own cartwheel (no side somersault). */
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;
            hero[0x010]=0x02;hero[0x01C]=0;hero[0x011]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;hero[0x1E5]=0xFF;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
            som_input_poll();mocked_buttons=0;som_input_poll();
            assert(evade_active&&cartwheel_native&&!ukemi_draw.active);
            for(unsigned f=0;f<80u;f++){
                som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;
            }
            hero[0x1E5]=0xFF;
            ram[0xED]=keep_ed;disciplines.selected=keep;
        }
        /* AI "evade_right": the dodge goes right whatever the facing (an
         * escape dodges along its route, not back into what chases it). */
        {
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            const uint8_t keep=disciplines.selected;
            disciplines.selected=14;                                   /* artificer: plain roll */
            for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x82;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            assert(!som_handle_command("evade_sideways"));
            assert(som_handle_command("evade_right")&&evade_active);
            for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
            assert(!evade_active&&som_bun_u16(hero+2)==200+24);
            hero[2]=200;hero[3]=0;hero[0x010]=0x02;
            memcpy(ram+0xA8,camera,sizeof(camera));
            disciplines.selected=keep;
        }
        /* Mage A: Blink step, the carried dodge with the hero hidden while
         * he moves and shown again where he lands. */
        {
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_MAGIC;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;hero[0x0E]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(evade_active&&evade_blink&&(hero[0x0E]&0x80u));
            for(unsigned f=0;f<30;f++){som_hop_step();som_evade_tick();frame_number++;}
            assert(!evade_active&&!evade_blink&&!(hero[0x0E]&0x80u));
            assert(som_bun_u16(hero+2)==200-30);                       /* back from the facing */
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            disciplines.selected=keep;
        }
        /* FlXtR MP/AP: a technique is paid from the class's own pool
         * (Chakra for the ninja) and evades from the AP pips; refused when
         * short, refilled over time, faster standing still. */
        {
            SomEnergy e;som_energy_init(&e);
            som_energy_tick(&e,2,1,0);
            assert(e.energy[2]==SOM_ENERGY_BASE&&e.ap==SOM_AP_MAX);
            assert(!strcmp(som_energy_name(2),"Chakra")&&!strcmp(som_energy_name(1),"Chi"));
            som_energy_take(&e,2,45,4);
            assert(!som_energy_can(&e,2,10,0)&&!som_energy_can(&e,2,0,1)&&som_energy_can(&e,2,5,0));
            for(unsigned f=0;f<SOM_ENERGY_STILL_FRAMES*5u;f++)som_energy_tick(&e,2,1,1);
            assert(e.energy[2]==10&&e.ap==0);
            for(unsigned f=0;f<SOM_AP_REFILL_FRAMES;f++)som_energy_tick(&e,2,1,0);
            assert(e.ap==1);
            assert(som_energy_max(51)==SOM_ENERGY_BASE+100u);
            const uint8_t keep=disciplines.selected;
            disciplines.selected=14;energy_free=0;
            energy.started[14]=1;energy.energy[14]=50;energy.ap=0;
            for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            assert(!som_evade_start(ram)&&!evade_active);               /* no AP: no dodge */
            energy.ap=1;
            assert(som_evade_start(ram)&&evade_active&&energy.ap==0);   /* one pip */
            for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
            hero[2]=200;hero[3]=0;
            energy_free=1;disciplines.selected=keep;
        }
        /* Ninjutsu X then A (Ninja Storm): the tapped jutsu waits, A in the
         * wait is Blink toward the held direction instead, no jutsu. */
        {
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_NINJUTSU;hero[0x1E4]=1;
            for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x82;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;hero[0x0E]=0;hero[0x170]=0;
            blink_ready=0;x_cast_at=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(x_cast_at&&hero[0x170]==0);                       /* waiting */
            frame_number+=3;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
            som_input_poll();mocked_buttons=0;som_input_poll();
            assert(!x_cast_at&&blink_active&&!evade_active&&blink_tx==200+SOM_BLINK_DASH_PX);
            for(unsigned f=0;f<60;f++){som_blink_frame(ram);som_input_poll();frame_number++;}
            assert(!blink_active&&hero[0x170]==0&&som_bun_u16(hero+2)==200+SOM_BLINK_DASH_PX);
            hero[2]=200;hero[3]=0;hero[0x010]=0x02;hero[0x0E]=0;blink_ready=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            disciplines.selected=keep;
        }
        /* Samurai Issen (L2+X): a dash-cut that passes a monster cuts it
         * exactly once; he is untouchable meanwhile; then a cooldown. */
        {
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_BUSHIDO;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x1F1]=0;
            foe[2]=250;foe[3]=0;foe[4]=200;foe[5]=0;foe[0x182]=50;foe[0x183]=0;
            const uint16_t l2x=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X));
            mocked_buttons=l2x;som_input_poll();
            assert(evade_active&&issen_active);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<30;f++){som_hop_step();som_evade_tick();frame_number++;}
            assert(foe[0x1F1]==16u+2u*disciplines.level[SOM_DISC_BUSHIDO]);   /* once */
            assert(som_bun_u16(hero+2)>=200+70&&!evade_active&&!issen_active);
            mocked_buttons=l2x;som_input_poll();assert(!evade_active);        /* cooldown */
            mocked_buttons=0;som_input_poll();
            foe[0]=0;foe[0x1F1]=0;issen_ready=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            disciplines.selected=keep;
        }
        /* Monk Hundred Fists (L2+X): the game sees B on and off, the monster
         * in front takes damage every tick, and it ends on time. */
        {
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_MARTIAL;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x1F1]=0;
            foe[2]=218;foe[3]=0;foe[4]=200;foe[5]=0;foe[0x182]=50;foe[0x183]=0;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X));
            som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(hundred_until);
            int presses=0,released=0;
            for(unsigned f=0;f<SOM_HUNDRED_FRAMES+2u;f++){
                som_hop_step();
                if(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B))presses++;
                else released++;
                frame_number++;
            }
            assert(presses>10&&released>10);
            assert(foe[0x1F1]>=(SOM_HUNDRED_FRAMES/SOM_HUNDRED_TICK-1u)*
                   (3u+disciplines.level[SOM_DISC_MARTIAL]/2u));
            assert(!hundred_until);
            foe[0]=0;foe[0x1F1]=0;hundred_ready=0;
            disciplines.selected=keep;
        }
        /* Records written with 12 classes still load: the new ones start at
         * level 1 (progress "d" lines, styles "s1" lines). */
        {
            SomProgress p;memset(&p,0,sizeof(p));
            for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)p.level[i]=(uint16_t)(i+2u);
            p.selected=SOM_DISCIPLINE_NONE;
            char text[SOM_PROG_TEXT_MAX];size_t n=som_prog_format(&p,text,sizeof(text));
            assert(n);
            char old[SOM_PROG_TEXT_MAX];size_t m=0;
            for(const char *line=text;*line;){
                const char *end=strchr(line,'\n');const size_t len=(size_t)(end-line)+1u;
                unsigned i=0;
                const int newer=sscanf(line,"d %u",&i)==1&&i>=12u;
                if(!newer&&strncmp(line,"sum ",4)){memcpy(old+m,line,len);m+=len;}
                line=end+1;
            }
            m+=(size_t)snprintf(old+m,sizeof(old)-m,"sum %016llx\n",
                                (unsigned long long)som_prog_hash((const uint8_t*)old,m));
            SomProgress q;
            assert(som_prog_parse(old,m,&q)==SOM_PROG_OK);
            assert(q.level[11]==13u&&q.level[12]==1u&&q.level[15]==1u);
            SomStyles st;
            char line[1400];
            strcpy(line,"s1");
            for(unsigned i=0;i<12u;i++)strcat(line," 2");
            for(unsigned i=0;i<36u;i++)strcat(line," 3 7");
            assert(som_styles_parse(&st,line)&&st.selected[11]==2u&&st.level[35]==3u);
            assert(st.selected[12]==SOM_STYLE_DEFAULT&&st.level[36]==1u);
            assert(som_styles_format(&st,line,sizeof(line)));
            SomStyles again;assert(som_styles_parse(&again,line)&&!memcmp(&again,&st,sizeof(st)));
        }
        /* Hunter X: holy water burns what stands in it; Artificer X: a bomb
         * goes off after its fuse; L2+X: a mine a monster sets off;
         * Mentalist X: the nearest monster becomes the familiar, then is
         * let go. */
        {
            const uint8_t keep=disciplines.selected;
            const uint16_t x=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x1F1]=0;foe[0x182]=50;
            foe[2]=240;foe[3]=0;foe[4]=200;foe[5]=0;                   /* 40 px ahead */
            disciplines.selected=SOM_DISC_HUNTER;
            mocked_buttons=x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(holy_until);
            for(unsigned f=0;f<SOM_HOLY_FRAMES+2u;f++){som_hop_step();frame_number++;}
            assert(foe[0x1F1]>=(SOM_HOLY_FRAMES/SOM_HOLY_TICK-1u)*(5u+disciplines.level[SOM_DISC_HUNTER]));
            foe[0x1F1]=0;x_move_ready=0;
            disciplines.selected=SOM_DISC_ARTIFICER;
            foe[2]=224;                                                 /* 24 px ahead */
            mocked_buttons=x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(bomb_at&&foe[0x1F1]==0);
            for(unsigned f=0;f<SOM_BOMB_FUSE+2u;f++){som_hop_step();frame_number++;}
            assert(!bomb_at&&foe[0x1F1]==18u+2u*disciplines.level[SOM_DISC_ARTIFICER]);
            foe[0x1F1]=0;
            mocked_buttons=(uint16_t)(x|(1u<<RETRO_DEVICE_ID_JOYPAD_L2));som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(mine_live[0]);
            foe[2]=200;                                                 /* steps on it */
            som_hop_step();frame_number++;
            assert(!mine_live[0]&&foe[0x1F1]==24u+2u*disciplines.level[SOM_DISC_ARTIFICER]);
            foe[0x1F1]=0;class_move_ready=0;x_move_ready=0;
            disciplines.selected=SOM_DISC_PSIONICS;
            foe[2]=240;
            mocked_buttons=x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(familiar.slot==0&&mind_slot==0&&mind_until);
            frame_number=mind_until;som_hop_step();
            assert(familiar.slot<0&&mind_slot<0&&!familiar_carry.valid);
            /* X again lets the bound one go: the mind (Mentalist), a familiar
             * (Summoner, Necromancer, Dark Arts' shadow pact). */
            x_move_ready=0;
            mocked_buttons=x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(familiar.slot==0&&mind_slot==0);
            mocked_buttons=x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(familiar.slot<0&&mind_slot<0);                    /* released at once */
            for(unsigned c=0;c<2u;c++){
                static const uint8_t binders[2]={6,13};
                disciplines.selected=binders[c];x_move_ready=0;
                familiar.slot=1;familiar_carry.valid=0;
                mocked_buttons=x;som_input_poll();mocked_buttons=0;som_input_poll();
                assert(familiar.slot<0);
            }
            assert(som_disc_x_role(5)==SOM_X_SUMMON&&som_disc_x_role(6)==SOM_X_SUMMON);
            /* Raise Dead's undead by creature: a rabite rises as the Beast
             * Zombie, a goblin as a Zombie, a mushroom as a Ghoul, a bee as a
             * Ghost, a book as a Specter; the undead stay themselves. The
             * re-skin is the game's own: id, restart, refresh request bit 0. */
            assert(som_undead_for(0x00)==SOM_UNDEAD_BEAST&&som_undead_for(0x06)==SOM_UNDEAD_ZOMBIE);
            assert(som_undead_for(0x02)==SOM_UNDEAD_GHOUL&&som_undead_for(0x01)==SOM_UNDEAD_GHOST);
            assert(som_undead_for(0x23)==SOM_UNDEAD_SPECTER&&som_undead_for(0x36)==0x36u);
            assert(som_undead_for(-1)==SOM_UNDEAD_ZOMBIE);
            {
                uint8_t *u=ram+0xE800;uint8_t keep_u[0x200];memcpy(keep_u,u,sizeof(keep_u));
                u[0x180]=0;u[0x1FA]=0;u[0x011]=3;u[0x030]=0;familiar_undead=SOM_UNDEAD_BEAST;
                som_familiar_raise_undead(ram,1);
                assert(u[0x180]==0x35u&&u[0x1E7]==0x35u&&(u[0x1FA]&1u)&&u[0x011]==0&&u[0x030]==0xFFu);
                memcpy(u,keep_u,sizeof(keep_u));familiar_undead=0;
            }
            foe[0]=0;foe[0x1F1]=0;x_move_ready=0;
            disciplines.selected=keep;
        }
        /* L1 exclusively locks onto the nearest monster; a second tap
         * releases, and neither L1+B nor L3 shortcuts steal targeting. */
        {
            const uint16_t l1=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L);
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            som_put_word(hero+0x182,40);som_put_word(hero+0x184,80);
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x182]=50;
            foe[2]=150;foe[3]=0;foe[4]=210;foe[5]=0;                   /* to the left */
            auto_target_ready=0;lock_slot=-1;
            som_auto_target_frame(ram);                                /* baseline */
            foe[0x182]=49;som_auto_target_frame(ram);
            assert(lock_slot==0&&lock_who==0u);                        /* party hit */
            som_lock_release("test");foe[0x182]=50;som_auto_target_frame(ram);
            som_put_word(hero+0x182,39);som_auto_target_frame(ram);
            assert(lock_slot==0&&lock_who==0u);                        /* party member hit */
            som_lock_release("test");som_put_word(hero+0x182,40);
            som_auto_target_frame(ram);
            mocked_buttons=l1;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(lock_slot==0);
            som_hop_step();
            assert(hero[0x010]==0x82);                                 /* faces it */
            hero[0x010]=0x01;som_style_after(ram);assert(hero[0x010]==0x82);
            /* Not during an attack: a turn restarts its script in the air. */
            for(unsigned st=0x20;st<=0x80u;st+=0x60u){
                hero[0x01C]=(uint8_t)st;hero[0x010]=0x01;som_style_after(ram);
                assert(hero[0x010]==0x01);
            }
            hero[0x01C]=0;som_style_after(ram);assert(hero[0x010]==0x82);
            mocked_buttons=l1;som_input_poll();
            mocked_buttons=(uint16_t)(l1|(1u<<RETRO_DEVICE_ID_JOYPAD_B));som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(lock_slot<0);      /* second press toggles immediately */
            hero[0x01C]=0;hero[0x19B]=0;som_monk_chain_frame(ram);monk_chain_step=0;   /* that B's strike ends */
            mocked_buttons=l1;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(lock_slot==0);
            /* Strafing: locked on a foe to the left, Up walks round it (the
             * game is given Up), and the radius holds: drifting out pulls
             * him back in. */
            {
                const uint16_t upb=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_UP);
                uint8_t keep_dims[4];memcpy(keep_dims,ram+0xC0,4);
                ram[0xC0]=0x00;ram[0xC1]=0x02;ram[0xC2]=0x00;ram[0xC3]=0x02;   /* 512x512, open */
                memset(ram+0x10000,0,0x10000);ram[0x1B800]=0;
                hero[2]=200;hero[4]=210;foe[2]=150;foe[4]=210;
                som_style_after(ram);assert(lock_radius==50);              /* standing: taken */
                mocked_buttons=upb;som_input_poll();
                assert(lock_orbiting&&lock_keep_radius);
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP)==1&&
                       som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)==0);
                hero[4]=190;som_style_after(ram);                           /* the game walked up */
                const long ddx=(long)som_bun_u16(hero+2)-150,ddy=(long)som_bun_u16(hero+4)-210;
                assert(som_bun_u16(hero+2)<200&&ddx*ddx+ddy*ddy<53L*53L);  /* pulled in */
                /* Further round, the game is told up-left: the walk bends. */
                hero[2]=180;hero[4]=170;mocked_buttons=upb;som_input_poll();
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP)==1&&
                       som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)==1);
                /* Every dodge profile reuses lock-on's translated tangent:
                 * the same input circles with evade, Cartwheel, Ukemi, or Blink. */
                {
                    int dx=0,dy=0;
                    assert(som_lock_evasion_direction(&dx,&dy));
                    assert(dx==-1&&dy==-1);
                }
                /* Toward it (down, from up here): a plain walk, the radius follows. */
                mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN);som_input_poll();
                assert(!lock_orbiting);
                mocked_buttons=0;som_input_poll();
                memcpy(ram+0xC0,keep_dims,4);hero[2]=200;hero[4]=200;foe[2]=150;foe[4]=210;
            }
            foe[0x191]=0x80;som_hop_step();                            /* it dies */
            assert(lock_slot<0);
            foe[0]=0;foe[0x191]=0;hero[0x010]=0x02;
        }
        /* Masked Bushido weapon art: a valid Dai Katana mask hides only the
         * native effects tiles while it draws, mirrors with the actor, and
         * switching to Katana restores the exact previous bytes. No masks
         * or ROM patches are enabled unless the marker file exists. */
        {
            char mask_path[]="/tmp/flxtr-weapon-mask-test-XXXXXX";
            const int fd=mkstemp(mask_path);assert(fd>=0);
            uint8_t pack[10+44+6];memset(pack,0,sizeof(pack));
            memcpy(pack,"SOMWORK1",8);pack[8]=1;
            pack[10]=0xD5;pack[11]=14;pack[12]=10;pack[13]=1;
            pack[14]=0x1F;                       /* mask palette #1 red */
            pack[54]=1;pack[57]=1;pack[58]=1;pack[59]=1;
            assert(write(fd,pack,sizeof(pack))==(ssize_t)sizeof(pack));close(fd);
            weapon_mask_file=mask_path;weapon_mask_tried=0;
            weapon_mask_on_file=mask_path;weapon_mask_on_checked=0;
            const uint8_t keep=disciplines.selected,old=styles.selected[SOM_DISC_BUSHIDO];
            disciplines.selected=SOM_DISC_BUSHIDO;styles.selected[SOM_DISC_BUSHIDO]=SOM_STYLE_HEAVY;
            hero[0x1E4]=1;hero[0x1E3]=14;hero[0x026]=20;hero[0x027]=0;hero[0x024]=0x81;hero[0x028]=0x40;
            for(unsigned i=0;i<0x800;i++)ram[0x2000+i]=(uint8_t)(i*7u+1u);
            som_weapon_mask_frame(ram);
            assert(weapon_tiles_hidden[0]&&ram[0x2000]==0&&ram[0x27FF]==0);
            som_weapon_mask_pick(ram);
            assert(weapon_masked[0].draw.active&&weapon_masked[0].draw.entry.weapon==14u&&weapon_masked[0].draw.dir==3u);
            hero[0x026]=22;som_weapon_mask_pick(ram);
            assert(!weapon_masked[0].draw.active);            /* this frame only */
            /* The next check restores the game's art, and a frame with
             * replacement art is masked again. */
            som_weapon_mask_frame(ram);
            assert(!weapon_tiles_hidden[0]&&ram[0x2000]==1&&ram[0x2001]==8);
            hero[0x026]=20;som_weapon_mask_frame(ram);som_weapon_mask_pick(ram);
            assert(weapon_tiles_hidden[0]&&weapon_masked[0].draw.active);
            hero[0x027]=1;som_weapon_mask_pick(ram);assert(!weapon_masked[0].draw.active);   /* pose 138 */
            hero[0x027]=0;hero[0x024]=0;som_weapon_mask_pick(ram);
            assert(!weapon_masked[0].draw.active);            /* weapon not shown */
            hero[0x024]=0x81;
            styles.selected[SOM_DISC_BUSHIDO]=SOM_STYLE_BALANCED;
            som_weapon_mask_frame(ram);
            assert(!weapon_tiles_hidden[0]&&ram[0x2000]==1&&ram[0x2001]==8);
            som_weapon_mask_pick(ram);assert(!weapon_masked[0].draw.active);
            unlink(mask_path);weapon_mask_file=SOM_WEAPON_MASK_FILE;weapon_mask_tried=0;
            weapon_mask_on_file=SOM_WEAPON_MASK_ON_FILE;weapon_mask_on_checked=0;
            free(weapon_mask_pack);weapon_mask_pack=NULL;weapon_mask_pack_size=0;
            free(weapon_mask_index);weapon_mask_index=NULL;
            disciplines.selected=keep;styles.selected[SOM_DISC_BUSHIDO]=old;
        }
        /* L2+X Whirlwind (swordsman) cuts what is around him; Volley
         * (archer) hits what is ahead in the cone, not what is behind. */
        {
            const uint8_t keep=disciplines.selected;
            uint8_t *foe2=ram+0xE600+0x200;
            const uint16_t l2x=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X));
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x1F1]=0;foe[0x182]=50;
            foe[2]=230;foe[3]=0;foe[4]=200;foe[5]=0;                   /* 30 px ahead */
            foe2[0]=1;foe2[0x180]=7;foe2[0x190]=foe2[0x191]=0;foe2[0x1F1]=0;foe2[0x182]=50;
            foe2[2]=100;foe2[3]=0;foe2[4]=200;foe2[5]=0;              /* 100 px behind */
            disciplines.selected=SOM_DISC_SWORD;
            mocked_buttons=l2x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(foe[0x1F1]==14u+2u*disciplines.level[SOM_DISC_SWORD]&&foe2[0x1F1]==0);
            foe[0x1F1]=0;class_move_ready=0;
            disciplines.selected=SOM_DISC_ARCHER;
            foe[2]=(uint8_t)300;foe[3]=1;foe[4]=220;                   /* 100 ahead, 20 off */
            mocked_buttons=l2x;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(foe[0x1F1]==10u+disciplines.level[SOM_DISC_ARCHER]&&foe2[0x1F1]==0);
            foe[0]=0;foe2[0]=0;foe[0x1F1]=foe2[0x1F1]=0;class_move_ready=0;
            disciplines.selected=keep;
        }
        /* AI "kawarimi": armed; nothing moves until a monster touches him.
         * Then he reappears 3 x 18 = 54 px (the ninja's Ukemi distance) away from it, the hit is
         * dropped, a log stands on his old spot, and he is untouchable for
         * the move plus the grace frames. */
        disciplines.selected=2;hero[2]=200;
        assert(som_handle_command("kawarimi")&&!evade_active&&kawarimi_armed_until>frame_number);
        assert(!som_handle_command("kawarimi"));                     /* already armed */
        for(unsigned f=0;f<10;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(som_bun_u16(hero+2)==200&&!evade_active&&kawarimi_armed_until);  /* untouched */
        foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x1F1]=0;
        foe[2]=210;foe[3]=0;foe[4]=200;foe[5]=0;              /* touches from the right */
        hero[0x182]=100;hero[0x183]=0;hero[0x059]=1;hero[0x1F1]=25;
        som_hop_step();
        assert(evade_active&&!kawarimi_armed_until&&kawarimi_log_start);
        assert(som_bun_u16(hero+2)==200-28&&som_bun_u16(hero+4)==200);
        assert(kawarimi_log_x==200&&kawarimi_log_y==200);
        assert(!hero[0x059]&&!hero[0x1F1]);                   /* the hit is dropped */
        foe[0]=0;
        const unsigned long dodge_start=frame_number;
        for(unsigned f=0;f<40;f++){
            if(frame_number-dodge_start==10u){                /* a monster hits */
                hero[0x059]=1;hero[0x1F1]=25;som_hop_step();
                assert(!hero[0x059]&&!hero[0x1F1]);
                hero[0x182]=80;som_evade_tick();
                assert(hero[0x182]==100);                     /* HP put back */
            }else{som_hop_step();som_evade_tick();}
            frame_number++;
            if(frame_number-dodge_start==20u)assert(evade_active);
        }
        assert(!evade_active&&som_bun_u16(hero+2)==200-28);
        hero[0x1F1]=7;som_hop_step();som_evade_tick();
        assert(hero[0x1F1]==7);                               /* not after the dodge */
        hero[0x1F1]=0;
        /* The window closes: no swap after SOM_KAWARIMI_ARM_FRAMES. */
        hero[2]=200;
        assert(som_handle_command("kawarimi"));
        frame_number+=SOM_KAWARIMI_ARM_FRAMES+1u;
        foe[0]=1;foe[2]=210;som_hop_step();
        assert(!evade_active&&!kawarimi_armed_until&&som_bun_u16(hero+2)==200);
        foe[0]=0;
        /* A hit with nobody touching fires it too: back from the facing. */
        assert(som_handle_command("kawarimi"));
        hero[0x1F1]=9;som_hop_step();
        assert(evade_active&&!hero[0x1F1]&&som_bun_u16(hero+2)==200-28);
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        /* Dodge Mastery (Ninjutsu 10: rank 2, +4 frames): 36 px; off: 28. */
        disciplines.level[2]=10;hero[2]=200;
        assert(som_handle_command("kawarimi"));
        hero[0x1F1]=9;som_hop_step();
        assert(som_bun_u16(hero+2)==200-36);
        for(unsigned f=0;f<50;f++){som_hop_step();som_evade_tick();frame_number++;}
        perks_enabled=0;hero[2]=200;
        assert(som_handle_command("kawarimi"));
        hero[0x1F1]=9;som_hop_step();
        assert(som_bun_u16(hero+2)==200-28);
        for(unsigned f=0;f<50;f++){som_hop_step();som_evade_tick();frame_number++;}
        perks_enabled=1;disciplines.level[2]=1;hero[0x1F1]=0;
        /* Kawarimi is a ninja skill: monk, archer, rogue and the classes
         * without a dodge are refused. */
        for(unsigned c=0;c<4u;c++){
            static const uint8_t others[4]={1,11,9,4};
            disciplines.selected=others[c];hero[2]=200;hero[4]=200;hero[0x1F1]=0;
            assert(!som_handle_command("kawarimi")&&!kawarimi_armed_until);
        }
        /* Ninja with the sword: A is Ukemi, back with no direction (to the
         * side: a cartwheel, here the hop fallback, 26 px, facing kept); the AI's "shunshin"
         * puts a clone on each side across the facing (clone, ninja,
         * clone); no dash. Not with clones out, not with another weapon. */
        disciplines.selected=2;hero[0x1E4]=1;hero[0x191]=0;hero[2]=200;hero[4]=200;
        hero[0x010]=0x02;
        mocked_buttons=r1a;som_input_poll();mocked_buttons=0;som_input_poll();
        assert(evade_active&&!som_bun_alive(&bunshin));
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(!evade_active&&som_bun_u16(hero+2)==200-26&&hero[0x010]==0x02);
        hero[2]=200;hero[0x1E4]=6;assert(!som_handle_command("shunshin"));hero[0x1E4]=1;
        assert(som_handle_command("shunshin"));
        assert(!evade_active&&!kawarimi_armed_until&&som_bun_alive(&bunshin)==2);
        assert(som_bun_u16(hero+2)==200&&som_bun_u16(hero+4)==200);
        assert(bunshin.x[0]==200&&bunshin.y[0]==168&&bunshin.x[1]==200&&bunshin.y[1]==232);  /* 32 px above / below */
        assert(!som_handle_command("shunshin"));                     /* clones already out */
        assert(bunshin_shunshin);
        /* Shunshin is not a substitution: a monster at the hero sets off
         * both clones, the hero stays put. */
        {
            uint8_t *sm=ram+0xE600;
            sm[0]=1;sm[0x180]=7;sm[0x190]=sm[0x191]=0;sm[0x1F1]=0;
            sm[2]=204;sm[3]=0;sm[4]=200;sm[5]=0;
            frame_number+=SOM_BUN_SWAP_COOLDOWN+1;
            som_bunshin_frame(ram);
            assert(!som_bun_alive(&bunshin)&&!bunshin_shunshin);
            assert(som_bun_u16(hero+2)==200&&som_bun_u16(hero+4)==200);
            sm[0]=0;
            /* Time running out detonates them too. */
            assert(som_handle_command("shunshin")&&bunshin_shunshin);
            frame_number=bunshin.until+1;som_bunshin_frame(ram);
            assert(!som_bun_alive(&bunshin)&&!bunshin_shunshin);
        }
        som_bun_clear(&bunshin);
        /* Kage Bunshin (chakram): two clones across the facing, on open
         * ground; a monster touching one makes it explode; A swaps. */
        memset(ram+0x10000,0,0x10000);                       /* all tile 0: open */
        ram[0xC0]=0x00;ram[0xC1]=0x02;ram[0xC2]=0x00;ram[0xC3]=0x02;   /* 512x512 */
        hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;  /* facing right */
        hero[0x1E4]=6;hero[0x191]=0;hero[0x01C]=0;hero[0x011]=0;
        for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
        mocked_buttons=r1a;som_input_poll();mocked_buttons=0;som_input_poll();
        assert(!evade_active&&som_bun_alive(&bunshin)==2);
        assert(bunshin.x[0]==200&&bunshin.y[0]==168&&bunshin.x[1]==200&&bunshin.y[1]==232);  /* 32 px above / below */
        hero[2]=202;som_bunshin_frame(ram);                  /* clones copy the step */
        assert(bunshin.x[0]==202&&bunshin.x[1]==202);
        uint8_t *m0=ram+0xE600;
        {
            const SomBunshin keep_bunshin=bunshin;
            const uint8_t keep_owner=bunshin_owner;
            uint8_t saved_m[0x200];memcpy(saved_m,m0,sizeof(saved_m));
            const unsigned row=0x102AD0u+64u*0x0Cu;
            rom[row+8]=0x81;rom[row+15]=3;
            memset(m0,0,0x200);
            m0[0]=1;m0[0x180]=0x57;m0[0x140]=0x44;m0[0x170]=0x0C;
            m0[0x172]=m0[0x173]=m0[0x174]=0xFF;m0[0x186]=9;
            m0[0x176]=3;m0[0x178]=1;m0[0x179]=2;m0[0x17A]=3;
            bunshin_owner=1;
            som_bunshin_absorb_magic(ram);
            assert(som_bun_alive(&bunshin)==2&&m0[0x186]==9);     /* area spell */
            m0[0x176]=1;m0[0x179]=m0[0x17A]=0;
            som_bunshin_absorb_magic(ram);
            assert(som_bun_alive(&bunshin)==2&&m0[0x186]==9);     /* Boy was targeted */
            m0[0x178]=2;
            som_bunshin_absorb_magic(ram);
            assert(som_bun_alive(&bunshin)==1&&m0[0x186]==6);
            assert(m0[0x172]==0&&m0[0x173]==1&&m0[0x174]==2);
            assert(!m0[0x176]&&!m0[0x178]);
            som_bunshin_absorb_magic(ram);
            assert(som_bun_alive(&bunshin)==1&&m0[0x186]==6);     /* one clone per cast */
            bunshin=keep_bunshin;bunshin_owner=keep_owner;
            memcpy(m0,saved_m,sizeof(saved_m));
        }
        m0[0]=1;m0[0x180]=7;m0[0x190]=m0[0x191]=0;m0[0x1F1]=0;
        m0[2]=210;m0[3]=0;m0[4]=230;m0[5]=0;                  /* touches clone 1 */
        som_bunshin_frame(ram);
        assert(!bunshin.alive[1]&&bunshin.alive[0]&&m0[0x1F1]==som_bunshin_damage());
        m0[0]=0;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_UP));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(som_bun_u16(hero+4)==168&&bunshin.y[0]==200);   /* swapped places */
        m0[0]=1;m0[0x1F1]=0;m0[2]=202;m0[3]=0;m0[4]=178;m0[5]=0;   /* at the hero */
        frame_number+=SOM_BUN_SWAP_COOLDOWN+1;bunshin.until=frame_number+100;
        som_bunshin_frame(ram);
        assert(som_bun_u16(hero+4)==200&&!som_bun_alive(&bunshin)&&m0[0x1F1]); /* substitution */
        m0[0]=0;
        /* No clone ends up in a wall. */
        ram[0x1B800+4*9]=0x03;                                 /* tile 9 solid */
        for(unsigned c=0;c<256;c++)ram[0x10000+((160u>>4)*128u)+c]=9;  /* wall above: rows 10-11 */
        hero[2]=200;hero[4]=200;
        assert(som_bun_spawn(&bunshin,ram,200,200,0x02,0,frame_number)==1);
        assert(!bunshin.alive[0]&&bunshin.alive[1]);
        som_bun_clear(&bunshin);memset(ram+0x10000,0,0x10000);ram[0x1B800+4*9]=0;
        hero[0x01C]=0;hero[0x011]=0;som_evade_tick();hero[0x1E4]=1;hero[0x191]=0;
        /* Swordsman R2+A: bash = native run (A), release, native swing (B). */
        disciplines.selected=0;hero[0x1ED]=0;hero[0x060]=0;hero[0x00A]=0;
        hero[0x01A]=hero[0x01B]=0;hero[0x190]=hero[0x191]=0;
        const uint16_t block_r2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R2);
        mocked_buttons=(uint16_t)(block_r2|r1a);som_input_poll();
        assert(bash_stage==SOM_BASH_LUNGE&&!evade_active);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==0);
        hero[0x006]=3;
        for(unsigned f=0;f<SOM_BASH_LUNGE_FRAMES;f++){frame_number++;som_bash_tick();}
        assert(bash_stage==SOM_BASH_RELEASE);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==0);
        hero[0x1ED]=75;frame_number++;som_bash_tick();         /* the run's gauge */
        assert(bash_stage==SOM_BASH_STRIKE&&hero[0x1ED]==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
        frame_number+=SOM_BASH_STRIKE_FRAMES;som_bash_tick();
        assert(bash_stage==SOM_BASH_NONE);
        mocked_buttons=0;som_input_poll();
        hero[0x1ED]=10;mocked_buttons=(uint16_t)(block_r2|r1a);som_input_poll();
        assert(bash_stage==SOM_BASH_NONE);                     /* out of breath */
        mocked_buttons=0;som_input_poll();hero[0x1ED]=0;hero[0x006]=0;
        /* Martial Arts X: the flying kick = the bash's run, then the glove.
         * The game never sees X; with a frame pack the hero is hidden and
         * drawn kicking in a jump arc, landing at the strike. */
        {
            static uint8_t pack[10+44+6+6];
            memset(pack,0,sizeof(pack));
            memcpy(pack,"SOMWORK1",8);pack[8]=1;
            pack[10]=0x90;pack[11]=0xFD;pack[12]=2;pack[13]=2;   /* boy kick, side */
            pack[54]=1;pack[57]=1;pack[58]=1;pack[59]=1;         /* chamber 1 tick */
            pack[60]=4;pack[63]=1;pack[64]=1;pack[65]=1;         /* kick 4 ticks */
            cartwheel_pack=pack;cartwheel_pack_size=sizeof(pack);cartwheel_pack_tried=1;
            const uint16_t xb=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
            disciplines.selected=1;hero[0x180]=0x80;hero[0x010]=0x82;hero[0x0E]=0;hero[0x1E4]=0;
            som_combo_clear();
            mocked_buttons=xb;som_input_poll();
            assert(bash_stage==SOM_BASH_LUNGE&&bash_kick&&hero[0x1E4]==0);
            assert(flykick.active&&flykick.dir==3u&&(hero[0x0E]&0x80u));
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==1);
            mocked_buttons=0;som_input_poll();
            hero[0x006]=3;
            int peak=0;
            for(unsigned f=0;f<SOM_BASH_LUNGE_FRAMES;f++){
                frame_number++;som_bash_tick();som_flykick_frame(ram);
                if(flykick.lift>peak)peak=flykick.lift;
            }
            assert(peak==SOM_FLYKICK_LIFT&&bash_stage==SOM_BASH_RELEASE);
            frame_number++;som_bash_tick();som_flykick_frame(ram);
            assert(bash_stage==SOM_BASH_STRIKE&&!flykick.lift);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
            frame_number+=SOM_BASH_STRIKE_FRAMES;som_bash_tick();som_flykick_frame(ram);
            assert(bash_stage==SOM_BASH_NONE&&!bash_kick&&flykick.active);
            frame_number+=25;som_flykick_frame(ram);
            assert(!flykick.active&&!(hero[0x0E]&0x80u));
            /* With a combo level ready the landing kick is the finisher. */
            hero[0x006]=0;hero[0x1ED]=0;hero[0x19C]=2;hero[0x19B]=0;hero[0x01C]=0;
            som_combo_clear();combo.hits=5;combo.mixes=1;combo.hit_frame=frame_number;
            mocked_buttons=xb;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(bash_stage==SOM_BASH_LUNGE);
            for(unsigned f=0;f<=SOM_BASH_LUNGE_FRAMES;f++){frame_number++;som_bash_tick();som_flykick_frame(ram);}
            /* The monk's glove has every level (not capped at the 2 learned). */
            assert(bash_stage==SOM_BASH_NONE&&special_stage>=2u&&special_weapon==0u&&special_suppress);
            assert(hero[0x19C]==SOM_MONK_GLOVE_LEVELS);
            assert(flykick.active);                           /* drawn through the finisher */
            special_stage=0;special_suppress=0;frame_number+=30;som_flykick_frame(ram);
            assert(!flykick.active&&!(hero[0x0E]&0x80u));
            special_stage=0;special_suppress=0;
            /* The monk's B: a Storm string (4 strikes, the level-1 flurry, a
             * finisher by direction) and Tekken commands, no charge, B
             * hidden from the game; a B during a move queues the next. */
            {
                const uint16_t bb=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
                const uint16_t lt=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT);
                const uint16_t rt=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT);
                const uint16_t up=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_UP);
                for(unsigned m=0;m<9u;m++)ram[0xE600+0x200*m]=0;   /* no monster: far, facing */
                hero[0x1E4]=0;hero[0x19C]=2;hero[0x19B]=0;hero[0x01C]=0;hero[0x1ED]=40;
                hero[0x060]=0;hero[0x061]=0;hero[0x191]=0;hero[0x010]=0x82;   /* faces left */
                monk_glove_raised=-1;monk_chain_active=0;monk_chain_step=0;
                monk_dir_last=monk_dir_prev=0;frame_number+=100;
                #define MONK_TAP(btn) do{mocked_buttons=(btn);som_input_poll();mocked_buttons=0;som_input_poll();}while(0)
                #define MONK_END() do{hero[0x01C]=0;som_monk_chain_frame(ram);frame_number++;}while(0)
                mocked_buttons=bb;som_input_poll();
                assert(hero[0x01C]==0x20u&&hero[0x011]==1u&&hero[0x19B]==0u&&monk_chain_active);
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0); /* hidden */
                assert(hero[0x19C]==SOM_MONK_GLOVE_LEVELS);
                mocked_buttons=0;som_input_poll();
                MONK_TAP(bb);assert(monk_chain_queued&&hero[0x011]==1u);   /* waits its turn */
                MONK_END();assert(hero[0x01C]==0x20u&&hero[0x011]==0u);    /* strike 2 */
                MONK_END();MONK_TAP(bb);assert(hero[0x011]==3u);
                MONK_END();MONK_TAP(bb);assert(hero[0x011]==2u);
                MONK_END();MONK_TAP(bb);
                assert(hero[0x01C]==0x80u&&hero[0x011]==4u+2u&&hero[0x19B]==1u);  /* flurry */
                MONK_END();MONK_TAP(bb);
                assert(hero[0x011]==4u*3u+2u&&hero[0x19B]==2u);             /* finisher: none = 3 */
                MONK_END();assert(!hero[0x19B]);
                /* The finisher by direction: toward (left, the facing) 4,
                 * away (right) 2, sideways (up) 5. */
                const uint16_t held[3]={lt,rt,up};const unsigned want[3]={4u,2u,5u};
                for(unsigned k=0;k<3u;k++){
                    for(unsigned n=0;n<5u;n++){MONK_TAP(bb);MONK_END();}
                    frame_number+=40;                                   /* no command */
                    hero[0x010]=0x82;MONK_TAP((uint16_t)(bb|held[k]));
                    assert(hero[0x011]==4u*want[k]+2u);
                    MONK_END();
                }
                /* Commands: toward, toward + B = 7; away, toward + B = 6;
                 * they cancel a strike in progress. */
                hero[0x010]=0x82;MONK_TAP(bb);assert(hero[0x01C]==0x20u);    /* a strike */
                MONK_TAP(lt);frame_number+=5;MONK_TAP(lt);frame_number+=5;MONK_TAP(bb);
                assert(hero[0x01C]==0x80u&&hero[0x011]==4u*7u+2u&&hero[0x19B]==3u);
                MONK_END();hero[0x010]=0x82;
                MONK_TAP(rt);hero[0x010]=0x82;frame_number+=5;MONK_TAP(lt);frame_number+=5;MONK_TAP(bb);
                assert(hero[0x011]==4u*6u+2u);
                MONK_END();hero[0x010]=0x82;
                MONK_TAP(lt);frame_number+=SOM_MONK_CMD_GAP+5u;MONK_TAP(lt);frame_number+=2;MONK_TAP(bb);
                assert(hero[0x01C]==0x20u);                                 /* too slow: a strike */
                MONK_END();frame_number+=SOM_MONK_CHAIN_RESET+1u;
                MONK_TAP(bb);assert(hero[0x011]==1u);                       /* a pause starts over */
                MONK_END();hero[0x19B]=0;
                /* The samurai: the same string on the katana (sword, type 1);
                 * with the bow in hand B takes the katana back and strings. */
                {
                    const uint8_t keep_cls=disciplines.selected;
                    disciplines.selected=SOM_DISC_BUSHIDO;frame_number+=200;
                    hero[0x1E4]=1;hero[0x010]=0x82;monk_chain_step=0;monk_dir_last=monk_dir_prev=0;
                    MONK_TAP(bb);assert(hero[0x01C]==0x20u&&hero[0x011]==1u);
                    for(unsigned n=0;n<3u;n++){MONK_END();MONK_TAP(bb);}
                    MONK_END();MONK_TAP(bb);
                    assert(hero[0x01C]==0x80u&&hero[0x011]==6u&&hero[0x19B]==1u);   /* level 1 */
                    MONK_END();MONK_TAP(lt);frame_number+=5;MONK_TAP(lt);frame_number+=5;MONK_TAP(bb);
                    assert(hero[0x011]==4u*7u+2u);                                 /* Tsubame Gaeshi */
                    MONK_END();frame_number+=SOM_MONK_CHAIN_RESET+1u;
                    hero[0x1E4]=5;monk_dir_last=monk_dir_prev=0;                   /* the bow (Y) */
                    MONK_TAP(bb);assert(hero[0x1E4]==1u&&monk_chain_active&&hero[0x011]==1u); /* B: katana, string */
                    MONK_END();
                    hero[0x1E4]=0;disciplines.selected=keep_cls;hero[0x19B]=0;
                }
                /* The archer: shots, a double shot (arrow stage held until
                 * the launch, then the damage stage), a javelin finisher,
                 * and away, away + B: a hop back, then three arrows. */
                {
                    const uint8_t keep_cls=disciplines.selected;
                    static uint8_t keep_hero[0x200];memcpy(keep_hero,hero,sizeof(keep_hero));
                    const int keep_raised=monk_glove_raised;
                    disciplines.selected=SOM_DISC_ARCHERY;frame_number+=200;
                    const uint8_t keep_jav=ram[0xCC5B],keep_bow=ram[0xCC59];
                    ram[0xCC5B]=0;ram[0xCC59]=0;                       /* bow and javelin owned */
                    hero[0x1E4]=5;hero[0x010]=0x82;hero[0x061]=0;monk_chain_step=0;
                    monk_dir_last=monk_dir_prev=0;archer_hop_move=-1;archer_moving=-1;
                    #define ARC_END() do{hero[0x01C]=0;hero[0x061]=0;hero[0x19B]=0;frame_number+=40;som_monk_chain_frame(ram);}while(0)
                    MONK_TAP(bb);assert(hero[0x01C]==0x20u&&hero[0x011]==0u&&hero[0x19B]==0u);
                    ARC_END();MONK_TAP(bb);ARC_END();MONK_TAP(bb);
                    assert(archer_moving==SOM_ARC_SHOT);
                    ARC_END();MONK_TAP(bb);
                    assert(archer_moving==SOM_ARC_DOUBLE&&hero[0x01C]==0x80u&&hero[0x19B]==3u);
                    frame_number=archer_launch_at;som_monk_chain_frame(ram);assert(hero[0x19B]==3u);
                    hero[0x061]=1;frame_number++;som_monk_chain_frame(ram);
                    assert(hero[0x19B]==1u);                            /* damage after the launch */
                    /* B while the volley flies: queued, fired when it is gone. */
                    hero[0x01C]=0;frame_number++;som_monk_chain_frame(ram);
                    MONK_TAP((uint16_t)(bb|lt));                        /* toward (facing left) */
                    assert(monk_chain_queued||archer_moving==SOM_ARC_LEAP);
                    hero[0x061]=0;hero[0x19B]=0;frame_number+=30;som_monk_chain_frame(ram);  /* volley gone */
                    assert(archer_moving==SOM_ARC_LEAP&&hero[0x1E4]==7u&&hero[0x011]==4u*4u+2u);
                    ARC_END();
                    hero[0x1E4]=5;          /* the bow back (the mock has no bow art to swap to) */
                    MONK_TAP(rt);hero[0x010]=0x82;frame_number+=5;MONK_TAP(rt);hero[0x010]=0x82;
                    frame_number+=5;MONK_TAP(bb);
                    assert(archer_hop_move==SOM_ARC_FALCON||archer_moving==SOM_ARC_FALCON);
                    evade_active=0;som_monk_chain_frame(ram);
                    assert(archer_moving==SOM_ARC_FALCON&&hero[0x19B]==6u&&hero[0x1E4]==5u&&hero[0x010]==0x82u);
                    ARC_END();archer_hop_move=-1;
                    #undef ARC_END
                    som_evade_tick();hero[0x1E4]=0;disciplines.selected=keep_cls;hero[0x19B]=0;
                    ram[0xCC5B]=keep_jav;ram[0xCC59]=keep_bow;
                    memcpy(hero,keep_hero,sizeof(keep_hero));monk_glove_raised=keep_raised;
                }
                /* The mixed strings: every class's tables name weapons the
                 * class is supplied, and Swordsmanship plays its string. */
                for(unsigned i=0;i<sizeof(som_blade_strings)/sizeof(som_blade_strings[0]);i++){
                    const SomBladeMove *m=(const SomBladeMove*)som_blade_strings[i].step;
                    assert(som_blade_strings[i].step[0].weapon<8u);
                    for(unsigned k=0;k<SOM_BLADE_STEPS+4u+2u;k++){
                        m=k<SOM_BLADE_STEPS?&som_blade_strings[i].step[k]:
                          k<SOM_BLADE_STEPS+4u?&som_blade_strings[i].finish[k-SOM_BLADE_STEPS]:
                          k==SOM_BLADE_STEPS+4u?&som_blade_strings[i].toward:&som_blade_strings[i].away;
                        assert(m->weapon<8u&&m->level<=8u&&(!m->level||m->name));
                        assert(!(m->launch)||m->free>m->launch);
                    }
                }
                {
                    const uint8_t keep_cls=disciplines.selected,keep_axe=ram[0xCC56];
                    static uint8_t keep_hero[0x200];memcpy(keep_hero,hero,sizeof(keep_hero));
                    disciplines.selected=SOM_DISC_SWORD;frame_number+=200;
                    const uint8_t keep_style=styles.selected[SOM_DISC_NINJUTSU];
                    styles.selected[SOM_DISC_NINJUTSU]=2u;assert(som_string_off(SOM_DISC_NINJUTSU));
                    styles.selected[SOM_DISC_NINJUTSU]=1u;assert(!som_string_off(SOM_DISC_NINJUTSU));
                    styles.selected[SOM_DISC_NINJUTSU]=keep_style;
                    ram[0xCC56]=0xFF;                       /* no axe: the sword stands in */
                    som_put_word(hero+0x182,50);hero[0x1E4]=1;hero[0x010]=0x82;hero[0x01C]=0;hero[0x061]=0;
                    monk_chain_step=0;monk_chain_active=0;monk_dir_last=monk_dir_prev=0;
                    const uint8_t v[4]={1,0,3,2};
                    for(unsigned n=0;n<4u;n++){
                        MONK_TAP(bb);assert(hero[0x01C]==0x20u&&hero[0x011]==v[n]);MONK_END();
                    }
                    MONK_TAP(bb);assert(hero[0x01C]==0x80u&&hero[0x011]==6u&&hero[0x1E4]==1u);
                    MONK_END();frame_number+=40;
                    MONK_TAP((uint16_t)(bb|lt));assert(hero[0x011]==4u*4u+2u);      /* Thrust */
                    MONK_TAP(bb);assert(monk_chain_queued);
                    MONK_END();assert(hero[0x01C]==0x20u);                        /* queued strike */
                    MONK_TAP(lt);frame_number+=5;MONK_TAP(lt);frame_number+=5;MONK_TAP(bb);
                    assert(hero[0x011]==4u*7u+2u&&hero[0x19B]==3u);               /* cancels it */
                    MONK_END();assert(!hero[0x19B]&&!monk_chain_active);
                    memcpy(hero,keep_hero,sizeof(keep_hero));
                    disciplines.selected=keep_cls;ram[0xCC56]=keep_axe;monk_chain_step=0;blade_move=NULL;
                }
                #undef MONK_TAP
                #undef MONK_END
                ram[0xD0]=0x83;som_monk_glove(ram);                        /* the inn's save */
                assert(hero[0x19C]==2&&monk_glove_raised<0);
                ram[0xD0]=0;som_monk_glove(ram);assert(hero[0x19C]==SOM_MONK_GLOVE_LEVELS);
                disciplines.selected=0;som_monk_glove(ram);
                assert(hero[0x19C]==2&&monk_glove_raised<0);
                disciplines.selected=1;taijutsu_step=0;
            }

            /* The AI's "flykick": the same move; refused for other classes. */
            disciplines.selected=0;assert(!som_handle_command("flykick"));
            disciplines.selected=1;hero[0x1ED]=0;hero[0x01C]=0;
            assert(som_handle_command("flykick")&&bash_stage==SOM_BASH_LUNGE&&bash_kick);
            for(unsigned f=0;f<=SOM_BASH_LUNGE_FRAMES+SOM_BASH_STRIKE_FRAMES+1;f++){
                frame_number++;som_bash_tick();som_flykick_frame(ram);
            }
            assert(!bash_stage&&!flykick_from_ai);
            frame_number+=30;som_flykick_frame(ram);
            cartwheel_pack=NULL;cartwheel_pack_size=0;hero[0x19C]=1;hero[0x010]=0x02;
            disciplines.selected=0;hero[0x1E4]=1;
        }
        /* Swordsman forward + A: the game's cartwheel drawn facing forward,
         * the hero hidden and carried 64 px ahead (not turned round). */
        {
            static uint8_t pack[10+44+6];
            memset(pack,0,sizeof(pack));
            memcpy(pack,"SOMWORK1",8);pack[8]=1;
            pack[10]=SOM_FORWARD_CART_KIND;pack[11]=0xF9;pack[12]=2;pack[13]=1;
            pack[54]=5;pack[57]=1;pack[58]=1;pack[59]=1;
            cartwheel_pack=pack;cartwheel_pack_size=sizeof(pack);cartwheel_pack_tried=1;
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            disciplines.selected=SOM_DISC_SWORD;hero[0x180]=0x80;
            for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;hero[0x0E]=0;hero[0x1E5]=0xFF;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
            som_input_poll();mocked_buttons=0;som_input_poll();
            assert(evade_active&&!cartwheel_native&&ukemi_draw.active&&ukemi_draw.dir==2u);  /* right: as drawn */
            assert(hop_dx==2&&hero[0x010]==0x02&&(hero[0x0E]&0x80u));
            for(unsigned f=0;f<40u;f++){som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;}
            assert(!evade_active&&!ukemi_draw.active&&!(hero[0x0E]&0x80u));
            assert(som_bun_u16(hero+2)==200+26&&hero[0x010]==0x02);
            /* Facing up, right + A: drawn facing where he goes (the game's
             * cartwheel would turn him round); the facing is kept. Facing
             * right, left + A: the same, only A alone jumps backward. */
            hero[2]=200;hero[3]=0;hero[0x010]=0x00;hero[0x0E]=0;hero[0x1E5]=0xFF;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
            som_input_poll();mocked_buttons=0;som_input_poll();
            assert(evade_active&&!cartwheel_native&&ukemi_draw.active&&ukemi_draw.dir==2u&&hop_dx==2);
            for(unsigned f=0;f<40u;f++){som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;}
            assert(!evade_active&&som_bun_u16(hero+2)==200+26&&hero[0x010]==0x00);
            hero[2]=200;hero[3]=0;hero[0x010]=0x02;hero[0x0E]=0;hero[0x1E5]=0xFF;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT));
            som_input_poll();mocked_buttons=0;som_input_poll();
            assert(evade_active&&ukemi_draw.active&&ukemi_draw.dir==3u&&hop_dx==-2);
            for(unsigned f=0;f<60u;f++){som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;}
            hero[0x1E5]=0xFF;hero[0x010]=0x02;
            /* Up/down + A: drawn with the game's down-cartwheel look, carried
             * the way pressed (26 px), on any map; facing kept. */
            for(int sgn=-1;sgn<=1;sgn+=2){
                hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x0E]=0;hero[0x1E5]=0xFF;
                mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|
                    (1u<<(sgn>0?RETRO_DEVICE_ID_JOYPAD_DOWN:RETRO_DEVICE_ID_JOYPAD_UP)));
                som_input_poll();mocked_buttons=0;som_input_poll();
                assert(evade_active&&!cartwheel_native&&ukemi_draw.active&&
                       ukemi_draw.dir==2u&&hop_dy==2*sgn&&hero[0x1E5]==0xFF);
                for(unsigned f=0;f<40u;f++){som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;}
                assert(!evade_active&&som_bun_u16(hero+4)==(unsigned)(200+26*sgn)&&hero[0x010]==0x02);
            }
            hero[4]=200;hero[5]=0;
            /* Every cartwheel: the rogue, and the monk's Ukemi to the side
             * (the monk tumbles now, Ukemi like the ninja and samurai). */
            assert(!strcmp(som_disc_dodge(1,0).name,"Ukemi"));
            static const uint8_t classes[2]={9,SOM_DISC_MARTIAL_ARTS};
            for(unsigned c=0;c<2u;c++){
                disciplines.selected=classes[c];
                hero[2]=200;hero[3]=0;hero[0x010]=0x02;hero[0x0E]=0;hero[0x1E5]=0xFF;hero[0x1E4]=0;
                hero[0x01C]=0;hero[0x060]=0;
                mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
                som_input_poll();mocked_buttons=0;som_input_poll();
                assert(evade_active&&!cartwheel_native&&ukemi_draw.active&&hop_dx>0);
                assert(ukemi_draw.dir==2u);              /* right: as drawn, every class */
                for(unsigned f=0;f<40u;f++){som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;}
                assert(!evade_active&&som_bun_u16(hero+2)>200&&hero[0x010]==0x02);
            }
            disciplines.selected=SOM_DISC_SWORD;hero[0x1E4]=1;
            hero[2]=200;hero[3]=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            cartwheel_pack=NULL;cartwheel_pack_size=0;
        }
        /* The AI's attack runs the ninja's chain only in a fight: cutting
         * grass (no monster within 80 px) the sword stays in hand. */
        {
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_NINJUTSU;taijutsu_step=0;taijutsu_last=frame_number;
            for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
            hero[0x1E4]=1;hero[0x01C]=0;hero[0x060]=0;hero[0x1ED]=0;
            som_start_action(SOM_ACT_ATTACK);
            assert(hero[0x1E4]==1&&taijutsu_step==0u);                  /* sword kept */
            active_action=SOM_ACT_NONE;armed=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x1F1]=0;
            foe[2]=(uint8_t)(hero[2]+30);foe[3]=hero[3];foe[4]=hero[4];foe[5]=hero[5];
            const uint8_t keep_glove=ram[0xCC54];ram[0xCC54]=0;              /* glove owned */
            som_start_action(SOM_ACT_ATTACK);
            assert(taijutsu_step==1u);                                  /* chain: a punch */
            active_action=SOM_ACT_NONE;armed=0;foe[0]=0;taijutsu_step=0;hero[0x1E4]=1;
            ram[0xCC54]=keep_glove;
            disciplines.selected=keep;
        }
        /* Artificer portals: hold X places A, again B; stepping into A
         * (after leaving both) travels to B; a tap stays the bomb. */
        {
            const uint16_t xb=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_ARTIFICER;portals.count=0;
            hero[2]=100;hero[3]=0;hero[4]=100;hero[5]=0;hero[0x01C]=0;hero[0x060]=0;
            mocked_buttons=xb;som_input_poll();
            for(unsigned f=0;f<SOM_PORTAL_HOLD_FRAMES;f++){frame_number++;som_input_poll();}
            assert(portals.count==1&&portals.x[0]==100);
            mocked_buttons=0;som_input_poll();                        /* release: no bomb */
            hero[2]=180;
            mocked_buttons=xb;som_input_poll();
            for(unsigned f=0;f<SOM_PORTAL_HOLD_FRAMES;f++){frame_number++;som_input_poll();}
            mocked_buttons=0;som_input_poll();
            assert(portals.count==2&&portals.x[1]==180);
            som_portal_frame(ram);assert(!blink_active);              /* standing in B: not armed */
            hero[2]=140;som_portal_frame(ram);assert(portals.armed);
            hero[2]=104;som_portal_frame(ram);
            assert(blink_active&&blink_tx==180&&blink_ty==100&&!portals.armed);
            for(unsigned f=0;f<60;f++){som_blink_frame(ram);frame_number++;}
            assert(!blink_active&&som_bun_u16(hero+2)==180);
            portals.count=0;hero[2]=200;hero[4]=200;hero[0x0E]=0;
            disciplines.selected=keep;
        }
        /* BG3 stays at the game's own scroll: saves made with the old 10/20/26 px
         * HUD lift get it back to 0 and the portrait back down; field only. */
        {
            uint8_t keep_oam[0x220];memcpy(keep_oam,ram+0x0800,sizeof(keep_oam));
            const uint8_t keep_d0=ram[0xD0],keep_7d=ram[0x7D],keep_e000=ram[0xE000];
            ram[0xE000]=1;ram[0xD0]=0;ram[0x7D]=0;ram[0x41]=0;
            uint8_t *e=ram+0x0800,*m=ram+0x0804;
            for(unsigned old=10;old<=26;old+=old==10?10u:6u){
                ram[0x40]=(uint8_t)old;
                e[0]=88;e[1]=(uint8_t)(199-old);e[2]=0xE8;e[3]=0x33;   /* portrait, tile 0x1E8 */
                m[0]=60;m[1]=(uint8_t)(199-old);m[2]=0x40;m[3]=0x30;   /* a monster as high */
                som_hud_lift(ram,0);
                assert(ram[0x40]==0&&e[1]==199&&m[1]==199-old);
                som_hud_lift(ram,0);assert(e[1]==199);                  /* not twice */
            }
            ram[0x40]=10;e[1]=189;som_hud_lift(ram,1);                  /* a menu */
            assert(ram[0x40]==10&&e[1]==189);
            ram[0xD0]=0x83;som_hud_lift(ram,0);assert(ram[0x40]==10);   /* a dialog */
            ram[0xD0]=0;ram[0x40]=0x30;som_hud_lift(ram,0);             /* a scroll of the game's own */
            assert(ram[0x40]==0x30);
            ram[0x40]=0;ram[0xD0]=keep_d0;ram[0x7D]=keep_7d;ram[0xE000]=keep_e000;
            memcpy(ram+0x0800,keep_oam,sizeof(keep_oam));
        }
        /* AI commands: class choice and casts within the class. */
        assert(som_handle_command("class_3")&&disciplines.selected==3);
        assert(!som_handle_command("class_16")&&!som_handle_command("class_x"));
        assert(som_handle_command("class_0")&&disciplines.selected==0);

        /* X/Triangle: Fire Ball at the nearest live, on-screen monster. */
        hero[0x020]=100;hero[0x022]=100;
        uint8_t *far=ram+0xE600,*near=ram+0xE800,*off=ram+0xEA00;
        far[0]=1;far[0x20]=200;far[0x22]=100;
        near[0]=1;near[0x20]=110;near[0x22]=90;
        off[0]=1;off[0x20]=0xF8;off[0x22]=100;   /* off screen */
        hero[0x1C5]=0x30;                        /* Salamando level 3 */
        hero[0x186]=1;
        disciplines.selected=2;                  /* Ninjutsu: Fire Ball first */
        assert(som_current_spell()==SOM_SPELL_FIRE_BALL);
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_input_poll();                        /* press: nothing cast yet */
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
        mocked_buttons=0;som_input_poll();       /* release: not enough MP */
        for(unsigned f=0;f<=SOM_X_A_WINDOW;f++){frame_number++;som_input_poll();}  /* the A wait */
        assert(hero[0x01C]==0&&hero[0x060]==0&&hero[0x170]==0);
        hero[0x186]=5;near[0x191]=0x80;          /* nearest one is dead */
        hero[0x179]=4;hero[0x17A]=5;             /* stale list from an old cast */
        memcpy(hero_before,hero,sizeof(hero_before));
        uint8_t buffer_before[0x800];memcpy(buffer_before,ram+0x2000,sizeof(buffer_before));
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
        mocked_buttons=0;som_input_poll();       /* tap released: the jutsu waits for A */
        assert(x_cast_at&&hero[0x170]!=0x0C);
        for(unsigned f=0;f<=SOM_X_A_WINDOW;f++){frame_number++;som_input_poll();}  /* no A: cast */
        assert(hero[0x170]==0x0C&&hero[0x177]==2&&hero[0x176]==1);
        assert(hero[0x178]==4&&hero[0x179]==0&&hero[0x17A]==0);     /* slot 3 */
        assert(hero[0x172]==0xFF&&hero[0x173]==0xFF&&hero[0x174]==0xFF);
        assert(hero[0x171]==3&&hero[0x111]==3&&hero[0x108]==0);
        assert(hero[0x01C]==0x40&&hero[0x011]==0x94&&hero[0x060]==0x20);
        assert(hero[0x04F]==0xCE&&hero[0x030]==0xFF);
        assert(hero[0x186]==5);                  /* the game takes MP on the hit */
        EXPECT_TILES(0x14BA00);
        assert(memcmp(ram+0x2800,expect,sizeof(expect))==0);          /* spark */
        assert(memcmp(ram+0x2000,buffer_before,sizeof(buffer_before))==0);
        /* Mid-cast: no second cast, no weapon switch. */
        hero[0x170]=0;
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_input_poll();mocked_buttons=0;som_input_poll();
        assert(hero[0x170]==0);
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        som_input_poll();
        assert(hero[0x1E4]==1&&y_weapon_blocked);
        mocked_buttons=0;som_input_poll();
        hero[0x01C]=0;hero[0x060]=0;
        /* No live target on screen: nothing is written. */
        far[0x191]=0x80;
        assert(!som_cast_spell(ram,SOM_SPELL_FIRE_BALL)&&hero[0x170]==0);
        far[0x191]=0;
        /* Ally spells target the boy himself (hero slot 0 -> 1). */
        hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
        assert(som_cast_spell(ram,0x07)&&hero[0x170]==0x07&&hero[0x178]==1);
        assert(hero[0x177]==1&&hero[0x01C]==0x40);
        hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
        {   /* The girl and the sprite cast the same way from their own
             * objects: self target who+1, the sprite's palette index 0xCF,
             * the spark in her own effect buffer; the boy is left alone. */
            uint8_t *girl=ram+0xE200,*spr=ram+0xE400;
            const uint8_t keep_d9=ram[0xD9],keep_da=ram[0xDA],keep_db=ram[0xDB];
            uint8_t keep_girl[0x200],keep_spr[0x200],keep_buf[0x2000];
            memcpy(keep_girl,girl,0x200);memcpy(keep_spr,spr,0x200);
            memcpy(keep_buf,ram+0x3000,sizeof(keep_buf));
            memset(girl,0,0x200);memset(spr,0,0x200);
            girl[0]=spr[0]=1;girl[0x182]=spr[0x182]=40;girl[0x184]=spr[0x184]=50;
            girl[0x186]=spr[0x186]=9;
            spr[0x20]=hero[0x20];spr[0x21]=hero[0x21];spr[0x22]=hero[0x22];spr[0x23]=hero[0x23];
            uint8_t boy_before[0x200];memcpy(boy_before,hero,0x200);
            assert(som_cast_spell_by(ram,1u,0x07)&&girl[0x170]==0x07&&girl[0x178]==2);
            assert(girl[0x01C]==0x40&&girl[0x04F]==0xCE&&!memcmp(hero,boy_before,0x200));
            assert(!som_cast_spell_by(ram,1u,0x07));                  /* busy now */
            girl[0x1C]=girl[0x60]=0;
            const unsigned revive_row=0x102AD0u+0x21u*64u;
            rom[revive_row+8]=1;rom[revive_row+15]=3;
            assert(!som_cast_spell_by(ram,1u,0x21u));                /* nobody fallen */
            spr[0x182]=0;
            const SomAllyTech keep_tech=ally_tech[0];
            const uint8_t keep_cleric=disciplines.party[0];
            disciplines.party[0]=SOM_DISC_CLERIC;
            som_ally_tech_one(ram,1u);
            assert(girl[0x170]==0x21u&&girl[0x178]==3u&&girl[0x60]==0x20);
            disciplines.party[0]=keep_cleric;ally_tech[0]=keep_tech;
            spr[0x191]=0x80;
            girl[0x186]=6;
            som_revival_frame(ram);
            assert(spr[0x182]==0);                   /* still in the cast pose */
            girl[0x60]=0;
            som_revival_frame(ram);
            assert(spr[0x182]==1&&spr[0x191]==0&&spr[0x1F3]==12);
            girl[0x1C]=girl[0x60]=0;
            spr[0x182]=0;
            disciplines.party[0]=SOM_DISC_BUSHIDO;
            assert(som_try_revivifier(ram,1u,1)&&girl[0x178]==3u);
            assert(disciplines.party[0]==SOM_DISC_BUSHIDO);
            assert(!som_try_revivifier(ram,0u,1));  /* already targeted */
            memset(revivals,0,sizeof(revivals));
            disciplines.party[0]=keep_cleric;
            spr[0x182]=40;
            assert(som_cast_spell_by(ram,2u,SOM_SPELL_FIRE_BALL)&&spr[0x170]==0x0C);
            assert(spr[0x04F]==0xCF&&spr[0x178]==4&&!memcmp(hero,boy_before,0x200));
            EXPECT_TILES(0x14BA00);
            assert(memcmp(ram+0x4800,expect,sizeof(expect))==0);          /* her spark */
            /* B while driving a Mage girl: no spell, the game's punch (the
             * mage bolt rides on it); Y is her Y spell, cast by her, quick. */
            memset(girl+0x170,0,0x10);girl[0x01C]=girl[0x060]=0;girl[0x186]=9;
            const uint8_t keep_party=disciplines.party[0];
            disciplines.party[0]=3;ram[0xD9]=2;ram[0xDA]=1;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
            assert(girl[0x170]==0&&!b_weapon_blocked&&hero[0x170]==0);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
            mocked_buttons=0;som_input_poll();
            rom[0x102AD0+0x06*64+8]=0x81;rom[0x102AD0+0x06*64+15]=2;     /* Freeze */
            /* Y: her second spell, the game's own full cast (2026-11-22:
             * original effects and values; no quick skip). */
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
            assert(girl[0x170]==som_caster_spell(3,1)&&y_weapon_blocked&&hero[0x170]==0);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
            assert(!quick_cast_until[1]);
            mocked_buttons=0;som_input_poll();
            /* Select can move pad 1 before the proxy move owner changes. */
            const uint8_t keep_boy_class=disciplines.selected;
            disciplines.selected=3;
            som_actor=0;blink_active=1;
            girl[0x170]=girl[0x1C]=girl[0x60]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
            assert(som_actor==0u&&y_weapon_blocked);
            assert(girl[0x170]==som_caster_spell(3u,1u)&&girl[0x60]==0x20);
            assert(!memcmp(hero,boy_before,sizeof(boy_before)));
            blink_active=0;disciplines.selected=keep_boy_class;
            mocked_buttons=0;som_input_poll();
            /* X follows pad 1 even while the Boy is casting. */
            girl[0x1C]=girl[0x60]=0;
            hero[0x1C]=0x40;hero[0x60]=0x20;
            const unsigned girl_x=som_current_spell();
            rom[0x102AD0+girl_x*64+8]=0x81;
            rom[0x102AD0+girl_x*64+15]=2;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
            som_input_poll();
            assert(x_spell_blocked&&som_actor==1u);
            mocked_buttons=0;som_input_poll();
            assert(girl[0x170]==girl_x&&girl[0x60]==0x20&&hero[0x170]==0);

            /* A pending tap stays with its actor across Select. A fresh X
             * charges the Sprite's equipped sword while both allies cast. */
            girl[0x1C]=girl[0x60]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);som_input_poll();
            girl[0x1C]=0x40;girl[0x60]=0x20;
            const uint8_t keep_sprite_class=disciplines.party[1];
            disciplines.party[1]=SOM_DISC_BUSHIDO;
            ram[0xD9]=4;ram[0xDA]=2;ram[0xDB]=1;
            spr[0x1C]=spr[0x60]=0;spr[0x1E4]=1;spr[0x19C]=4;
            iai_ready=0;
            mocked_buttons=0;som_input_poll();
            assert(som_actor==2u&&!special_stage);
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);som_input_poll();
            assert(x_spell_blocked);
            mocked_buttons=0;som_input_poll();
            assert(special_stage==SOM_IAI_LEVEL&&som_actor==2u);
            assert(hero[0x1C]==0x40&&girl[0x1C]==0x40);
            special_stage=special_suppress=0;iai_ready=0;

            disciplines.party[1]=3;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);som_input_poll();
            assert(y_weapon_blocked&&spr[0x170]==som_caster_spell(3u,1u)&&spr[0x60]==0x20);
            mocked_buttons=0;som_input_poll();
            spr[0x170]=spr[0x1C]=spr[0x60]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);som_input_poll();
            assert(x_spell_blocked);
            mocked_buttons=0;som_input_poll();
            assert(spr[0x170]==som_current_spell()&&spr[0x60]==0x20);
            assert(hero[0x170]==0&&girl[0x170]==girl_x);
            hero[0x1C]=hero[0x60]=0;
            disciplines.party[1]=keep_sprite_class;
            disciplines.party[0]=keep_party;ram[0xD9]=keep_d9;ram[0xDA]=keep_da;ram[0xDB]=keep_db;
            som_actor_update();
            memcpy(girl,keep_girl,0x200);memcpy(spr,keep_spr,0x200);
            memcpy(ram+0x3000,keep_buf,sizeof(keep_buf));
        }
        {   /* Quick cast: the pose jumps to its closing "83 FD", the spirit to
             * its FF (bytes as in the ROM: pose D1:5D4E, Fire Ball's spirit
             * D1:85E0); a pointer elsewhere is never moved. */
            static const uint8_t pose[]={0x8A,0xA7,0x1C,0xB5,0x1A,0xB5,0xF9,0x2D,0x00,
                0x18,0x7E,0x18,0x85,0x83,0xFD,0x1B,0x2F,0x1B,0x2F,0x1A,0x26,0xFF};
            static const uint8_t spirit[]={0xF9,0x2E,0x00,0,0,0,1,0,2,0,3,0,4,0,5,0,6,0,7,
                0,8,0,9,0,10,0,11,0xFF};
            memcpy(rom+0x115D4E,pose,sizeof(pose));memcpy(rom+0x1185E0,spirit,sizeof(spirit));
            quick_trace_file=NULL;                              /* no trace file in tests */
            uint8_t keep_hero[0x200];memcpy(keep_hero,hero,0x200);
            hero[0x1C]=0x40;hero[0x60]=0x20;hero[0x11]=0x94;hero[0x12]=3;
            hero[0x16]=0x4F;hero[0x17]=0x5D;                    /* after 8A */
            quick_cast_until[0]=frame_number+10u;
            som_quick_cast_frame(ram);
            assert(som_word(hero+0x16)==0x5D5Bu&&hero[0x12]==0);  /* at the 83 */
            som_quick_cast_frame(ram);assert(som_word(hero+0x16)==0x5D5Bu);
            hero[0x60]=0x80;hero[0xFC]=0xE0;hero[0xFD]=0x85;hero[0x16]=0xE3;hero[0x17]=0x85;
            som_quick_cast_frame(ram);
            assert(som_word(hero+0x16)==0x85E0+sizeof(spirit)-1u&&quick_cast_until[0]);
            /* The pose waiting on FD while +0xFC runs the spirit: +0xFC moves. */
            hero[0x16]=0x5C;hero[0x17]=0x5D;hero[0xFC]=0xE5;hero[0xFD]=0x85;
            som_quick_cast_frame(ram);
            assert(som_word(hero+0xFC)==0x85E0+sizeof(spirit)-1u&&som_word(hero+0x16)==0x5D5C);
            som_quick_cast_frame(ram);                          /* at the FF: stays */
            assert(som_word(hero+0xFC)==0x85E0+sizeof(spirit)-1u);
            hero[0x60]=0;som_quick_cast_frame(ram);assert(!quick_cast_until[0]);   /* over */
            /* A stale pointer (no 83 before an FD): left alone. */
            rom[0x115D5B]=0x18;
            hero[0x60]=0x20;hero[0x16]=0x4F;hero[0x17]=0x5D;quick_cast_until[0]=frame_number+10u;
            som_quick_cast_frame(ram);assert(som_word(hero+0x16)==0x5D4F);
            quick_cast_until[0]=0;memcpy(hero,keep_hero,0x200);
        }
        rom[0x102AD0+0x08*64+8]=0x40;           /* undecoded target kind */
        assert(!som_cast_spell(ram,0x08)&&hero[0x170]==0);
        /* A heal is cast on the most hurt hero; 0x41 (may take all) is
         * cast on one, like the ring's default. */
        {
            uint8_t *g=ram+0xE200;uint8_t keep_g[0x200];memcpy(keep_g,g,0x200);
            const uint8_t keep_s0=ram[0xE400];ram[0xE400]=0;
            const uint8_t keep_mp=hero[0x186];hero[0x186]=20;
            g[0]=1;som_put_word(g+0x182,10);som_put_word(g+0x184,100);
            rom[0x102AD0+0x0B*64+8]=0x01;rom[0x102AD0+0x0B*64+15]=3;     /* Cure Water */
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
            assert(som_cast_spell(ram,0x0B)&&hero[0x170]==0x0B&&hero[0x178]==2);   /* the girl */
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
            rom[0x102AD0+0x05*64+8]=0x41;rom[0x102AD0+0x05*64+15]=2;     /* Defender */
            assert(som_cast_spell(ram,0x05)&&hero[0x178]==1);              /* himself */
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;hero[0x186]=keep_mp;
            memcpy(g,keep_g,0x200);ram[0xE400]=keep_s0;
        }
        /* Casters trade weapons for spells: Mage B = Fire Ball, Y = Freeze,
         * X starts at the third spell; B without the MP is nothing (the glove
         * is only for looks: casters never punch). */
        {
            const uint8_t keep=disciplines.selected;
            disciplines.selected=3;
            rom[0x102AD0+0x06*64+8]=0x81;rom[0x102AD0+0x06*64+15]=2;     /* Freeze */
            hero[0x186]=5;hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
            som_input_poll();
            assert(hero[0x170]==0);                            /* B: no spell, */
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1); /* the punch */
            mocked_buttons=0;som_input_poll();
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
            som_input_poll();
            assert(hero[0x170]==0x06);                               /* Y: Freeze, cast */
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
            mocked_buttons=0;som_input_poll();
            assert(som_current_spell()==som_class_spell(3,2));       /* X: the rest */
            /* Spell sets (R1): set II puts spells 3 and 4 on B and Y, X goes on
             * after them; the default style is set I. */
            assert(som_caster_set(3)==0u);
            styles.selected[3]=2;
            assert(som_caster_spell(3,0)==som_class_spell(3,2)&&som_caster_spell(3,1)==som_class_spell(3,3));
            assert(som_current_spell()==som_class_spell(3,4));
            styles.selected[3]=SOM_STYLE_DEFAULT;
            /* Each class names its spells its own way. */
            assert(!strcmp(som_class_spell_label(2,0x0C),"Katon"));
            assert(!strcmp(som_class_spell_label(4,0x28),"Holy Light"));
            assert(!strcmp(som_class_spell_label(3,0x0C),"Fire Ball"));      /* the mage's own */
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;hero[0x186]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
            som_input_poll();
            assert(hero[0x170]==0);                                  /* no MP: */
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1); /* B still bolts */
            mocked_buttons=0;som_input_poll();
            /* Mage bolt: a plain swing throws a ball along the facing; the
             * kick becomes the jab; it hits the monster in line (pending
             * damage) and bursts; a held charge is dropped (no kicks). */
            {
                uint8_t keep_m[0x200];memcpy(keep_m,ram+0xE600,sizeof(keep_m));
                const uint8_t keep_e[4]={ram[0xE4],ram[0xE6],ram[0xD0],ram[0x7D]};
                ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xD0]=0;ram[0x7D]=0;
                memset(mage_bolts,0,sizeof(mage_bolts));
                uint8_t *m=ram+0xE600;memset(m,0,0x200);
                const unsigned hx=som_bun_u16(hero+2),hy=som_bun_u16(hero+4);
                m[0]=1;m[0x180]=0x05;m[2]=(uint8_t)(hx+40u);m[3]=(uint8_t)((hx+40u)>>8);
                m[4]=(uint8_t)hy;m[5]=(uint8_t)(hy>>8);
                hero[0x182]=hero[0x182]?hero[0x182]:30;hero[0x1E4]=0;hero[0x010]=0x02;
                hero[0x181]=4;hero[0x19B]=2;hero[0x01C]=0;hero[0x060]=0;
                som_mage_bolt_frame(ram,0);assert(hero[0x19B]==0);   /* no charge */
                som_mage_bolt_frame(ram,1);assert(!som_bolts_active(mage_bolts));
                hero[0x01C]=0x20;hero[0x011]=2;                      /* the kick */
                som_mage_bolt_frame(ram,1);
                assert(!hero[0x01C]&&hero[0x1ED]>=46u&&hero[0x030]==0xFF);   /* no jab: an orb */
                assert(mage_bolts[0].kind==SOM_BOLT_ORB&&mage_orbs[0]==SOM_ORBS-1u);
                assert(mage_bolts[0].active&&mage_bolts[0].dx==1&&mage_bolts[0].dy==0);
                assert(mage_bolts[0].element==2u);                  /* Fire Ball: Salamando */
                som_mage_bolt_frame(ram,1);assert(!mage_bolts[1].active);   /* one per swing */
                hero[0x19B]=2;som_mage_bolt_frame(ram,0);assert(hero[0x19B]==0);   /* no charge */
                som_mage_bolt_frame(ram,1);assert(!mage_bolts[1].active);
                unsigned long guard=0;
                while(!m[0x1F1]&&guard++<SOM_BOLT_FRAMES){frame_number++;som_mage_bolt_frame(ram,1);}
                assert(m[0x1F1]==som_mage_bolt_damage(hero,3)&&mage_bolts[0].burst);
                frame_number+=SOM_BOLT_BURST_FRAMES;som_mage_bolt_frame(ram,1);
                assert(!som_bolts_active(mage_bolts));
                /* Heals go to the most hurt hero (nobody: no cast). */
                {
                    uint8_t *g=ram+0xE200;uint8_t keep_g[0x200];memcpy(keep_g,g,0x200);
                    const uint8_t keep_s0=ram[0xE400];ram[0xE400]=0;       /* sprite away */
                    g[0]=1;som_put_word(g+0x182,10);som_put_word(g+0x184,100);
                    som_put_word(hero+0x184,som_word(hero+0x182)+1u);
                    assert(som_most_hurt_hero(ram,0x0Au)==1);
                    som_put_word(g+0x182,100);assert(som_most_hurt_hero(ram,0x0Au)==0);  /* the boy */
                    som_put_word(hero+0x184,som_word(hero+0x182));
                    assert(som_most_hurt_hero(ram,0x0Au)==-1);             /* nobody hurt */
                    som_put_word(g+0x182,0);                               /* the girl fell */
                    assert(som_most_hurt_hero(ram,0x21u)==1);              /* Revivifier */
                    assert(som_most_hurt_hero(ram,0x0Au)==-1);             /* Cure Water: no */
                    memcpy(g,keep_g,0x200);ram[0xE400]=keep_s0;
                    assert(som_bolt_heal_spell(som_caster_spell(4,1)));     /* Cure Water */
                }
                /* Hostile Y spells: in front beats a little nearer behind;
                 * never the familiar; the lock wins. */
                {
                    uint8_t keep_t[0xC00];memcpy(keep_t,ram+0xE000,sizeof(keep_t));
                    const int keep_fam=familiar.slot,keep_lock=lock_slot;
                    uint8_t *c=ram+0xE000;
                    som_put_word(c+0x20,100);som_put_word(c+0x22,100);c[0x10]=0x02u;  /* faces right */
                    for(unsigned k=3;k<6u;k++){uint8_t *e=ram+0xE000u+0x200u*k;e[0]=0;e[0x191]=0;}
                    uint8_t *e3=ram+0xE600,*e4=ram+0xE800,*e5=ram+0xEA00;
                    e3[0]=1;som_put_word(e3+0x20,70);som_put_word(e3+0x22,100);     /* 30 px behind */
                    e4[0]=1;som_put_word(e4+0x20,140);som_put_word(e4+0x22,100);    /* 40 px ahead */
                    familiar.slot=-1;lock_slot=-1;
                    assert(som_cast_target(ram,0)==4);
                    c[0x10]=0x82u;assert(som_cast_target(ram,0)==3);             /* faces left */
                    familiar.slot=0;assert(som_cast_target(ram,0)==4);           /* summon is ours */
                    familiar.slot=-1;
                    e5[0]=1;som_put_word(e5+0x20,230);som_put_word(e5+0x22,100);
                    lock_slot=2;assert(som_cast_target(ram,0)==5);               /* locked on */
                    memcpy(ram+0xE000,keep_t,sizeof(keep_t));familiar.slot=keep_fam;lock_slot=keep_lock;
                }
                /* Psionics' ball is Shadow (Dark Force, Shadow Bolt). */
                assert(som_spell_element(som_caster_spell(SOM_DISC_PSIONICS,0))==6u);
                /* An AI companion mage throws at a monster in line out of
                 * reach of her fist: she turns to it and jabs. */
                uint8_t *spr2=ram+0xE400;uint8_t keep_s[0x200];memcpy(keep_s,spr2,0x200);
                const uint8_t keep_party2=disciplines.party[1];disciplines.party[1]=5u;
                memset(spr2,0,0x200);spr2[0]=1;spr2[0x182]=20;spr2[0x184]=20;
                spr2[2]=(uint8_t)hx;spr2[3]=(uint8_t)(hx>>8);
                const unsigned sy=hy+60u;spr2[4]=(uint8_t)sy;spr2[5]=(uint8_t)(sy>>8);
                m[0x1F1]=0;m[4]=(uint8_t)(sy-0u);m[5]=(uint8_t)(sy>>8);   /* beside her, 40 px */
                hero[0x01C]=0;mage_ally_next[2]=0;mage_swing_prev[2]=0;
                som_mage_bolt_frame(ram,1);
                assert(!spr2[0x01C]&&spr2[0x1ED]>=46u&&spr2[0x010]==0x02);
                assert(som_bolts_active(mage_bolts));
                assert(!som_mage_ally_throw(ram,2));                  /* busy, waits */
                /* The class attacks: no jab is left, and each does its own. */
                for(unsigned pick=0;pick<3u;pick++){
                    const unsigned c2=pick==0u?SOM_DISC_PSIONICS:pick==1u?SOM_DISC_CLERIC:7u;
                    memset(mage_bolts,0,sizeof(mage_bolts));memset(judged_until,0,sizeof(judged_until));
                    const uint8_t keep_id=m[0x180];const uint8_t keep_x[4]={m[2],m[3],m[4],m[5]};
                    som_put_word(m+2,hx+20u);som_put_word(m+4,sy);m[0x180]=0x14;m[0x1F1]=0;m[0x190]=m[0x191]=0;m[0x1AA]=m[0x1AB]=0;
                    spr2[0x1C]=0x20;spr2[0x10]=2;spr2[0x1ED]=0;
                    assert(som_class_attack(ram,2u,c2,som_word(ram+0xDC)&0x1FFu));
                    assert(!spr2[0x1C]&&spr2[0x1ED]>=46u);
                    unsigned kinds=0;for(unsigned q=0;q<SOM_BOLTS;q++)if(mage_bolts[q].active)kinds|=1u<<mage_bolts[q].kind;
                    if(pick==0u)assert(kinds==1u<<SOM_BOLT_PETAL&&!m[0x1F1]);
                    if(pick==1u)assert(kinds==1u<<SOM_BOLT_TOUCH&&!m[0x1F1]&&(judged_until[0]||judged_until[1]||judged_until[2]));
                    if(pick==2u)assert(kinds==1u<<SOM_BOLT_RIBBON&&m[0x1F1]&&(m[0x190]&4u));
                    m[0x180]=keep_id;memcpy(m+2,keep_x,4);m[0x190]=m[0x191]=0;m[0x1F1]=0;
                }
                for(unsigned pick=0;pick<4u;pick++){       /* orb, chain, wisp, spikes */
                    static const unsigned cs[4]={3u,5u,6u,13u},want[4]={SOM_BOLT_ORB,SOM_BOLT_CHAIN,SOM_BOLT_WISP,SOM_BOLT_SPIKE};
                    memset(mage_bolts,0,sizeof(mage_bolts));
                    uint8_t keep_m[0x200];memcpy(keep_m,m,sizeof(keep_m));
                    const uint8_t keep_layer=spr2[0x0B];spr2[0x0B]=m[0x0B];
                    m[0]=1;m[0x180]=0x14;m[0x190]=m[0x191]=0;m[0x1F1]=0;som_put_word(m+2,hx+24u);som_put_word(m+4,sy);
                    som_put_word(spr2+2,hx);som_put_word(spr2+4,sy);spr2[0x1C]=0x20;spr2[0x10]=2;
                    assert(som_class_attack(ram,2u,cs[pick],som_word(ram+0xDC)&0x1FFu)&&!spr2[0x1C]);
                    assert(mage_bolts[0].active&&mage_bolts[0].kind==(int)want[pick]);
                    unsigned hits=0;
                    for(unsigned f=1;f<70u;f++){
                        som_put_word(spr2+2,hx);
                        if(som_bolt_step(&mage_bolts[0],ram,som_word(ram+0xDC)&0x1FFu,-1,frame_number+f)>=0)hits++;
                    }
                    assert(!mage_bolts[0].active&&m[0x1F1]);          /* it ends, and it hurt */
                    assert(hits==(pick==1u?0u:pick==2u?2u:1u));       /* the wisp hits both ways */
                    memcpy(m,keep_m,sizeof(keep_m));spr2[0x0B]=keep_layer;
                }
                {   /* sleep: not on the immune, not on bosses */
                    uint8_t o2[0x200]={0};o2[0x180]=0x14;
                    assert(som_class_status(o2,0x10u,60u)&&(o2[0x190]&0x10)&&o2[0x1B2]==60u);
                    o2[0x190]=0;o2[0x1AA]=0x10;assert(!som_class_status(o2,0x10u,60u));
                    o2[0x1AA]=0;o2[0x180]=0x60;assert(!som_class_status(o2,0x10u,60u));
                    o2[0x180]=0x14;o2[0x191]=4;assert(!som_class_status(o2,0x10u,60u));   /* familiar */
                }
                memset(mage_bolts,0,sizeof(mage_bolts));memset(judged_until,0,sizeof(judged_until));
                memcpy(spr2,keep_s,0x200);disciplines.party[1]=keep_party2;
                memset(mage_bolts,0,sizeof(mage_bolts));
                memcpy(ram+0xE600,keep_m,sizeof(keep_m));
                ram[0xE4]=keep_e[0];ram[0xE6]=keep_e[1];ram[0xD0]=keep_e[2];ram[0x7D]=keep_e[3];
                hero[0x01C]=0;hero[0x011]=0;hero[0x030]=0;
            }
            /* An NPC or a chest right in front: B goes to the game (talk,
             * open), no spell. */
            {
                uint8_t keep_obj[0x200];memcpy(keep_obj,ram+0xE600,sizeof(keep_obj));
                uint8_t *o=ram+0xE600;memset(o,0,0x200);
                o[0]=1;o[0x0B]=hero[0x0B];o[0x180]=0x90;o[0x3E]=0x21;           /* an NPC */
                const unsigned px=som_bun_u16(hero+2)+16u,py=som_bun_u16(hero+4);
                o[2]=(uint8_t)px;o[3]=(uint8_t)(px>>8);o[4]=(uint8_t)py;o[5]=(uint8_t)(py>>8);
                hero[0x010]=0x02;hero[0x186]=5;hero[0x170]=0;
                mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
                assert(hero[0x170]==0);                                    /* no spell */
                mocked_buttons=0;som_input_poll();
                o[0x180]=0x56;o[0x3E]=0;                                   /* a chest */
                mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);som_input_poll();
                assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
                mocked_buttons=0;som_input_poll();
                hero[0x010]=0x82;                                          /* behind him: no */
                assert(!som_interact_ahead_by(ram,0u));
                hero[0x010]=0x02;hero[0x186]=0;
                memcpy(ram+0xE600,keep_obj,sizeof(keep_obj));
            }
            hero[0x186]=5;hero[0x01C]=0;hero[0x060]=0;
            disciplines.selected=keep;
        }
        /* Holding X steps to the next Ninjutsu spell instead of casting. */
        x_cast_at=0;                             /* an earlier tap's jutsu, still waiting */
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_input_poll();
        frame_number+=SOM_X_HOLD_FRAMES;som_input_poll();
        assert(som_current_spell()==0x06);       /* Freeze */
        mocked_buttons=0;som_input_poll();
        assert(hero[0x170]==0);                  /* release after a cycle: no cast */
        for(unsigned i=0;i<som_class_spell_count(2)-1u;i++)som_next_spell();
        assert(som_current_spell()==SOM_SPELL_FIRE_BALL);  /* wraps */
        /* L2+X: a ninja with a short-range weapon casts Shunshin (X hidden);
         * with a long-range weapon the old native ring input is swallowed. */
        {
            const uint8_t weapon=hero[0x1E4];
            hero[0x1E4]=4;                       /* long range */
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X));
            som_input_poll();
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
            assert(!som_bun_alive(&bunshin));
            mocked_buttons=0;som_input_poll();
            hero[0x1E4]=0;                       /* short range, hero free */
            hero[0x01C]=0;hero[0x060]=0;hero[0x190]=0;hero[0x191]=0;
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X));
            som_input_poll();
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
            const int shunshin=som_bun_alive(&bunshin)&&bunshin_shunshin;
            mocked_buttons=0;som_input_poll();
            som_bun_clear(&bunshin);bunshin_shunshin=0;
            hero[0x1E4]=weapon;
            assert(shunshin);
        }
        /* Infinite MP (gambit test switch) keeps casting possible at 0 MP. */
        hero[0x186]=0;hero[0x187]=0;hero[0x01C]=0;hero[0x060]=0;
        gambits.infinite_mp=1;som_infinite_mp_frame();
        assert(hero[0x186]==SOM_TEST_MP);
        assert(som_cast_spell(ram,SOM_SPELL_FIRE_BALL)&&hero[0x060]==0x20);
        hero[0x186]=97;som_infinite_mp_frame();assert(hero[0x186]==SOM_TEST_MP);
        ram[0xE4]=0;hero[0x186]=3;som_infinite_mp_frame();
        assert(hero[0x186]==3);                  /* outside field play: untouched */
        ram[0xE4]=1;
        gambits.infinite_mp=0;som_infinite_mp_changed();
        assert(hero[0x186]==0);                  /* back to the real maximum */
        som_infinite_mp_frame();assert(hero[0x186]==0);
        gambit_file="/nonexistent/flxtr-gambits";gambits.infinite_mp=1;som_gambit_load();
        assert(!gambits.infinite_mp);            /* never carries over a load */
        hero[0x01C]=0;hero[0x060]=0;
        /* Weapon names from the ROM name block; the samurai's class weapon
         * is pinned at its grade (sword 5, Masamune in Turbo). */
        {
            const unsigned ev=0x800u+0x32u+1u*9u+5u;
            const unsigned text=0x9000u;
            rom[0x0A0000+(ev-0x400u)*2u]=(uint8_t)text;rom[0x0A0000+(ev-0x400u)*2u+1u]=(uint8_t)(text>>8);
            static const uint8_t masamune[]={0xA7,0x81,0x93,0x81,0x8D,0x95,0x8E,0x85,0x00};
            memcpy(rom+0x0A0000+text,masamune,sizeof(masamune));
            assert(!strcmp(som_weapon_name(1,5),"Masamune"));
            assert(som_class_arm_slot(SOM_DISC_BUSHIDO,1)==0u&&som_class_arm_slot(SOM_DISC_BUSHIDO,5)==1u);
            assert(som_class_arm_slot(SOM_DISC_BUSHIDO,2)==3u);
            assert(som_class_arm_slot(SOM_DISC_FLOW,5)==2u);
            const uint8_t keep=disciplines.selected;
            disciplines.selected=SOM_DISC_BUSHIDO;
            ram[0xCC55]=(uint8_t)((ram[0xCC55]&0xF0u)|2u);           /* sword at grade 2 */
            hero[0x1E4]=0;hero[0x01C]=0;hero[0x060]=0;
            som_style_frame(ram);
            assert((ram[0xCC55]&0x0Fu)==5u);
            disciplines.selected=keep;
        }
        #undef EXPECT_TILES
        physical_state=mocked_input;mocked_has_vram=0;
        pressed=100;pressed_too=100;
        som_disc_init(&disciplines);
        som_rom=NULL;som_rom_size=0;weapon_gfx_verified=0;
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
    /* use_candy: one Candy fewer, 100 HP queued for the game; the last one
     * empties its slot and the later items move up. use_herb: poison only. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));memset(ram+0xCC48,0xFF,11);
        ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xE000]=1;
        ram[0xE182]=40;ram[0xE184]=117;
        ram[0xCC48]=0x43;ram[0xCC49]=0x60;                     /* 2 of item 3, 3 Candy */
        assert(som_use_candy(ram,0u)&&ram[0xCC49]==0x40&&ram[0xE1F3]==100&&ram[0xCC48]==0x43);
        ram[0xE1F3]=200;assert(som_use_candy(ram,0u)&&ram[0xE1F3]==255&&ram[0xCC49]==0x20);
        ram[0xCC4A]=0x24;                                      /* 1 Medical Herb after it */
        assert(som_use_candy(ram,0u)&&ram[0xCC49]==0x24&&ram[0xCC4A]==0xFF);  /* last Candy */
        assert(!som_use_candy(ram,0u));                           /* none left */
        ram[0xCC4A]=0x20;ram[0xE200]=1;ram[0xE382]=12;ram[0xE384]=100;
        assert(som_use_candy(ram,1u)&&ram[0xE3F3]==100&&ram[0xE1F3]==255);
        ram[0xE200]=0;ram[0xE382]=ram[0xE384]=ram[0xE3F3]=0;
        /* use_rope: refused without the game's permission, the rope or a way
         * out; the script address is checked against the ROM. */
        assert(!som_use_rope(ram)&&ram[0xD0]==0);
        ram[0x11F]=0x40;ram[0x108]=0x4A;ram[0x109]=1;assert(!som_use_rope(ram)&&ram[0xD0]==0);
        ram[0xCC4B]=0x26;som_use_rope(ram);
        assert(ram[0xD0]==0||(ram[0xD1]==0xF7&&ram[0xD2]==0x90&&ram[0xD3]==0xCA));
        ram[0xD0]=ram[0xD1]=ram[0xD2]=ram[0xD3]=0;ram[0xCC4B]=0xFF;ram[0x11F]=0;
        assert(!som_use_herb(ram)&&ram[0xCC49]==0x24);         /* not poisoned */
        ram[0xE190]=0x01;ram[0xE191]=0x20;                     /* poison (0x2001 live) */
        assert(som_use_herb(ram)&&ram[0xE191]==0x00&&ram[0xE190]==0x01&&ram[0xCC49]==0xFF);
        ram[0xE191]=0x20;assert(!som_use_herb(ram)&&ram[0xE191]==0x20);   /* no Herb */
        ram[0xE190]=ram[0xE191]=0;
        ram[0xCC49]=0xE0;assert(som_use_candy(ram,0u)&&ram[0xCC49]==0xC0); /* Turbo: 7 */
        ram[0xCC49]=0x60;ram[0xD0]=0x81;assert(!som_use_candy(ram,0u)); /* dialogue */
        ram[0xD0]=0;ram[0xE182]=0;assert(!som_use_candy(ram,0u));      /* KO */
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
        som_disc_init(&disciplines);memset(member_pal_have,0,sizeof(member_pal_have));
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF20,original,32));               /* no discipline */
        disciplines.selected=0;                               /* Swordmaster frost */
        som_hero_palette_frame(ram);
        uint8_t once[32];memcpy(once,ram+0xDF20,32);
        assert(!memcmp(once,original,16)&&!memcmp(once+30,original+30,2)); /* 0-7, 15 kept */
        assert(memcmp(once+16,original+16,14));                    /* clothes 8-14 tinted */
        assert(!memcmp(ram+0x0720,once,32));                       /* working buffer too */
        memset(ram+0x0720,0x77,32);                                /* an event fade */
        som_hero_palette_frame(ram);
        assert(ram[0x0720+2]==0x77);                               /* left alone */
        ram[0xE4]=1;ram[0xE6]=0x0F;                                /* field play */
        som_hero_palette_frame(ram);                               /* block/dash glow */
        assert(!memcmp(ram+0x0720,once,32));                       /* held at the tint */
        memcpy(ram+0x0720,original,32);                            /* ROM reload ($C1:CA04) */
        ram[0x0720+20]^=0x11;                                      /* ... then flashed */
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0x0720,once,32));                       /* old palette never shows */
        ram[0xE4]=0;ram[0xE6]=0;
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
        ram[0xE4]=1;ram[0xE6]=0x0F;memset(ram+0x0720,0x55,32);  /* no class, block glow */
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0x0720,original,32));               /* glow held off */
        ram[0xE4]=0;ram[0xE6]=0;
        for(unsigned d=0;d<SOM_DISCIPLINE_COUNT;d++){           /* every class has colours */
            disciplines.selected=(uint8_t)d;
            assert(som_disc_visual_profile(&disciplines)!=SOM_PAL_ORIGINAL);
        }
        som_disc_init(&disciplines);memset(member_pal_have,0,sizeof(member_pal_have));
    }
    /* The companions wear their own class colours (2026-11-20): the girl
     * (slot 1, palette 2 at 7EDF40) and the sprite (slot 2, palette 3). */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));
        ram[0xE000]=1;ram[0xE08B]=0x22;
        ram[0xE200]=1;ram[0xE28B]=0x24;ram[0xE384]=82;      /* girl, max HP 82 */
        ram[0xE400]=1;ram[0xE48B]=0x26;ram[0xE584]=77;      /* sprite */
        for(unsigned i=0;i<96;i++)ram[0xDF20+i]=ram[0x0720+i]=(uint8_t)(0x40+i);
        uint8_t original[96];memcpy(original,ram+0xDF20,96);
        som_disc_init(&disciplines);memset(member_pal_have,0,sizeof(member_pal_have));
        assert(disciplines.party[0]==SOM_GIRL_DEFAULT_CLASS&&
               disciplines.party[1]==SOM_SPRITE_DEFAULT_CLASS);
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF20,original,32));                 /* boy: no class */
        for(unsigned who=1;who<3;who++){
            const uint8_t *pal=ram+0xDF20+32*who,*was=original+32*who;
            for(unsigned c=1;c<16;c++){
                const int hair=who==1u&&c>=SOM_GIRL_HAIR_FIRST&&c<SOM_GIRL_HAIR_FIRST+3u;
                const int clothes=(som_member_clothes[who]>>c)&1u;
                assert((clothes||hair)==(memcmp(pal+2*c,was+2*c,2)!=0)); /* clothes (+ her hair) */
                if(hair)assert((pal[2*c]|pal[2*c+1]<<8)==som_girl_hair[c-SOM_GIRL_HAIR_FIRST]);
            }
            assert(!memcmp(ram+0x0700+32*(who+1),pal,32));       /* shown buffer too */
        }
        uint8_t girl[32];memcpy(girl,ram+0xDF40,32);
        som_hero_palette_frame(ram);
        assert(!memcmp(ram+0xDF40,girl,32));                     /* never stacks */
        ram[0xE4]=1;ram[0xE6]=0x0F;ram[0x0740+20]^=0x11;         /* her hit flash */
        som_hero_palette_frame(ram);
        assert(memcmp(ram+0x0740,girl,32));                      /* kept: only the boy's glow is held */
        ram[0xE4]=0;ram[0xE6]=0;
        disciplines.party[0]=SOM_DISCIPLINE_NONE;                 /* girl: no class */
        som_hero_palette_frame(ram);
        {   /* her own clothes back, the black hair kept */
            uint8_t want[32];memcpy(want,original+32,32);som_member_hair(want,1u);
            assert(!memcmp(ram+0xDF40,want,32));
        }
        ram[0xE384]=0;memset(ram+0xDF40,0x99,32);                /* not joined yet */
        disciplines.party[0]=SOM_DISC_NINJUTSU;
        som_hero_palette_frame(ram);
        assert(ram[0xDF40]==0x99&&ram[0xDF40+31]==0x99);        /* palette 2 left alone */
        /* Menu: X picks whose class B sets; B there leaves the boy's. */
        som_disc_init(&disciplines);
        assert(som_disc_press(&disciplines,1u<<RETRO_DEVICE_ID_JOYPAD_UP)==SOM_DISC_OPENED);
        char line[33];som_disc_line(&disciplines,0,line);assert(!strncmp(line,"BOY CLASS",9));
        assert(som_disc_press(&disciplines,1u<<RETRO_DEVICE_ID_JOYPAD_X)==SOM_DISC_WHO);
        som_disc_line(&disciplines,0,line);assert(!strncmp(line,"GIRL CLASS",10));
        som_disc_line(&disciplines,1+SOM_GIRL_DEFAULT_CLASS,line);assert(line[1]=='*');
        disciplines.cursor=SOM_DISC_NINJUTSU;
        assert(som_disc_press(&disciplines,1u<<RETRO_DEVICE_ID_JOYPAD_B)==SOM_DISC_PARTY_SELECTED);
        assert(disciplines.party[0]==SOM_DISC_NINJUTSU&&disciplines.selected==SOM_DISCIPLINE_NONE);
        som_disc_press(&disciplines,1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_disc_press(&disciplines,1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_disc_line(&disciplines,0,line);assert(!strncmp(line,"BOY CLASS",9));
        assert(strlen(line)<=32u);
        som_disc_init(&disciplines);memset(member_pal_have,0,sizeof(member_pal_have));
    }
    /* Class shortcuts follow saved order. Opening the editor requires the
     * Classes icon and preserves the ring owner and native entries. */
    {
        backend_retro_get_memory_data=mocked_memory_data;
        backend_retro_get_memory_size=mocked_memory_size;
        memset(mocked_wram,0,sizeof(mocked_wram));
        mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;
        som_disc_init(&disciplines);disciplines.selected=SOM_DISC_BUSHIDO;
        disc_file="/nonexistent/flxtr-disciplines";
        ring_ai_enabled=0;som_manual_disciplines=0;special_stage=bash_stage=0;
        on_turbo=1;test_enabled=1;menu_hold_until_release=menu_was_open=0;
        physical_state=mocked_input_mask;pressed=100;
        const uint16_t l2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        const uint16_t l1=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L);
        const uint16_t r1=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);
        const uint16_t b=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        const uint16_t a=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
        const uint16_t start=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);
        mocked_buttons=0;last_physical_buttons=0;som_input_poll();
        const int lock_before=lock_slot;
        mocked_buttons=l2;som_input_poll();
        mocked_buttons=l2|r1;som_input_poll();
        assert(disciplines.selected==SOM_DISC_BUSHIDO+1u);         /* next */
        mocked_buttons=l2;som_input_poll();
        mocked_buttons=l2|l1;som_input_poll();
        mocked_buttons=l2;som_input_poll();
        mocked_buttons=l2|l1;som_input_poll();
        assert(disciplines.selected==SOM_DISC_BUSHIDO-1u);         /* back twice */
        assert(lock_slot==lock_before);                            /* no lock-on toggle */
        disciplines.selected=0;mocked_buttons=l2;som_input_poll();
        mocked_buttons=l2|l1;som_input_poll();
        assert(disciplines.selected==SOM_DISCIPLINE_COUNT-1u);     /* wraps */
        mocked_buttons=0;som_input_poll();
        /* L2+R1 follows the player's class order, which the save keeps. */
        {
            char tmp[]="/tmp/flxtr-disc-order-test-XXXXXX";
            const int fd=mkstemp(tmp);assert(fd>=0);close(fd);
            disc_file=tmp;
            for(unsigned k=0;k<SOM_DISCIPLINE_COUNT;k++)
                disciplines.order[k]=(uint8_t)(SOM_DISCIPLINE_COUNT-1u-k);
            disciplines.selected=SOM_DISC_BUSHIDO;
            mocked_buttons=l2;som_input_poll();
            mocked_buttons=l2|r1;som_input_poll();
            assert(disciplines.selected==SOM_DISC_BUSHIDO-1u);     /* next in that order */
            mocked_buttons=0;som_input_poll();
            som_disc_save();
            som_disc_init(&disciplines);assert(disciplines.order[0]==0u);
            som_disc_load();
            assert(disciplines.order[0]==SOM_DISCIPLINE_COUNT-1u&&
                   disciplines.selected==SOM_DISC_BUSHIDO-1u);
            /* An older file without the order: 0..15. */
            FILE *old=fopen(tmp,"w");assert(old);
            fprintf(old,"v1 3");
            for(unsigned k=0;k<SOM_DISCIPLINE_COUNT;k++)fprintf(old," 1 0");
            fprintf(old," p 4 3\n");fclose(old);
            som_disc_load();
            assert(disciplines.selected==3u&&disciplines.order[0]==0u&&disciplines.order[15]==15u);
            unlink(tmp);disc_file="/nonexistent/flxtr-disciplines";
        }
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R3);som_input_poll(); /* R3 opens the ring */
        assert(!som_manual_disciplines);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==
               (1u<<RETRO_DEVICE_ID_JOYPAD_Y));
        frame_number+=SOM_START_RING_FRAMES;
        mocked_buttons=0;som_input_poll();

        mocked_wram[0x7D]=1;mocked_wram[0x1808]=1;mocked_wram[0x1809]=2;
        mocked_wram[0x180A]=4;mocked_wram[0x180B]=SOM_RING_CONFIG_ENTRIES;
        mocked_buttons=b;som_input_poll();                         /* Controller Settings */
        assert(!som_manual_disciplines);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
        mocked_buttons=0;som_input_poll();
        mocked_buttons=start;som_input_poll();                     /* Start in the ring */
        assert(!som_manual_disciplines);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        mocked_buttons=0;som_input_poll();

        /* The native descriptor list can rotate after visiting Status. */
        classes_ring_enabled=1;
        mocked_wram[0x180A]=4;
        som_put_word(mocked_wram+0x1880+2*4,SOM_RING_CLASSES_DESCRIPTOR);
        mocked_wram[0x1801]=0x80u;
        mocked_buttons=b;som_input_poll();
        assert(!som_manual_disciplines);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
        mocked_buttons=0;som_input_poll();
        mocked_wram[0x1801]=0;
        mocked_buttons=b;som_input_poll();
        assert(som_manual_disciplines&&disciplines.open&&disciplines.who==1u);
        assert(disciplines.cursor==disciplines.party[0]);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
        mocked_buttons=0;som_input_poll();
        mocked_buttons=a;som_input_poll();
        assert(!som_manual_disciplines&&som_ring_on_classes(mocked_wram));
        som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==0);
        mocked_buttons=0;som_input_poll();
        memset(mocked_wram,0,sizeof(mocked_wram));som_disc_init(&disciplines);
        menu_hold_until_release=menu_was_open=0;
    }
    /* A Mentalist blinks the endangered ally, leaving the caster, the
     * other ally, and the enemy where they were. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));
        ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xC1]=ram[0xC3]=2;
        for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
            uint8_t *actor=som_ally_obj(ram,who);
            actor[0]=1;actor[0x184]=100;actor[0x182]=who==0u?30:100;
            som_put_word(actor+2,(uint16_t)(200+40*who));som_put_word(actor+4,200);
        }
        uint8_t *enemy=ram+0xE600;
        enemy[0]=1;enemy[0x180]=0x57;enemy[0x182]=100;
        som_put_word(enemy+2,204);som_put_word(enemy+4,200);
        rescue_ready[1]=0;familiar.slot=-1;
        ram[0x1B800]=3;                  /* all landing spots blocked */
        assert(!som_telekinesis_rescue(ram,1u,1)&&som_word(ram+0xE002)==200);
        ram[0x1B800]=0;
        assert(som_telekinesis_rescue(ram,1u,1));
        assert(som_rescue_threat(ram,(int)som_word(ram+0xE002),
                                 (int)som_word(ram+0xE004),0u)>=48);
        assert(som_word(ram+0xE202)==240&&som_word(ram+0xE402)==280);
        assert(som_word(enemy+2)==204&&som_word(ram+0xE182)==30);
        assert(!som_telekinesis_rescue(ram,1u,1));
        rescue_ready[1]=0;
    }
    /* Companion class moves (2026-11-20): the girl (Cleric) heals the
     * hurt boy, the sprite (Mage) bolts the monster; a Ninja girl dashes to
     * the monster's far side and cuts on arrival. Weapon holder bits. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));
        ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xC1]=0x04;ram[0xC3]=0x04;   /* 1024 x 1024 map */
        const unsigned px[3]={200,220,240};
        for(unsigned who=0;who<3;who++){
            uint8_t *o=ram+0xE000+0x200*who;
            o[0]=1;o[2]=(uint8_t)px[who];o[4]=200;o[0x184]=100;o[0x182]=who==0u?30:who==2u?65:100;
        }
        uint8_t *m=ram+0xE600;m[0]=1;som_put_word(m+2,240+50);m[4]=200;        /* 50 px from the sprite */
        som_disc_init(&disciplines);familiar.slot=-1;ally_tech_ready=0;
        const unsigned long saved_frame=frame_number;frame_number=1000;
        som_ally_tech_frame(ram);
        assert(ram[0xE1F3]>0&&ram[0xE5F3]>0);                  /* party healed */
        assert(m[0x1F1]>0);                                       /* the monster hit */
        const uint8_t heal=ram[0xE1F3],hurt=m[0x1F1];
        frame_number++;som_ally_tech_frame(ram);
        assert(ram[0xE1F3]==heal&&m[0x1F1]==hurt);                /* cooldown */
        assert(ally_tech[0].en.energy[4]<som_energy_max(1));      /* her own energy paid */
        uint8_t *focus=ram+0xE800;
        focus[0]=1;focus[0x180]=7;focus[4]=200;som_put_word(focus+2,340);
        ally_order.map=0;ally_order.slot=1;ally_order.identity=7;
        ally_order.until=frame_number+20;
        m[0x1F1]=0;ally_tech_ready=0;
        som_ally_tech_frame(ram);
        assert(focus[0x1F1]>0&&m[0x1F1]==0);
        ally_order.slot=SOM_ALLY_SUPPORT_SLOT;
        focus[0x1F1]=0;ram[0xE1F3]=0;ally_tech_ready=0;
        som_ally_tech_frame(ram);
        assert(ram[0xE1F3]>0&&focus[0x1F1]==0&&m[0x1F1]==0);
        frame_number=ally_order.until;ally_tech_ready=0;
        ram[0xE42C]=2;  /* A human controls the sprite. */
        som_ally_tech_frame(ram);
        assert(m[0x1F1]==0);
        ram[0xE42C]=0;
        som_ally_tech_frame(ram);
        assert(m[0x1F1]>0);
        ally_order.until=0;focus[0]=0;
        /* A Ninja girl: dash past the monster, cut once there. */
        disciplines.party[0]=SOM_DISC_NINJUTSU;disciplines.party[1]=SOM_DISCIPLINE_NONE;
        ally_tech_ready=0;m[0x1F1]=0;som_put_word(m+2,220+60);
        for(unsigned i=0;i<20;i++){frame_number++;som_ally_tech_frame(ram);}
        assert(!ally_tech[0].dashing);
        assert(som_bun_u16(ram+0xE202)==220+60+20);               /* the far side */
        assert(m[0x1F1]>0&&!(ram[0xE20E]&0x80));                  /* cut, seen again */
        /* Keep-back: an Archer sprite with a monster 20 px away walks off
         * 2 px a frame; not while she swings, nor while the player drives her. */
        {
            disciplines.party[1]=SOM_DISC_ARCHERY;disciplines.party[0]=SOM_DISCIPLINE_NONE;
            som_put_word(ram+0xE402,240);som_put_word(m+2,260);m[0x1F1]=0;
            frame_number++;ally_tech_ready=0;som_ally_tech_frame(ram);
            assert(som_bun_u16(ram+0xE402)==238);
            ram[0xE41C]=0x20;                                      /* mid-swing: stays */
            frame_number++;ally_tech_ready=0;som_ally_tech_frame(ram);
            assert(som_bun_u16(ram+0xE402)==238);
            ram[0xE41C]=0;ram[0xD9]=4;                             /* on pad 1 */
            frame_number++;ally_tech_ready=0;som_ally_tech_frame(ram);
            assert(som_bun_u16(ram+0xE402)==238);
            ram[0xD9]=1;som_put_word(m+2,300);                     /* 60 px: far enough */
            frame_number++;ally_tech_ready=0;som_ally_tech_frame(ram);
            assert(som_bun_u16(ram+0xE402)==238);
            disciplines.party[0]=SOM_DISC_NINJUTSU;disciplines.party[1]=SOM_DISCIPLINE_NONE;
            som_put_word(m+2,220+60);
        }
        /* Dead or not joined: nothing. */
        ally_tech_ready=0;m[0x1F1]=0;ram[0xE382]=0;ram[0xE383]=0;
        for(unsigned i=0;i<5;i++){frame_number++;som_ally_tech_frame(ram);}
        assert(m[0x1F1]==0);
        frame_number=saved_frame;som_disc_init(&disciplines);ally_tech_ready=0;
        /* One holder per weapon: 1 boy, 2 girl, 3 sprite in bits 6-7. */
        uint8_t owned[8]={0x05,0x02,0xFF,0,0,0,0,0};
        som_weapon_hold(owned,0,1);assert(owned[0]==0x85);
        som_weapon_hold(owned,0,0);assert(owned[0]==0x85);       /* hers: not taken */
        som_weapon_release(owned,0,0);assert(owned[0]==0x85);    /* not his to drop */
        som_weapon_release(owned,0,1);assert(owned[0]==0x05);
        som_weapon_hold(owned,2,2);assert(owned[2]==0xFF);       /* not owned */
        som_weapon_hold(owned,1,2);assert(owned[1]==0xC2);
    }
    /* The playing song (7E:1E05) is announced once per change. */
    {
        static uint8_t ram[SOM_WRAM_SIZE];
        memset(ram,0,sizeof(ram));
        som_song_load(&song_names,"/nonexistent/flxtr-songs");song_last=0x100u;
        ram[0x1E05]=0x04;som_song_frame(ram);assert(song_last==0x04);
        ram[0x1E05]=0x40;som_song_frame(ram);assert(song_last==0x04);   /* not a song id */
        ram[0x1E05]=0x0E;som_song_frame(ram);assert(song_last==0x0E);
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
        assert(disciplines.party[0]==SOM_GIRL_DEFAULT_CLASS);
        disciplines.party[0]=SOM_DISC_BUSHIDO;disciplines.party[1]=SOM_DISCIPLINE_NONE;
        som_disc_save();som_disc_init(&disciplines);som_disc_load();
        assert(disciplines.party[0]==SOM_DISC_BUSHIDO&&disciplines.party[1]==SOM_DISCIPLINE_NONE&&
               disciplines.selected==6);
        /* A save from before rogue/samurai/archer (9 disciplines) loads. */
        FILE *old=fopen(disc_path,"w");
        assert(old&&fputs("v1 2 3 10 1 0 4 20 1 0 1 0 1 0 1 0 1 0 1 0\n",old)>=0&&fclose(old)==0);
        som_disc_init(&disciplines);som_disc_load();
        assert(disciplines.selected==2&&disciplines.level[0]==3&&disciplines.level[2]==4&&
               disciplines.level[9]==1&&disciplines.level[11]==1);
        assert(disciplines.party[1]==SOM_SPRITE_DEFAULT_CLASS);      /* no companions: defaults */
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
    som_put_word(mocked_wram+0xDC,130);
    assert(!flxtr_som_ai_command("party_order_129_1_7"));
    assert(!flxtr_som_ai_command("party_order_130_4_7"));
    assert(flxtr_som_ai_command("party_order_130_1_7"));
    assert(ally_order.until==frame_number+SOM_ALLY_ORDER_FRAMES&&ally_order.slot==1);
    assert(flxtr_som_ai_command("party_release")&&!ally_order.until);
    assert(flxtr_som_ai_command("ai_off")&&!flxtr_som_ai_enabled());
    direct_transport=0;
    /* The acting hero (2026-11-22): with pad 1 on a Bushido girl, Issen
     * dashes her (not the boy) from her own energy; the actor stays hers
     * until the dash is over, then follows pad 1 back to the boy. */
    {
        uint8_t *ram=mocked_wram;
        memset(ram,0,SOM_WRAM_SIZE);
        ram[0xE4]=1;ram[0xE6]=0x0F;ram[0xC1]=0x04;ram[0xC3]=0x04;
        ram[0xA8]=150;ram[0xAA]=100;ram[0x10E]=1;               /* a camera with them in view */
        for(unsigned who=0;who<2u;who++){
            uint8_t *o=ram+0xE000+0x200*who;
            o[0]=1;som_put_word(o+2,(uint16_t)(200+40*who));o[4]=200;o[0x10]=0x02;
            o[0x182]=50;o[0x184]=50;
        }
        som_disc_init(&disciplines);disciplines.party[0]=SOM_DISC_BUSHIDO;
        ram[0xD9]=2;ram[0xDA]=1;
        som_actor=0;som_actor_update();assert(som_actor==1u);
        assert(som_actor_class()==SOM_DISC_BUSHIDO&&SOM_ACT(ram)==ram+0xE200);
        energy_free=0;issen_ready=0;evade_active=0;
        ally_tech[0].en.energy[SOM_DISC_BUSHIDO]=(uint16_t)som_energy_max(1);
        const unsigned girl_energy=ally_tech[0].en.energy[SOM_DISC_BUSHIDO];
        const unsigned boy_energy=energy.energy[SOM_DISC_BUSHIDO];
        assert(som_issen_start(ram)&&evade_active&&issen_active);
        assert(ally_tech[0].en.energy[SOM_DISC_BUSHIDO]<girl_energy&&
               energy.energy[SOM_DISC_BUSHIDO]==boy_energy);
        ram[0xD9]=1;ram[0xDA]=0x80;                      /* Select mid-dash */
        som_hop_step();frame_number++;
        som_actor_update();assert(som_actor==1u);       /* not while she dashes */
        assert(som_bun_u16(ram+0xE202)>240&&som_bun_u16(ram+0xE002)==200);
        evade_active=0;issen_active=0;hop_frames_left=0;
        som_actor_update();assert(som_actor==0u);       /* free: back to the boy */
        energy_free=1;
        physical_state=mocked_input_mask;
        memcpy(ram+0xE400,ram+0xE200,0x200);
        som_put_word(ram+0xE402,300);
        for(unsigned who=1;who<SOM_PARTY_MEMBERS;who++){
            disciplines.party[who-1u]=SOM_DISC_ARCHER;
            ram[0xD9]=(uint8_t)(1u<<who);ram[0xDA]=0;ram[0xDB]=0;
            uint8_t *actor=ram+0xE000u+0x200u*who;
            const unsigned start_x=som_word(actor+2);
            mocked_buttons=0;som_input_poll();
            mocked_buttons=(1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT);
            som_input_poll();
            assert(evade_active&&som_actor==who);
            som_hop_step();frame_number++;
            assert(som_word(actor+2)>start_x&&som_word(ram+0xE002)==200);
            assert(trail_owner==who);
            evade_active=0;hop_frames_left=0;
        }
        mocked_buttons=0;som_input_poll();
        som_disc_init(&disciplines);memset(ram,0,SOM_WRAM_SIZE);
    }
    /* Enemy tactics in the proxy: configuration, Classic, General ranks and
     * the habits a general keeps between encounters. */
    {
        backend_retro_get_memory_data=mocked_memory_data;backend_retro_get_memory_size=mocked_memory_size;
        uint8_t *w=mocked_wram;memset(w,0,SOM_WRAM_SIZE);
        w[0xE4]=1;w[0xE6]=0x0F;w[0xD0]=0;w[0x7D]=0;w[0xDC]=52;
        uint8_t *h=w+0xE000;h[0]=1;som_put_word(h+2,100);som_put_word(h+4,100);som_put_word(h+0x182,50);
        uint8_t *m=w+0xE600;m[0]=1;m[0x180]=0x00;som_put_word(m+2,150);som_put_word(m+4,100);
        som_put_word(m+0x182,20);som_put_word(m+0x184,20);m[6]=0x82;
        const int keep_fam=familiar.slot;familiar.slot=-1;
        setenv("FLXTR_SOM_FIGHT_DIFFICULTY","classic",1);unsetenv("FLXTR_SOM_FIGHT_ARENA");
        som_fight_configure();
        assert(fight.level==SOM_FIGHT_CLASSIC);
        {uint8_t before[SOM_WRAM_SIZE];memcpy(before,w,SOM_WRAM_SIZE);
         for(unsigned i=0;i<60;i++){frame_number++;som_enemy_fight_frame();}
         assert(!memcmp(before,w,SOM_WRAM_SIZE)&&!fight.foe[0].live);}
        setenv("FLXTR_SOM_FIGHT_DIFFICULTY","hard",1);setenv("FLXTR_SOM_FIGHT_ARENA","zoner",1);
        setenv("FLXTR_SOM_FIGHT_RANK","elite",1);
        som_fight_configure();
        assert(fight.level==SOM_FIGHT_HARD&&fight.arena_archetype==SOM_ARCH_ZONER&&fight_arena_rank==SOM_RANK_ELITE);
        for(unsigned i=0;i<30;i++){frame_number++;som_enemy_fight_frame();}
        /* not the arena: the species' own archetype, common rank */
        assert(fight.foe[0].live&&fight.foe[0].archetype==SOM_ARCH_RUSHDOWN&&fight.foe[0].rank==SOM_RANK_COMMON);
        som_put_word(w+0xDC,SOM_ARENA_MAP);
        for(unsigned i=0;i<5;i++){frame_number++;som_enemy_fight_frame();}
        assert(fight.foe[0].archetype==SOM_ARCH_ZONER&&fight.foe[0].rank==SOM_RANK_ELITE);
        som_put_word(w+0xDC,52);
        /* Generals: rank 1..5 -> Veteran..General, a nemesis from rank 4 */
        SomGeneral g;memset(&g,0,sizeof(g));g.rank=1;g.nemesis=-1;
        assert(som_fight_general_rank(&g)==SOM_RANK_VETERAN);
        g.rank=5;assert(som_fight_general_rank(&g)==SOM_RANK_GENERAL);
        g.nemesis=2;assert(som_fight_general_rank(&g)==SOM_RANK_NEMESIS);
        g.rank=3;assert(som_fight_general_rank(&g)==SOM_RANK_CAPTAIN);
        {const int on=generals_on;const unsigned count=general_count;const SomGeneral keep=generals[0];
         const SomGeneralLive keep_live=generals_live[0];
         generals_on=1;general_count=1;memset(&generals[0],0,sizeof(generals[0]));
         generals[0].species=0x00;generals[0].rank=4;generals[0].nemesis=1;generals[0].habit[0][SOM_HABIT_BLOCK]=200;
         generals_live[0].slot=0;
         for(unsigned i=0;i<3;i++){frame_number++;som_enemy_fight_frame();}
         assert(fight.foe[0].rank==SOM_RANK_NEMESIS);
         som_fight_general_elected(0);
         assert(fight.foe[0].habit[0][SOM_HABIT_BLOCK]==150);          /* 3/4 remembered */
         fight.foe[0].habit[1][SOM_HABIT_RANGED]=180;
         frame_number++;som_enemy_fight_frame();
         som_fight_general_concluded(0);
         assert(generals[0].habit[1][SOM_HABIT_RANGED]==180&&generals[0].habit[0][SOM_HABIT_BLOCK]>=150);
         generals_on=on;general_count=count;generals[0]=keep;generals_live[0]=keep_live;}
        /* the gambit menu sets and saves the level; the env var wins on load */
        {SomGambits gm;memset(&gm,0,sizeof(gm));gm.open=1;gm.cursor=SOM_GAMBIT_FIGHT_ITEM;gm.fight_level=2;
         char line[33];
         assert(som_gambit_line(&gm,SOM_GAMBIT_FIGHT_ITEM+1u,line)&&strstr(line,"Enemy AI: Normal"));
         assert(som_gambit_press(&gm,(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))==SOM_GAMBIT_FIGHT_CHANGED&&gm.fight_level==3);
         assert(som_gambit_press(&gm,(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))==SOM_GAMBIT_FIGHT_CHANGED&&gm.fight_level==2);
         gm.fight_level=4;assert(som_gambit_press(&gm,(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B))==SOM_GAMBIT_FIGHT_CHANGED&&gm.fight_level==0);
         assert(som_gambit_line(&gm,SOM_GAMBIT_FIGHT_ITEM+1u,line)&&strstr(line,"Classic"));
         char tmpl[]="/tmp/som-fight-XXXXXX";const int fd=mkstemp(tmpl);assert(fd>=0);close(fd);
         const char *keep_file=fight_file;fight_file=tmpl;
         unsetenv("FLXTR_SOM_FIGHT_DIFFICULTY");
         som_gambit_write(fight_file,"expert");som_fight_configure();
         assert(fight.level==SOM_FIGHT_EXPERT&&gambits.fight_level==SOM_FIGHT_EXPERT);
         setenv("FLXTR_SOM_FIGHT_DIFFICULTY","easy",1);som_fight_configure();
         assert(fight.level==SOM_FIGHT_EASY);
         unlink(tmpl);fight_file=keep_file;}
        /* switching back to Classic leaves nothing behind */
        setenv("FLXTR_SOM_FIGHT_DIFFICULTY","classic",1);som_fight_configure();
        assert(!fight.foe[0].live&&!fight.foe[0].token_until);
        unsetenv("FLXTR_SOM_FIGHT_DIFFICULTY");unsetenv("FLXTR_SOM_FIGHT_ARENA");unsetenv("FLXTR_SOM_FIGHT_RANK");
        som_fight_configure();assert(fight.level==SOM_FIGHT_NORMAL);
        familiar.slot=keep_fam;memset(w,0,SOM_WRAM_SIZE);
    }
    unlink(path);unlink(wram_path);unlink(wram_tmp_path);unlink(ring_ai_path);
    puts("PASS: Turbo actions, tile steps, ring AI signal, idle auto-start, override, and WRAM export");
    return 0;
}
