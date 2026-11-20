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
    mocked_wram[0xD0]=0;mocked_wram[0x7D]=1;
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);som_input_poll();
    assert(!som_manual_disciplines);mocked_buttons=0;som_input_poll();
    backend_retro_get_memory_data=NULL;                      /* unreadable */
    mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);som_input_poll();
    assert(!som_manual_disciplines);mocked_buttons=0;som_input_poll();
    backend_retro_get_memory_data=mocked_memory_data;mocked_wram[0x7D]=0;
    /* Manual-play Class Progress menu via L2+R2 in verified field play. */
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
    mocked_buttons=0;som_input_poll();
    /* The menu, then R1: the MUSIC page. Down, B tunes the radio station (a
     * request line for the Steam Link's music bridge); A closes the menu. */
    {
        char juke_req[]="/tmp/flxtr-juke-req-test-XXXXXX";
        const int fd=mkstemp(juke_req);assert(fd>=0);close(fd);
        juke_request_file=juke_req;juke_list_file="/nonexistent/flxtr-juke-list";
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_R2));
        som_input_poll();mocked_buttons=0;som_input_poll();
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
    memset(mocked_wram,0,sizeof(mocked_wram));
    assert(som_disc_weapons[0][0]==1&&som_disc_weapons[0][1]==2);
    assert(som_disc_weapons[1][0]==0&&som_disc_weapons[1][1]==3);
    assert(som_disc_weapons[2][0]==1&&som_disc_weapons[2][1]==6);
    assert(som_disc_weapons[10][0]==1&&som_disc_weapons[10][1]==5);
    /* Grant only missing weapons on selection, retaining tiers/ownership. */
    memset(mocked_wram,0,sizeof(mocked_wram));
    mocked_wram[0xE000]=1;mocked_wram[0xE4]=1;mocked_wram[0xE6]=0x0F;
    for(unsigned i=0;i<8;i++)mocked_wram[0xCC54+i]=0xFF;
    mocked_wram[0xCC55]=0x43; /* already-owned tier-3 sword */
    assert(som_class_supply(mocked_wram,2)==2); /* Ninja gains chakram + Taijutsu glove */
    assert(mocked_wram[0xCC55]==0x43&&mocked_wram[0xCC5A]==0&&mocked_wram[0xCC54]==0);
    assert(som_class_supply(mocked_wram,2)==0); /* idempotent */
    assert(som_class_supply(mocked_wram,10)==1); /* Samurai gains yumi */
    assert(mocked_wram[0xCC59]==0&&mocked_wram[0xCC55]==0x43);
    mocked_wram[0xE4]=0;mocked_wram[0xCC56]=0xFF;
    assert(som_class_supply(mocked_wram,0)==0); /* no changes outside field */
    assert(mocked_wram[0xCC56]==0xFF);

    /* Taijutsu: glove, glove, sword, then again; a pause restarts it. */
    frame_number=1000;taijutsu_last=0;taijutsu_step=0;
    assert(som_taijutsu_peek()==0u);som_taijutsu_advance();
    assert(som_taijutsu_peek()==0u);som_taijutsu_advance();
    assert(som_taijutsu_peek()==1u);som_taijutsu_advance();
    assert(som_taijutsu_peek()==0u);som_taijutsu_advance();
    frame_number+=SOM_TAIJUTSU_RESET+1ul;
    assert(som_taijutsu_peek()==0u&&taijutsu_step==0u);
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
        for(unsigned c=0;c<SOM_DISCIPLINE_COUNT;c++)
            assert(som_weapon_styles[som_style_index(c,0)].weapon==som_disc_weapons[c][0]&&
                   som_weapon_styles[som_style_index(c,2)].weapon==som_disc_weapons[c][0]);
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
    /* While the AI plays the hot ranges are also written ready-made. */
    {
        char hot_path[]="/tmp/flxtr-som-hot-test-XXXXXX";
        int hfd=mkstemp(hot_path);assert(hfd>=0);close(hfd);unlink(hot_path);
        char hot_tmp[64];snprintf(hot_tmp,sizeof(hot_tmp),"%s.tmp",hot_path);
        hot_file=hot_path;hot_temp_file=hot_tmp;
        assert(som_export_wram()&&access(hot_path,F_OK)!=0);     /* AI off: none */
        ring_ai_enabled=1;assert(som_export_wram());ring_ai_enabled=0;
        FILE *hot=fopen(hot_path,"rb");assert(hot);
        static uint8_t got[24*1024];assert(fread(got,1,sizeof(got),hot)==sizeof(got));
        assert(fgetc(hot)==EOF);fclose(hot);
        assert(got[0]==0&&got[8*1024]==(uint8_t)((39*1024)&0xff)&&
               got[10*1024+5]==(uint8_t)((50*1024+5)&0xff)&&got[20*1024+7]==(uint8_t)((96*1024+7)&0xff));
        assert(som_export_wram()&&access(hot_path,F_OK)!=0);     /* AI off again: removed */
    }
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
    pressed=RETRO_DEVICE_ID_JOYPAD_R3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES-1;som_input_poll();
    pressed=100;som_input_poll();
    assert(!ring_ai_enabled&&access(ring_ai_path,F_OK)!=0);      /* too short */
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_R3;som_input_poll();
    frame_number+=SOM_SWITCH_HOLD_FRAMES;
    pressed=RETRO_DEVICE_ID_JOYPAD_B;som_input_poll();           /* another button */
    pressed=100;frame_number+=1;som_input_poll();
    assert(!ring_ai_enabled);
    frame_number+=10;pressed=RETRO_DEVICE_ID_JOYPAD_L2;som_input_poll();
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
            {3,2,2,2,3,3,3,3},{3,3},0,0,1};
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
        /* New physical controller: R2 is native R guard; R1 is an
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
        const uint16_t native_r=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);
        mocked_buttons=guard_r2;som_input_poll();
        assert(som_guarding());
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2)==0);
        assert((uint16_t)som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==native_r);
        physical_state=mocked_input;pressed=RETRO_DEVICE_ID_JOYPAD_R2;
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2)==0);
        pressed=100;physical_state=mocked_input_mask;
        mocked_buttons=0;som_input_poll();assert(!som_guarding());
        for(unsigned i=0;i<3u;i++){
            mocked_buttons=style_r1;som_input_poll();
            assert(styles.selected[0]==(i==0u?2u:i==1u?0u:1u));
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)==0);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
            som_input_poll(); /* holding R1 must not cycle again */
            assert(styles.selected[0]==(i==0u?2u:i==1u?0u:1u));
            mocked_buttons=0;som_input_poll();
        }
        /* Old L1+B style shortcut is moved to L3+B. Its B is hidden,
         * and pressing L1 alone does not send an unrelated native L. */
        const uint16_t l3=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L3);
        mocked_buttons=(uint16_t)(l3|(1u<<RETRO_DEVICE_ID_JOYPAD_B));som_input_poll();
        assert(styles.selected[0]==2u&&b_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        mocked_buttons=0;som_input_poll();
        /* L3+X is native L2+X without firing a spell on release. */
        mocked_buttons=(uint16_t)(l3|(1u<<RETRO_DEVICE_ID_JOYPAD_X));som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==1);
        mocked_buttons=0;som_input_poll();
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        som_input_poll();                           /* Y: axe now, swing as B */
        assert(hero[0x1E4]==2&&y_weapon_swap&&!y_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
        mocked_buttons=0;som_input_poll();
        hero[0x01C]=0x20;                           /* axe swing still playing */
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        som_input_poll();                           /* B waits: no axe swing */
        assert(hero[0x1E4]==2&&b_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
        hero[0x01C]=0;som_input_poll();             /* still held, hero free */
        assert(hero[0x1E4]==1&&!b_weapon_blocked);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1);
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
        /* L3+Y is the native ring, since L2+Y is now a seal special. */
        mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L3)|(1u<<RETRO_DEVICE_ID_JOYPAD_Y));
        som_input_poll();
        assert(!special_stage&&!y_weapon_swap);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2)==1);
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==
               ((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_Y)));
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
        /* Artificer A: the plain evade, 3 px x 16 frames away from the
         * facing, untouchable for the move plus the grace frames; A, D-pad
         * and attacks hidden. No Kawarimi on A. */
        for(unsigned m=0;m<3;m++)ram[0xE600+0x200*m]=0;
        uint8_t *foe=ram+0xE600;
        disciplines.selected=14;hero[0x191]=0;hero[0x01C]=0;hero[0x011]=0;
        hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;   /* facing right */
        ram[0xA8]=100;ram[0xA9]=0;ram[0xAA]=100;ram[0xAB]=0;            /* camera */
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
            assert(som_bun_u16(hero+2)==200-48&&som_bun_u16(hero+4)==200);
            assert(!evade_active&&!kawarimi_log_start&&trail_until>=hop_start+16u);
            /* The camera and both layers' scroll words followed him. */
            assert(som_bun_u16(ram+0xA8)==52&&som_bun_u16(ram+0xB0)==68&&
                   som_bun_u16(ram+0xAC)==0xFFFFu-17u&&som_bun_u16(ram+0xB4)==306-48&&
                   som_bun_u16(ram+0xAA)==100);
        }
        /* At the map's left edge the camera stays put. */
        ram[0xA8]=10;ram[0xA9]=0;hero[2]=200;hero[0x010]=0x02;
        mocked_buttons=r1a;som_input_poll();mocked_buttons=0;som_input_poll();
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(som_bun_u16(hero+2)==200-48&&som_bun_u16(ram+0xA8)==0);
        /* Rogue A off a combat map: the game has no cartwheel there, so the
         * 4 x 16 = 64 px hop; A held right moves right. */
        disciplines.selected=9;hero[2]=200;ram[0xA8]=100;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(!cartwheel_native&&!(hero[0x0E]&0x80u));
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(som_bun_u16(hero+2)==200+64&&!evade_active&&som_bun_u16(ram+0xA8)==164);
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
         * request is withdrawn and the 64 px hop runs instead. */
        hero[0x1E5]=0xFF;hero[0x010]=0x82;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(cartwheel_native&&hero[0x1E5]==0x13&&hero[0x010]==0x02);
        for(unsigned f=0;f<60;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(!evade_active&&!cartwheel_native&&hero[0x1E5]==0xFF&&som_bun_u16(hero+2)==200-64);
        /* Busy (already in a pose): straight to the hop. */
        hero[0x01C]=0x40;hero[2]=200;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(!cartwheel_native&&hero[0x1E5]==0xFF);
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        hero[0x01C]=0;ram[0xED]=0;hero[2]=200;hero[3]=0;
        /* Hold A: the hop fires on the press, then Kawarimi is armed once
         * he is free again. A tap leaves Kawarimi unarmed. */
        {
            const uint16_t a=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
            uint8_t camera[16];memcpy(camera,ram+0xA8,sizeof(camera));
            const uint8_t keep_class=disciplines.selected,keep_weapon=hero[0x1E4];
            disciplines.selected=SOM_DISC_NINJUTSU;hero[0x1E4]=1;      /* Kawarimi: ninja only */
            hero[2]=200;hero[3]=0;hero[0x01C]=0;
            mocked_buttons=a;som_input_poll();assert(evade_active);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;som_input_poll();}
            assert(!evade_active&&!kawarimi_armed_until);
            hero[2]=200;hero[3]=0;hero[0x01C]=0;
            mocked_buttons=a;som_input_poll();assert(evade_active);
            for(unsigned f=0;f<60&&!kawarimi_armed_until;f++){
                som_hop_step();som_evade_tick();frame_number++;som_input_poll();
            }
            assert(kawarimi_armed_until&&!evade_active);
            mocked_buttons=0;som_input_poll();
            kawarimi_armed_until=0;
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            memcpy(ram+0xA8,camera,sizeof(camera));
            /* Any other class: holding A arms nothing. */
            disciplines.selected=11;                                   /* archer */
            mocked_buttons=a;
            for(unsigned f=0;f<60;f++){som_hop_step();som_evade_tick();frame_number++;som_input_poll();}
            assert(!kawarimi_armed_until);
            mocked_buttons=0;som_input_poll();
            for(unsigned f=0;f<30;f++){som_hop_step();som_evade_tick();frame_number++;}
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
            for(unsigned forward=0;forward<2u;forward++){
                hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;
                hero[0x010]=0x02;hero[0x01C]=0;hero[0x011]=0;
                hero[0x060]=0;hero[0x190]=hero[0x191]=0;hero[0x1E5]=0xFF;
                const uint16_t a=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
                mocked_buttons=(uint16_t)(a|(forward?1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT:0u));
                som_input_poll();mocked_buttons=0;som_input_poll();
                assert(evade_active&&!cartwheel_native&&hero[0x1E5]==0xFF);
                assert(hop_dx==(forward?3:-3));
                for(unsigned f=0;f<40u;f++){
                    som_hop_step();som_ukemi_frame(ram);som_evade_tick();frame_number++;
                }
                assert(!evade_active&&hero[0x010]==0x02);
                assert(som_bun_u16(hero+2u)==(unsigned)(200+(forward?54:-54)));
            }
            ram[0xED]=keep_ed;disciplines.selected=keep;
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
            assert(som_bun_u16(hero+2)==200-48);                       /* back from the facing */
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
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
            foe[0]=0;foe[0x1F1]=0;x_move_ready=0;
            disciplines.selected=keep;
        }
        /* L1 exclusively locks onto the nearest monster; a second tap
         * releases, and neither L1+B nor L3 shortcuts steal targeting. */
        {
            const uint16_t l1=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L);
            hero[2]=200;hero[3]=0;hero[4]=200;hero[5]=0;hero[0x010]=0x02;hero[0x01C]=0;
            hero[0x060]=0;hero[0x190]=hero[0x191]=0;
            foe[0]=1;foe[0x180]=7;foe[0x190]=foe[0x191]=0;foe[0x182]=50;
            foe[2]=150;foe[3]=0;foe[4]=210;foe[5]=0;                   /* to the left */
            mocked_buttons=l1;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(lock_slot==0);
            som_hop_step();
            assert(hero[0x010]==0x82);                                 /* faces it */
            hero[0x010]=0x01;som_style_after(ram);assert(hero[0x010]==0x82);
            mocked_buttons=l1;som_input_poll();
            mocked_buttons=(uint16_t)(l1|(1u<<RETRO_DEVICE_ID_JOYPAD_B));som_input_poll();
            mocked_buttons=0;som_input_poll();
            assert(lock_slot<0);      /* second press toggles immediately */
            mocked_buttons=l1;som_input_poll();mocked_buttons=0;som_input_poll();
            assert(lock_slot==0);
            foe[0x191]=0x80;som_hop_step();                            /* it dies */
            assert(lock_slot<0);
            foe[0]=0;foe[0x191]=0;hero[0x010]=0x02;
        }
        /* Scaled weapon art: the Dai Katana (200 %) blanks the native weapon
         * tiles (a copy is kept) and draws the pack's frame for the hero's
         * frame, mirrored for a left facing; the Katana (100 %) puts the
         * native tiles back. */
        {
            char scale_path[]="/tmp/flxtr-weapon-scale-test-XXXXXX";
            const int fd=mkstemp(scale_path);assert(fd>=0);
            uint8_t pack[10+44+6+1];memset(pack,0,sizeof(pack));
            memcpy(pack,"SOMWORK1",8);pack[8]=1;
            pack[10]=0xD3;pack[11]=14;pack[12]=10;pack[13]=1;      /* 200 %, row 14, frame 10 */
            pack[54]=1;pack[57]=1;pack[58]=1;pack[59]=16;          /* one 1x1 blade pixel */
            assert(write(fd,pack,sizeof(pack))==(ssize_t)sizeof(pack));close(fd);
            weapon_scale_file=scale_path;weapon_scale_tried=0;
            const uint8_t keep=disciplines.selected,keep_style=styles.selected[SOM_DISC_BUSHIDO];
            disciplines.selected=SOM_DISC_BUSHIDO;styles.selected[SOM_DISC_BUSHIDO]=2;   /* Dai Katana */
            hero[0x1E4]=1;hero[0x1E3]=14;hero[0x026]=20;hero[0x028]=0x40;
            for(unsigned i=0;i<0x800;i++)ram[0x2000+i]=(uint8_t)(i*7u+1u);
            som_weapon_scale_frame(ram);
            assert(weapon_tiles_hidden&&ram[0x2000]==0&&ram[0x27FF]==0);
            som_weapon_scale_pick(ram);
            assert(weapon_scaled.active&&weapon_scaled.entry.weapon==14u&&weapon_scaled.dir==3u);
            hero[0x026]=22;som_weapon_scale_pick(ram);assert(!weapon_scaled.active);   /* no art */
            styles.selected[SOM_DISC_BUSHIDO]=1;                    /* Katana: native art */
            som_weapon_scale_frame(ram);
            assert(!weapon_tiles_hidden&&ram[0x2000]==1&&ram[0x2001]==8);
            som_weapon_scale_pick(ram);assert(!weapon_scaled.active);
            unlink(scale_path);weapon_scale_file=SOM_WEAPON_SCALE_FILE;weapon_scale_tried=0;
            free(weapon_scale_pack);weapon_scale_pack=NULL;weapon_scale_pack_size=0;
            free(weapon_scale_index);weapon_scale_index=NULL;
            disciplines.selected=keep;styles.selected[SOM_DISC_BUSHIDO]=keep_style;
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
        assert(som_bun_u16(hero+2)==200-54&&som_bun_u16(hero+4)==200);
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
        assert(!evade_active&&som_bun_u16(hero+2)==200-54);
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
        assert(evade_active&&!hero[0x1F1]&&som_bun_u16(hero+2)==200-54);
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        /* Dodge Mastery (Ninjutsu 10: rank 2, +4 frames): 66 px; off: 54. */
        disciplines.level[2]=10;hero[2]=200;
        assert(som_handle_command("kawarimi"));
        hero[0x1F1]=9;som_hop_step();
        assert(som_bun_u16(hero+2)==200-66);
        for(unsigned f=0;f<50;f++){som_hop_step();som_evade_tick();frame_number++;}
        perks_enabled=0;hero[2]=200;
        assert(som_handle_command("kawarimi"));
        hero[0x1F1]=9;som_hop_step();
        assert(som_bun_u16(hero+2)==200-54);
        for(unsigned f=0;f<50;f++){som_hop_step();som_evade_tick();frame_number++;}
        perks_enabled=1;disciplines.level[2]=1;hero[0x1F1]=0;
        /* Kawarimi is a ninja skill: monk, archer, rogue and the classes
         * without a dodge are refused. */
        for(unsigned c=0;c<4u;c++){
            static const uint8_t others[4]={1,11,9,4};
            disciplines.selected=others[c];hero[2]=200;hero[4]=200;hero[0x1F1]=0;
            assert(!som_handle_command("kawarimi")&&!kawarimi_armed_until);
        }
        /* Ninja with the sword: A is Ukemi, back with no direction (here
         * the hop fallback, 54 px, facing kept); the AI's "shunshin"
         * puts a clone on each side across the facing (clone, ninja,
         * clone); no dash. Not with clones out, not with another weapon. */
        disciplines.selected=2;hero[0x1E4]=1;hero[0x191]=0;hero[2]=200;hero[4]=200;
        hero[0x010]=0x02;
        mocked_buttons=r1a;som_input_poll();mocked_buttons=0;som_input_poll();
        assert(evade_active&&!som_bun_alive(&bunshin));
        for(unsigned f=0;f<40;f++){som_hop_step();som_evade_tick();frame_number++;}
        assert(!evade_active&&som_bun_u16(hero+2)==200-54&&hero[0x010]==0x02);
        hero[2]=200;hero[0x1E4]=6;assert(!som_handle_command("shunshin"));hero[0x1E4]=1;
        assert(som_handle_command("shunshin"));
        assert(!evade_active&&!kawarimi_armed_until&&som_bun_alive(&bunshin)==2);
        assert(som_bun_u16(hero+2)==200&&som_bun_u16(hero+4)==200);
        assert(bunshin.x[0]==200&&bunshin.y[0]==176&&bunshin.x[1]==200&&bunshin.y[1]==224);
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
        assert(bunshin.x[0]==200&&bunshin.y[0]==176&&bunshin.x[1]==200&&bunshin.y[1]==224);
        hero[2]=202;som_bunshin_frame(ram);                  /* clones copy the step */
        assert(bunshin.x[0]==202&&bunshin.x[1]==202);
        uint8_t *m0=ram+0xE600;
        m0[0]=1;m0[0x180]=7;m0[0x190]=m0[0x191]=0;m0[0x1F1]=0;
        m0[2]=210;m0[3]=0;m0[4]=230;m0[5]=0;                  /* touches clone 1 */
        som_bunshin_frame(ram);
        assert(!bunshin.alive[1]&&bunshin.alive[0]&&m0[0x1F1]==som_bunshin_damage());
        m0[0]=0;
        mocked_buttons=(uint16_t)(r1a|(1u<<RETRO_DEVICE_ID_JOYPAD_UP));som_input_poll();
        mocked_buttons=0;som_input_poll();
        assert(som_bun_u16(hero+4)==176&&bunshin.y[0]==200);   /* swapped places */
        m0[0]=1;m0[0x1F1]=0;m0[2]=202;m0[3]=0;m0[4]=186;m0[5]=0;   /* at the hero */
        frame_number+=SOM_BUN_SWAP_COOLDOWN+1;bunshin.until=frame_number+100;
        som_bunshin_frame(ram);
        assert(som_bun_u16(hero+4)==200&&!som_bun_alive(&bunshin)&&m0[0x1F1]); /* substitution */
        m0[0]=0;
        /* No clone ends up in a wall. */
        ram[0x1B800+4*9]=0x03;                                 /* tile 9 solid */
        for(unsigned c=0;c<128;c++)ram[0x10000+((176u>>4)*128u)+c]=9;  /* wall above */
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
            assert(bash_stage==SOM_BASH_NONE&&special_stage==2u&&special_weapon==0u&&special_suppress);
            assert(flykick.active);                           /* drawn through the finisher */
            special_stage=0;special_suppress=0;frame_number+=30;som_flykick_frame(ram);
            assert(!flykick.active&&!(hero[0x0E]&0x80u));
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
        assert(hero[0x01C]==0&&hero[0x060]==0&&hero[0x170]==0);
        hero[0x186]=5;near[0x191]=0x80;          /* nearest one is dead */
        hero[0x179]=4;hero[0x17A]=5;             /* stale list from an old cast */
        memcpy(hero_before,hero,sizeof(hero_before));
        uint8_t buffer_before[0x800];memcpy(buffer_before,ram+0x2000,sizeof(buffer_before));
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_input_poll();
        assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==0);
        mocked_buttons=0;som_input_poll();       /* tap released: cast */
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
        rom[0x102AD0+0x08*64+8]=0x40;           /* undecoded target kind */
        assert(!som_cast_spell(ram,0x08)&&hero[0x170]==0);
        /* Casters trade weapons for spells: Mage B = Fire Ball, Y = Freeze,
         * X starts at the third spell; B without the MP is the weapon swing. */
        {
            const uint8_t keep=disciplines.selected;
            disciplines.selected=3;
            rom[0x102AD0+0x06*64+8]=0x81;rom[0x102AD0+0x06*64+15]=2;     /* Freeze */
            hero[0x186]=5;hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
            som_input_poll();
            assert(hero[0x170]==0x0C);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
            mocked_buttons=0;som_input_poll();
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
            som_input_poll();
            assert(hero[0x170]==0x06);
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)==0);
            mocked_buttons=0;som_input_poll();
            assert(som_current_spell()==som_class_spell(3,2));       /* X: the rest */
            hero[0x01C]=0;hero[0x060]=0;hero[0x170]=0;hero[0x186]=0;
            mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
            som_input_poll();
            assert(hero[0x170]==0);                                  /* no MP: */
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==1); /* swing */
            mocked_buttons=0;som_input_poll();
            hero[0x186]=5;hero[0x01C]=0;hero[0x060]=0;
            disciplines.selected=keep;
        }
        /* Holding X steps to the next Ninjutsu spell instead of casting. */
        mocked_buttons=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        som_input_poll();
        frame_number+=SOM_X_HOLD_FRAMES;som_input_poll();
        assert(som_current_spell()==0x06);       /* Freeze */
        mocked_buttons=0;som_input_poll();
        assert(hero[0x170]==0);                  /* release after a cycle: no cast */
        for(unsigned i=0;i<som_class_spell_count(2)-1u;i++)som_next_spell();
        assert(som_current_spell()==SOM_SPELL_FIRE_BALL);  /* wraps */
        /* L2+X: a ninja with a short-range weapon casts Shunshin (X hidden);
         * with a long-range weapon L2+X keeps the game's own X. */
        {
            const uint8_t weapon=hero[0x1E4];
            hero[0x1E4]=4;                       /* long range */
            mocked_buttons=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|(1u<<RETRO_DEVICE_ID_JOYPAD_X));
            som_input_poll();
            assert(som_input_state(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)==1);
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
        assert(som_use_candy(ram)&&ram[0xCC49]==0x40&&ram[0xE1F3]==100&&ram[0xCC48]==0x43);
        ram[0xE1F3]=200;assert(som_use_candy(ram)&&ram[0xE1F3]==255&&ram[0xCC49]==0x20);
        ram[0xCC4A]=0x21;                                      /* 1 Medical Herb after it */
        assert(som_use_candy(ram)&&ram[0xCC49]==0x21&&ram[0xCC4A]==0xFF);  /* last Candy */
        assert(!som_use_candy(ram));                           /* none left */
        assert(!som_use_herb(ram)&&ram[0xCC49]==0x21);         /* not poisoned */
        ram[0xE190]=0x01;ram[0xE191]=0x20;                     /* poison (0x2001 live) */
        assert(som_use_herb(ram)&&ram[0xE191]==0x00&&ram[0xE190]==0x01&&ram[0xCC49]==0xFF);
        ram[0xE191]=0x20;assert(!som_use_herb(ram)&&ram[0xE191]==0x20);   /* no Herb */
        ram[0xE190]=ram[0xE191]=0;
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
        som_disc_init(&disciplines);hero_pal_have=0;
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
