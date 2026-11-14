/*
 * FlXtR Secret of Mana Turbo opt-in input proxy.
 * A Libretro core wrapper; loads the installed Snes9x 2005 Plus as a library.
 * It does NOT patch emulator code, ROM, or save data. Game-RAM writes are
 * limited to the explicit experimental clone_enemy command (undoable) and
 * the opt-in living-world npc_walk command (src/som_npc_walk.h), which is
 * off unless its enable marker exists and the ROM fingerprints match.
 *
 * Exact usage and safety caveats: docs/secret-of-mana-ai-input-bridge.md
 */
#define _POSIX_C_SOURCE 200809L
#include "vendor/libretro.h"
#include "som_ai_actions.h"
#include "som_overlay.h"
#include "som_gambit.h"
#include "som_npc_walk.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef SOM_PROXY_BASE_CORE
#define SOM_PROXY_BASE_CORE "/home/apps/retroarch/cores/snes9x2005_plus_libretro.so"
#endif
#define SOM_TRIGGER_POLL_FRAMES 2u
#define SOM_TRIGGER_DEFAULT "/tmp/flxtr-som-ai-go"
#define SOM_HOLD_FRAMES 30u
#define SOM_WRAM_SIZE 0x20000u
/* Ten snapshots per second keep the SkyLoft planner on fresh positions. */
#define SOM_WRAM_EXPORT_FRAMES 6u
/* Modern cores (including the pinned snes9x2005) read the whole pad with one
 * bitmask query when the frontend reports RETRO_ENVIRONMENT_GET_INPUT_BITMASKS.
 * The vendored libretro.h predates these constants. */
#ifndef RETRO_DEVICE_ID_JOYPAD_MASK
#define RETRO_DEVICE_ID_JOYPAD_MASK 256
#endif

static void *backend;
static retro_input_state_t physical_state;
static retro_input_poll_t physical_poll;
static unsigned long frame_number;
static int armed, cancelled, completed, game_loaded, on_turbo, test_enabled;
static unsigned long start_frame, injected_queries;
static SomAction active_action;
static unsigned action_frames;
static int autonomous_mode;
static unsigned auto_step;
static unsigned long auto_deadline;
static uint16_t last_physical_buttons;
static unsigned long last_attack_frame;
static unsigned combo_count;
static const char *trigger_file;
static const char *action_file="/tmp/flxtr-som-ai-action";
static const char *wram_file="/tmp/flxtr-som-wram.bin";
static const char *wram_temp_file="/tmp/flxtr-som-wram.bin.tmp";
static const char *ring_ai_file="/tmp/flxtr-som-ai-enabled";
static int wram_export_error_logged;
static int ring_ai_selector_open,ring_ai_pending,ring_ai_enabled;
/* Idle auto-start: after SOM_IDLE_AUTOSTART_FRAMES (one minute at 60 fps)
 * without any physical button, raise the same AI signal as "ai_on". Once
 * per idle period: a new period starts only with a real button press, so a
 * bridge that stops itself is never restarted in a loop, and "ai_off" stays
 * off until the player has touched the controller again. The marker file
 * SOM_IDLE_DISABLE_FILE turns the feature off. Pausing RetroArch stops
 * retro_run, so time in the RetroArch menu does not count as idle. */
#define SOM_IDLE_AUTOSTART_FRAMES 3600ul
#define SOM_IDLE_DISABLE_FILE "/home/apps/retroarch/cores/experimental/som_ai_idle_autostart_disable"
static const char *idle_disable_file=SOM_IDLE_DISABLE_FILE;
static unsigned long last_input_frame;
static int idle_started;
static uint8_t ring_ai_initial_cell;
static unsigned long ring_ai_grace_until;
/* In-game switch that works with a solo hero (the Action Grid only exists
 * once companions have joined): hold R2 or R3 ALONE for one second and
 * release to raise the same AI signal as "ai_on". The SNES has neither, so
 * the game never reacts to them. (Select cannot be used: on the Steam Link
 * it is RetroArch's hotkey-enable button, which RetroArch withholds from
 * the core while held.) Any button still stops the AI. A hold that starts
 * while the AI runs only stops it, so the release cannot switch it back on. */
#define SOM_SWITCH_HOLD_FRAMES 60ul
static int switch_holding,switch_spoiled;
static unsigned long switch_hold_start;
static int som_write_ring_ai_marker(void);
/* Gambits (src/som_gambit.h): while the AI plays, buttons drive this menu
 * instead of taking control. The enabled mask persists for the bridge. */
#define SOM_GAMBIT_FILE "/home/apps/retroarch/cores/experimental/som_ai_gambits"
static const char *gambit_file=SOM_GAMBIT_FILE;
static SomGambits gambits={0,0,{4,3,3,3,4,1}};
static void som_gambit_load(void){
    char text[16]={0};
    FILE *in=fopen(gambit_file,"r");
    if(in){if(!fgets(text,sizeof(text),in))text[0]=0;fclose(in);}
    som_gambit_parse(&gambits,text[0]?text:NULL);gambits.open=0;gambits.cursor=0;
}
static void som_gambit_save(void){
    char temp[256];
    snprintf(temp,sizeof(temp),"%s.tmp",gambit_file);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    char text[SOM_GAMBIT_COUNT+1];
    som_gambit_format(&gambits,text);
    int ok=fprintf(out,"%s\n",text)>0;
    if(fclose(out)!=0)ok=0;
    if(ok)rename(temp,gambit_file);else unlink(temp);
}
#define SOM_MENU_SCAN_START 0x1800u
#define SOM_MENU_SCAN_LENGTH 0x0200u
static uint8_t som_previous_menu[SOM_MENU_SCAN_LENGTH];
static int som_have_previous_menu;
static retro_environment_t environment_callback;
/* Optional AI message box, drawn on a private copy of each frame. */
#define SOM_MESSAGE_POLL_FRAMES 15u
#define SOM_MESSAGE_FRAMES 240u
static const char *message_file="/tmp/flxtr-som-ai-message";
static retro_video_refresh_t frontend_video;
static int pixel_format; /* RETRO_PIXEL_FORMAT_0RGB1555 until the core says otherwise */
static SomOverlay overlay;
/* Teaching: while the marker exists, physical presses are appended to a log
 * as the action names the bridge can replay. Nothing is injected. */
static const char *record_marker="/tmp/flxtr-som-ai-record";
static const char *record_file="/tmp/flxtr-som-ai-recorded";
static int recording;
static uint8_t *overlay_frame;
static size_t overlay_frame_size;
static void load_backend(void); /* RetroArch may query core info before init. */

#define FORWARD(ret, name, args, callargs) \
    static ret (*backend_##name) args; \
    RETRO_API ret name args { \
        load_backend(); \
        return backend_##name callargs; \
    }
#define FORWARD_VOID(name, args, callargs) \
    static void (*backend_##name) args; \
    RETRO_API void name args { \
        load_backend(); \
        backend_##name callargs; \
    }

FORWARD_VOID(retro_set_audio_sample,(retro_audio_sample_t cb),(cb))
FORWARD_VOID(retro_set_audio_sample_batch,(retro_audio_sample_batch_t cb),(cb))
FORWARD_VOID(retro_set_controller_port_device,(unsigned port,unsigned device),(port,device))
FORWARD_VOID(retro_reset,(void),())
FORWARD(size_t,retro_serialize_size,(void),())
FORWARD(bool,retro_serialize,(void *data,size_t size),(data,size))
FORWARD(bool,retro_unserialize,(const void *data,size_t size),(data,size))
FORWARD_VOID(retro_cheat_reset,(void),())
FORWARD_VOID(retro_cheat_set,(unsigned index,bool enabled,const char *code),(index,enabled,code))
FORWARD(bool,retro_load_game_special,(unsigned type,const struct retro_game_info *info,size_t count),(type,info,count))
FORWARD(unsigned,retro_get_region,(void),())
FORWARD(void *,retro_get_memory_data,(unsigned id),(id))
FORWARD(size_t,retro_get_memory_size,(unsigned id),(id))
FORWARD(unsigned,retro_api_version,(void),())
FORWARD_VOID(retro_get_system_info,(struct retro_system_info *info),(info))
FORWARD_VOID(retro_get_system_av_info,(struct retro_system_av_info *info),(info))

static void (*backend_set_environment)(retro_environment_t);
static void (*backend_set_video_refresh)(retro_video_refresh_t);
static void (*backend_set_input_poll)(retro_input_poll_t);
static void (*backend_set_input_state)(retro_input_state_t);
static void (*backend_init)(void);
static void (*backend_deinit)(void);
static bool (*backend_load_game)(const struct retro_game_info *);
static void (*backend_unload_game)(void);
static void (*backend_run)(void);

/* Keep all Libretro callbacks on the same thread as the original frontend.
 * Never try to emulate frames from a timer/network worker. */
static int16_t som_input_state(unsigned port,unsigned device,unsigned index,unsigned id){
    int16_t actual=physical_state?physical_state(port,device,index,id):0;
    int bitmask=id==RETRO_DEVICE_ID_JOYPAD_MASK;
    if(ring_ai_enabled&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0)
        actual=0;                    /* the player's buttons drive the gambit menu */
    if(port==0 && (device & RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD &&
       index==0 && (id<16||bitmask) && armed && !cancelled && !completed && on_turbo){
        if(actual){cancelled=1;armed=0;autonomous_mode=0;fprintf(stderr,"[som-ai] cancelled: physical input at frame %lu\n",frame_number);return actual;}
        uint16_t wanted=som_action_mask(active_action);
        if(frame_number>=start_frame &&
           frame_number<start_frame+action_frames && wanted &&
           (bitmask||(id<16 && (wanted & (uint16_t)(1u<<id))))){
            injected_queries++;
            return bitmask?(int16_t)wanted:1;
        }
    }
    return actual;
}
static void som_record_presses(uint16_t pressed){
    static const char *const names[16]={"confirm","open_ring","select","start",
        "step_up","step_down","step_left","step_right","cancel","menu_x",
        "prev_ring","next_ring","l2","r2","l3","r3"};
    if(!pressed)return;
    FILE *log=fopen(record_file,"a");
    if(!log)return;
    for(unsigned key=0;key<16;key++)
        if(pressed&(1u<<key))fprintf(log,"%s\n",names[key]);
    fclose(log);
}
static void som_input_poll(void){
    if(physical_poll)physical_poll();
    /* Observe physical attack edges without mutating the game's inputs. */
    if(physical_state && on_turbo){
        uint16_t current=0;
        for(unsigned key=0;key<16;key++)
            if(physical_state(0,RETRO_DEVICE_JOYPAD,0,key))current|=(uint16_t)(1u<<key);
        const uint16_t attack=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        if((current&attack)&&!(last_physical_buttons&attack)){
            combo_count=(frame_number-last_attack_frame<=30 && combo_count<3)?
                combo_count+1:1;
            last_attack_frame=frame_number;
            if(combo_count>=2)
                fprintf(stderr,"[som-ai] COMBO OBSERVED hits=%u frame=%lu (recognition only)\n",
                    combo_count,frame_number);
        }
        const uint16_t pressed=(uint16_t)(current&~last_physical_buttons);
        if(recording)som_record_presses(pressed);
        last_physical_buttons=current;
        if(current){last_input_frame=frame_number;idle_started=0;}
        const uint16_t ai_switch=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_R2)|
                                            (1u<<RETRO_DEVICE_ID_JOYPAD_R3));
        if(current&&!(current&~ai_switch)){
            if(!switch_holding){
                switch_holding=1;switch_hold_start=frame_number;
                switch_spoiled=ring_ai_enabled||armed;
            }
        }else if(current){
            switch_spoiled=1;                 /* another button joined in */
        }else if(switch_holding){
            switch_holding=0;
            if(!switch_spoiled&&frame_number-switch_hold_start>=SOM_SWITCH_HOLD_FRAMES){
                if(som_write_ring_ai_marker()){
                    ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
                    idle_started=1;
                    som_overlay_set(&overlay,"AI ON|any button: gambits",SOM_MESSAGE_FRAMES);
                    fprintf(stderr,"[som-ai] SWITCH-HOLD AI START signal ready at %s\n",
                        ring_ai_file);
                }else fprintf(stderr,"[som-ai] SWITCH-HOLD AI start signal failed\n");
            }
        }
        /* The bridge removes the marker when it stops (safety stop, game
         * exit): never keep swallowing the player's buttons after that. */
        if(ring_ai_enabled&&frame_number%30u==0&&access(ring_ai_file,F_OK)!=0){
            ring_ai_enabled=0;gambits.open=0;
            som_overlay_set(&overlay,"AI stopped|you play",SOM_MESSAGE_FRAMES);
            fprintf(stderr,"[som-ai] AI marker gone: controls back to the player\n");
        }
        if(ring_ai_enabled&&pressed&&frame_number>=ring_ai_grace_until){
            switch(som_gambit_press(&gambits,pressed)){
            case SOM_GAMBIT_CHANGED:
                som_gambit_save();
                {char text[SOM_GAMBIT_COUNT+1];som_gambit_format(&gambits,text);
                 fprintf(stderr,"[som-ai] GAMBITS %s\n",text);}
                break;
            case SOM_GAMBIT_EXIT:
                ring_ai_enabled=0;unlink(ring_ai_file);
                som_overlay_set(&overlay,"You play|hold R2 or R3 1s: AI plays",
                                SOM_MESSAGE_FRAMES);
                fprintf(stderr,"[som-ai] GAMBIT EXIT: controls back to the player\n");
                break;
            default:break;
            }
        }
        if(autonomous_mode && current){
            autonomous_mode=0;armed=0;cancelled=1;
            fprintf(stderr,"[som-ai] AUTO STOP physical controller takeover\n");
        }
    }
    /* Check all physical buttons before injecting input, regardless of which
     * individual IDs the emulator requests in this frame. */
    if(armed&&!cancelled&&!completed&&on_turbo&&physical_state&&!ring_ai_enabled)
        for(unsigned id=0;id<16;id++)
            if(physical_state(0,RETRO_DEVICE_JOYPAD,0,id)){
                cancelled=1;armed=0;autonomous_mode=0;
                fprintf(stderr,"[som-ai] physical controller override on frame %lu\n",frame_number);
                break;
            }
}

/* Candidate RAM offsets are for original SoM, UNVERIFIED on Turbo.
 * Read only and print raw bytes, never treat as validated state. */
static void som_snapshot(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||size<SOM_MENU_SCAN_START+SOM_MENU_SCAN_LENGTH){
        fprintf(stderr,"[som-ai] RAM unavailable or too small (size=%lu)\n",(unsigned long)size);
        return;
    }
    fprintf(stderr,"[som-ai] CANDIDATE frame=%lu size=%lu 7E180B=%02x 7E1846=%02x 7E1847=%02x (unverified Turbo)\n",
        frame_number,(unsigned long)size,ram[0x180b],ram[0x1846],ram[0x1847]);
    if(!som_have_previous_menu){
        memcpy(som_previous_menu,ram+SOM_MENU_SCAN_START,SOM_MENU_SCAN_LENGTH);
        som_have_previous_menu=1;
        fprintf(stderr,"[som-ai] MENU BASELINE frame=%lu range=7E1800..7E19FF; snapshot again after one controlled change\n",frame_number);
        return;
    }
    unsigned changed=0,printed=0;
    for(unsigned i=0;i<SOM_MENU_SCAN_LENGTH;i++){
        uint8_t now=ram[SOM_MENU_SCAN_START+i],before=som_previous_menu[i];
        if(now==before)continue;
        changed++;
        if(printed<48){
            fprintf(stderr,"[som-ai] MENU DIFF 7E%04X %02X->%02X\n",
                SOM_MENU_SCAN_START+i,(unsigned)before,(unsigned)now);
            printed++;
        }
    }
    fprintf(stderr,"[som-ai] MENU DIFF SUMMARY frame=%lu changed=%u printed=%u range=7E1800..7E19FF\n",
        frame_number,changed,printed);
    memcpy(som_previous_menu,ram+SOM_MENU_SCAN_START,SOM_MENU_SCAN_LENGTH);
}

/* Publish one complete raw WRAM image for the read-only SkyLoft observer.
 * Writing a temporary file and renaming it prevents SSH readers from seeing
 * a partially written frame. This never writes to emulator or game memory. */
static int som_export_wram(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||size!=SOM_WRAM_SIZE)return 0;
    int output_fd=open(wram_temp_file,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(output_fd<0)return 0;
    FILE *output=fdopen(output_fd,"wb");
    if(!output){close(output_fd);unlink(wram_temp_file);return 0;}
    int ok=fwrite(ram,1,SOM_WRAM_SIZE,output)==SOM_WRAM_SIZE;
    /* Readers only need the atomic rename; no fsync, so ten snapshots per
     * second never force flash writes if /tmp is not a RAM disk. */
    if(ok)ok=fflush(output)==0;
    if(fclose(output)!=0)ok=0;
    if(ok)ok=rename(wram_temp_file,wram_file)==0;
    if(!ok)unlink(wram_temp_file);
    return ok;
}

static int som_write_ring_ai_marker(void){
    int fd=open(ring_ai_file,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    static const char enabled[]="enabled\n";
    int ok=write(fd,enabled,sizeof(enabled)-1)==(ssize_t)(sizeof(enabled)-1);
    if(ok)ok=fsync(fd)==0;
    if(close(fd)!=0)ok=0;
    if(!ok)unlink(ring_ai_file);
    return ok;
}

/* The existing Action Grid remains unmodified. Confirming its currently
 * selected 4x4 cell is an otherwise harmless gesture that requests external
 * AI control. Changing cells continues to configure the game's native AI. */
static void som_ring_ai_tick(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    int ring_open=ram[0x007D]==1;
    int action_grid=ring_open&&ram[0xA20F]==4;
    int selector=action_grid&&(ram[0xA212]&0x20)!=0;
    uint8_t cell=ram[0xA136]&0x0F;
    if(selector&&!ring_ai_selector_open)ring_ai_initial_cell=cell;
    if(!selector&&ring_ai_selector_open&&action_grid&&cell==ring_ai_initial_cell){
        ring_ai_pending=1;
        fprintf(stderr,"[som-ai] RING AI requested from unchanged Action Grid cell %u\n",
            (unsigned)cell);
    }
    ring_ai_selector_open=selector;
    if(ring_ai_pending&&!ring_open){
        ring_ai_pending=0;
        if(som_write_ring_ai_marker()){
            ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
            fprintf(stderr,"[som-ai] RING AI START signal ready at %s\n",ring_ai_file);
        }else fprintf(stderr,"[som-ai] RING AI start signal failed\n");
    }
}
static void som_idle_tick(void){
    if(!test_enabled||!game_loaded||!on_turbo||idle_started||ring_ai_enabled||armed)return;
    if(frame_number-last_input_frame<SOM_IDLE_AUTOSTART_FRAMES)return;
    if(frame_number%SOM_MESSAGE_POLL_FRAMES)return;
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    if(ram[0x007D]==1)return;            /* ring menu open: retry when closed */
    idle_started=1;
    if(access(idle_disable_file,F_OK)==0)return;
    if(som_write_ring_ai_marker()){
        ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
        fprintf(stderr,"[som-ai] IDLE AI START after %lu frames without input\n",
            frame_number-last_input_frame);
    }else fprintf(stderr,"[som-ai] IDLE AI start signal failed\n");
}
/* EXPERIMENTAL, the proxy's only game-memory write: copy one live monster
 * object (0x200 bytes, slots 7EE600/E800/EA00 per cidwel/som-decomp) into an
 * empty monster slot, 32 px to the right. A full emulator snapshot is taken
 * first; "undo_spawn" restores it exactly. The game's own spawner ($C0:DE3B)
 * is not called, so engine bookkeeping outside the slot is not updated. */
#define SOM_MONSTER_BASE 0xE600u
#define SOM_OBJECT_SIZE 0x200u
static uint8_t *undo_state;
static size_t undo_size;
static int undo_ready;
static int som_monster_live(const uint8_t *object){
    uint16_t status=(uint16_t)(object[0x190]|object[0x191]<<8);
    return object[0]==1&&object[0x180]<0x80&&!(status&0x8000u);
}
static int som_clone_enemy(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       !backend_retro_serialize_size||!backend_retro_serialize)return 0;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE){
        fprintf(stderr,"[som-ai] SPAWN refused: WRAM unavailable\n");return 0;
    }
    int source=-1,target=-1;
    for(unsigned slot=0;slot<3;slot++){
        uint8_t *object=ram+SOM_MONSTER_BASE+slot*SOM_OBJECT_SIZE;
        if(som_monster_live(object)){if(source<0)source=(int)slot;}
        else if(object[0]!=1&&target<0)target=(int)slot;
    }
    if(source<0){fprintf(stderr,"[som-ai] SPAWN refused: no live monster to copy\n");return 0;}
    if(target<0){fprintf(stderr,"[som-ai] SPAWN refused: no empty monster slot\n");return 0;}
    size_t size=backend_retro_serialize_size();
    if(size>undo_size){
        uint8_t *grown=realloc(undo_state,size);
        if(!grown){fprintf(stderr,"[som-ai] SPAWN refused: no memory for undo\n");return 0;}
        undo_state=grown;undo_size=size;
    }
    if(!size||!backend_retro_serialize(undo_state,size)){
        fprintf(stderr,"[som-ai] SPAWN refused: undo snapshot failed\n");return 0;
    }
    undo_ready=1;
    uint8_t *from=ram+SOM_MONSTER_BASE+(unsigned)source*SOM_OBJECT_SIZE;
    uint8_t *to=ram+SOM_MONSTER_BASE+(unsigned)target*SOM_OBJECT_SIZE;
    memcpy(to,from,SOM_OBJECT_SIZE);
    uint16_t x=(uint16_t)(to[2]|to[3]<<8);x=(uint16_t)(x+32u);
    to[2]=(uint8_t)x;to[3]=(uint8_t)(x>>8);
    fprintf(stderr,"[som-ai] SPAWN copied monster id %u from 7E%04X to 7E%04X; undo with undo_spawn\n",
        (unsigned)from[0x180],SOM_MONSTER_BASE+(unsigned)source*SOM_OBJECT_SIZE,
        SOM_MONSTER_BASE+(unsigned)target*SOM_OBJECT_SIZE);
    return 1;
}
/* Living world (EXPERIMENTAL, opt-in): one native NPC walk per command file
 * write. Refused unless the ROM routines were verified at load, the enable
 * marker exists and som_npc_walk_refusal() accepts the live state. */
static const char *npc_command_file=SOM_NPC_COMMAND_FILE;
static const char *npc_enable_file=SOM_NPC_ENABLE_FILE;
static int npc_rom_verified;
static unsigned long npc_last_walk_frame;
static int npc_have_walked;
#define SOM_NPC_MIN_GAP_FRAMES 8u
static int som_npc_walk_command(const char *line){
    SomNpcWalk walk;
    if(!som_npc_parse_walk(line,&walk)){
        fprintf(stderr,"[som-ai] NPC refused: malformed command\n");return 0;
    }
    if(!npc_rom_verified){fprintf(stderr,"[som-ai] NPC refused: ROM routines not verified\n");return 0;}
    if(access(npc_enable_file,F_OK)!=0){fprintf(stderr,"[som-ai] NPC refused: living world not enabled\n");return 0;}
    if(npc_have_walked&&frame_number<npc_last_walk_frame+SOM_NPC_MIN_GAP_FRAMES){
        fprintf(stderr,"[som-ai] NPC refused: rate limit\n");return 0;
    }
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return 0;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    const char *refusal=som_npc_walk_refusal(ram,size,&walk);
    if(refusal){fprintf(stderr,"[som-ai] NPC refused: %s\n",refusal);return 0;}
    som_npc_apply_walk(ram,&walk);
    npc_have_walked=1;npc_last_walk_frame=frame_number;
    fprintf(stderr,"[som-ai] NPC WALK map %u slot %u kind %02X from (%u,%u) dir %u for %u frames\n",
        walk.map,walk.slot,walk.kind,walk.x,walk.y,walk.dir,walk.frames);
    return 1;
}
static void som_check_npc_command(void){
    if(!test_enabled||!game_loaded||!on_turbo||frame_number%SOM_TRIGGER_POLL_FRAMES)return;
    if(access(npc_command_file,F_OK)!=0)return;
    char line[96]={0};
    FILE *input=fopen(npc_command_file,"r");
    if(!input)return;
    int have=fgets(line,sizeof(line),input)!=NULL;
    fclose(input);
    if(unlink(npc_command_file)!=0||!have)return;
    line[strcspn(line,"\r\n")]=0;
    som_npc_walk_command(line);
}
static int som_undo_spawn(void){
    if(!undo_ready||!backend_retro_unserialize){
        fprintf(stderr,"[som-ai] UNDO refused: no spawn snapshot\n");return 0;
    }
    int ok=backend_retro_unserialize(undo_state,undo_size);
    undo_ready=0;
    fprintf(stderr,ok?"[som-ai] UNDO restored the pre-spawn snapshot\n":
                      "[som-ai] UNDO failed to restore the snapshot\n");
    return ok;
}
static void som_start_action(SomAction action){
    if(action==SOM_ACT_SNAPSHOT){som_snapshot();return;}
    active_action=action;
    action_frames=som_action_frames(action);
    armed=1;cancelled=0;completed=0;
    start_frame=frame_number;injected_queries=0;
    fprintf(stderr,"[som-ai] TRIGGERED %s at frame %lu for %u frames\n",
        som_action_name(action),start_frame,action_frames);
}
/* File commands are polled from the emulation thread.
 * A new command may replace an unfinished hold; physical input still wins. */
static void som_auto_tick(void){
    static const SomAction sequence[]={SOM_ACT_RIGHT,SOM_ACT_ATTACK,
        SOM_ACT_DOWN,SOM_ACT_ATTACK,SOM_ACT_LEFT,SOM_ACT_ATTACK,
        SOM_ACT_UP,SOM_ACT_ATTACK};
    if(!autonomous_mode)return;
    if(frame_number>=auto_deadline){
        autonomous_mode=0;
        fprintf(stderr,"[som-ai] AUTO COMPLETE after limited demo\n");
        return;
    }
    if(armed)return;
    /* Delay between actions allows attack recovery and visible stop frames. */
    if(frame_number<start_frame+action_frames+18)return;
    som_start_action(sequence[auto_step%8u]);
    auto_step++;
}
static void som_check_trigger(void){
    if(!test_enabled||!game_loaded||!on_turbo||
       frame_number%SOM_TRIGGER_POLL_FRAMES)return;
    /* A newer action file command replaces an unfinished hold, so the
     * planner can steer continuously instead of tap-wait-tap. */
    if(!armed&&trigger_file&&access(trigger_file,F_OK)==0){
        if(unlink(trigger_file)==0){som_start_action(SOM_ACT_RIGHT);return;}
        fprintf(stderr,"[som-ai] could not consume legacy trigger\n");
    }
    if(access(action_file,F_OK)!=0)return;
    char command[40]={0};
    FILE *input=fopen(action_file,"r");
    if(!input)return;
    int have=fgets(command,sizeof(command),input)!=NULL;
    fclose(input);
    if(unlink(action_file)!=0)return;
    if(!have)return;
    command[strcspn(command,"\r\n")]=0;
    if(!strcmp(command,"ai_on")){
        if(som_write_ring_ai_marker()){
            ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
            fprintf(stderr,"[som-ai] MANUAL AI START signal ready at %s\n",ring_ai_file);
        }else fprintf(stderr,"[som-ai] MANUAL AI start signal failed\n");
        return;
    }
    if(!strcmp(command,"ai_off")){
        ring_ai_enabled=0;ring_ai_pending=0;armed=0;cancelled=1;gambits.open=0;
        unlink(ring_ai_file);
        idle_started=1;                  /* stay off until the player plays */
        fprintf(stderr,"[som-ai] MANUAL AI STOP\n");
        return;
    }
    if(!strcmp(command,"auto_on")){
        autonomous_mode=1;auto_step=0;
        auto_deadline=frame_number+600; /* ten seconds maximum at 60fps */
        start_frame=frame_number>50?frame_number-50:0;action_frames=0;
        fprintf(stderr,"[som-ai] AUTO START: bounded 600-frame movement/attack demo\n");
        return;
    }
    if(!strcmp(command,"clone_enemy")){som_clone_enemy();return;}
    if(!strcmp(command,"undo_spawn")){som_undo_spawn();return;}
    if(!strcmp(command,"auto_off")){
        autonomous_mode=0;armed=0;
        fprintf(stderr,"[som-ai] AUTO STOP\n");
        return;
    }
    if(autonomous_mode){autonomous_mode=0;armed=0;}
    SomAction action=som_parse_action(command);
    if(action==SOM_ACT_NONE){
        fprintf(stderr,"[som-ai] unknown action %s\n",command);
        return;
    }
    som_start_action(action);
}

/* Show one message from the SkyLoft bridge. '|' starts the second line. */
static void som_check_message(void){
    if(!test_enabled||!game_loaded||!on_turbo||frame_number%SOM_MESSAGE_POLL_FRAMES)return;
    if(access(message_file,F_OK)!=0)return;
    char text[96]={0};
    FILE *input=fopen(message_file,"r");
    if(!input)return;
    int have=fgets(text,sizeof(text),input)!=NULL;
    fclose(input);
    unlink(message_file);
    if(have&&som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES))
        fprintf(stderr,"[som-ai] MESSAGE %s\n",text);
}
/* The hero's position on the base 256x224 screen, from read-only WRAM:
 * world position (7E:E002/E004) minus a camera that centres on the hero and
 * stops at the map edges (map size 7E:00C0/00C2). Field mode only. */
static int som_hero_screen(int *x,int *y){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return 0;
    if(ram[0xE000]!=1||ram[0x00E4]!=1||ram[0x00E6]!=0x0F||ram[0x00D0]!=0)return 0;
    long w=ram[0xC0]|ram[0xC1]<<8,h=ram[0xC2]|ram[0xC3]<<8;
    long hx=ram[0xE002]|ram[0xE003]<<8,hy=ram[0xE004]|ram[0xE005]<<8;
    if(w<256||h<224||w>4096||h>4096)return 0;
    hx%=w;hy%=h;
    long cam_x=hx-128,cam_y=hy-112;
    if(cam_x<0)cam_x=0;
    if(cam_x>w-256)cam_x=w-256;
    if(cam_y<0)cam_y=0;
    if(cam_y>h-224)cam_y=h-224;
    *x=(int)(hx-cam_x);*y=(int)(hy-cam_y);
    return 1;
}
static void som_video_refresh(const void *data,unsigned width,unsigned height,size_t pitch){
    if(!frontend_video)return;
    /* NULL repeats the previous (already decorated) frame; (void*)-1 is a
     * hardware-rendered frame that has no CPU pixels to draw on. */
    const int menu=gambits.open&&ring_ai_enabled;
    if((!overlay.frames_left&&!menu)||!data||data==RETRO_HW_FRAME_BUFFER_VALID||
       (pixel_format!=0&&pixel_format!=1&&pixel_format!=2)){
        frontend_video(data,width,height,pitch);
        return;
    }
    size_t size=(size_t)height*pitch;
    if(size>overlay_frame_size){
        uint8_t *grown=realloc(overlay_frame,size);
        if(!grown){frontend_video(data,width,height,pitch);return;}
        overlay_frame=grown;overlay_frame_size=size;
    }
    memcpy(overlay_frame,data,size);
    int hero_x=-1,hero_y=-1;
    if(overlay.speech_bubble)som_hero_screen(&hero_x,&hero_y);
    if(overlay.frames_left){
        som_overlay_draw(overlay_frame,width,height,pitch,pixel_format,&overlay,hero_x,hero_y);
        overlay.frames_left--;
    }
    if(menu)som_gambit_draw(overlay_frame,width,height,pitch,pixel_format,&gambits);
    frontend_video(overlay_frame,width,height,pitch);
}
static bool som_environment(unsigned cmd,void *data){
    bool handled=environment_callback?environment_callback(cmd,data):false;
    if(handled&&cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT&&data)
        pixel_format=(int)*(const enum retro_pixel_format*)data;
    return handled;
}

static void load_backend(void){
    if(backend)return;
    const char *path=getenv("FLXTR_AI_BASE_CORE");
    if(!path||!*path)path=SOM_PROXY_BASE_CORE;
    /* Never allow loading this proxy into itself. */
    if(strstr(path,"som_ai_proxy")){fputs("[som-ai] recursive core path refused\n",stderr);abort();}
    backend=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!backend){fprintf(stderr,"[som-ai] dlopen failed: %s\n",dlerror());abort();}
#define LOAD_VIDEO() do { \
    *(void**)(&backend_set_video_refresh)=dlsym(backend,"retro_set_video_refresh"); \
    if(!backend_set_video_refresh){fputs("[som-ai] backend lacks retro_set_video_refresh\n",stderr);abort();} \
} while(0)
#define LOAD(name) do { \
    *(void**)(&backend_##name)=dlsym(backend,#name); \
    if(!backend_##name){fprintf(stderr,"[som-ai] backend lacks %s\n",#name);abort();} \
} while(0)
    LOAD_VIDEO();LOAD(retro_set_audio_sample);
    LOAD(retro_set_audio_sample_batch);LOAD(retro_set_controller_port_device);
    LOAD(retro_reset);LOAD(retro_serialize_size);LOAD(retro_serialize);
    LOAD(retro_unserialize);LOAD(retro_cheat_reset);LOAD(retro_cheat_set);
    LOAD(retro_load_game_special);LOAD(retro_get_region);
    LOAD(retro_get_memory_data);LOAD(retro_get_memory_size);
    LOAD(retro_api_version);LOAD(retro_get_system_info);LOAD(retro_get_system_av_info);
#undef LOAD
#undef LOAD_VIDEO
#define LOAD_DIRECT(name) do { \
    *(void**)(&backend_##name)=dlsym(backend,"retro_"#name); \
    if(!backend_##name){fprintf(stderr,"[som-ai] missing retro_%s\n",#name);abort();} \
} while(0)
    LOAD_DIRECT(set_environment);LOAD_DIRECT(set_input_poll);LOAD_DIRECT(set_input_state);
    LOAD_DIRECT(init);LOAD_DIRECT(deinit);LOAD_DIRECT(load_game);LOAD_DIRECT(unload_game);LOAD_DIRECT(run);
#undef LOAD_DIRECT
}
RETRO_API void retro_set_environment(retro_environment_t cb){
    load_backend();environment_callback=cb;backend_set_environment(som_environment);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb){
    load_backend();frontend_video=cb;backend_set_video_refresh(som_video_refresh);
}
RETRO_API void retro_set_input_poll(retro_input_poll_t cb){
    load_backend();physical_poll=cb;backend_set_input_poll(som_input_poll);
}
RETRO_API void retro_set_input_state(retro_input_state_t cb){
    load_backend();physical_state=cb;backend_set_input_state(som_input_state);
}
RETRO_API void retro_init(void){
    load_backend();backend_init();
}
RETRO_API void retro_deinit(void){
    if(backend_deinit)backend_deinit();
    /* RetroArch may call get_system_info again; do not unload an active
     * backend out from under its borrowed system-info string pointers. */
    game_loaded=0;armed=0;
    unlink(wram_file);unlink(wram_temp_file);unlink(ring_ai_file);
    (void)environment_callback;
}
/* EXPERIMENTAL map-record patches, applied to the proxy's in-memory copy of
 * the ROM before the core sees it (the .smc file is never modified). Lines:
 *   patch <offset> <expected> <new>     (hex; offset without a copier header)
 * A line applies only if the ROM byte equals <expected>, so a different ROM
 * build is never altered. Used to turn a map's monster record into a Rabite,
 * which the game then spawns natively (drawing, AI, death and drops). */
#ifndef SOM_ROM_PATCH_FILE
#define SOM_ROM_PATCH_FILE "/home/apps/retroarch/cores/experimental/som_ai_rom_patch.txt"
#endif
static const char *rom_patch_file=SOM_ROM_PATCH_FILE;
static uint8_t *patched_rom;
static size_t som_apply_rom_patches(uint8_t *rom,size_t size){
    FILE *input=fopen(rom_patch_file,"r");
    if(!input)return 0;
    size_t header=size%0x8000u==512u?512u:0u,applied=0;
    char line[96];
    while(fgets(line,sizeof(line),input)){
        unsigned long offset;unsigned expected,value;
        if(sscanf(line,"patch %lx %x %x",&offset,&expected,&value)!=3)continue;
        if(expected>0xFFu||value>0xFFu||offset+header>=size){
            fprintf(stderr,"[som-ai] ROM PATCH skipped (out of range): %s",line);continue;
        }
        uint8_t *byte=rom+header+offset;
        if(*byte!=expected){
            fprintf(stderr,"[som-ai] ROM PATCH skipped at %06lX: found %02X, expected %02X\n",
                offset,(unsigned)*byte,expected);continue;
        }
        *byte=(uint8_t)value;applied++;
        fprintf(stderr,"[som-ai] ROM PATCH %06lX %02X -> %02X (memory only)\n",
            offset,expected,value);
    }
    fclose(input);
    return applied;
}
RETRO_API bool retro_load_game(const struct retro_game_info *game){
    load_backend();
    frame_number=0;cancelled=0;completed=0;armed=0;on_turbo=0;
    active_action=SOM_ACT_NONE;action_frames=0;som_have_previous_menu=0;
    autonomous_mode=0;auto_step=0;auto_deadline=0;
    last_physical_buttons=0;last_attack_frame=0;combo_count=0;
    test_enabled=0;trigger_file=NULL;start_frame=0;injected_queries=0;
    wram_export_error_logged=0;
    memset(&overlay,0,sizeof(overlay));
    undo_ready=0;
    ring_ai_selector_open=0;ring_ai_pending=0;ring_ai_enabled=0;
    ring_ai_initial_cell=0;ring_ai_grace_until=0;
    switch_holding=switch_spoiled=0;switch_hold_start=0;
    som_gambit_load();
    last_input_frame=frame_number;idle_started=0;
    const char *path=game?game->path:NULL;
    on_turbo=path&&strstr(path,"Secret of Mana, Turbo.smc")!=NULL;
    free(patched_rom);patched_rom=NULL;
    struct retro_game_info patched_game;
    const struct retro_game_info *load_info=game;
    npc_have_walked=0;npc_last_walk_frame=0;
    npc_rom_verified=on_turbo&&game&&game->data&&
        som_npc_rom_verified((const uint8_t*)game->data,game->size);
    if(on_turbo)fprintf(stderr,"[som-ai] living-world NPC routines %s\n",
        npc_rom_verified?"verified":"NOT verified; npc_walk disabled");
    if(on_turbo&&game&&game->data&&game->size&&(patched_rom=malloc(game->size))){
        memcpy(patched_rom,game->data,game->size);
        if(som_apply_rom_patches(patched_rom,game->size)){
            patched_game=*game;patched_game.data=patched_rom;load_info=&patched_game;
        }
    }
    game_loaded=backend_load_game(load_info);
    /* Selecting this experimental core is explicit opt-in. No injection
     * occurs until the separate one-shot trigger is consumed. A
     * launcher-provided environment variable is not reliable across the
     * greenlink -> greenlink-catalog -> RetroArch process boundary. */
    test_enabled=game_loaded&&on_turbo;
    if(test_enabled){
        trigger_file=getenv("FLXTR_SOM_AI_TRIGGER_FILE");
        if(!trigger_file || !*trigger_file) trigger_file=SOM_TRIGGER_DEFAULT;
        /* Stale files from prior sessions must never cause a new takeover. */
        if(unlink(trigger_file)!=0 && access(trigger_file,F_OK)==0){
            fprintf(stderr,"[som-ai] cannot clear stale trigger; disabled\n");
            test_enabled=0;
        }
        if(test_enabled){
            unlink(action_file);unlink(message_file);unlink(record_marker);
            unlink(npc_command_file);
            recording=0;
            unlink(wram_file);unlink(wram_temp_file);unlink(ring_ai_file);
            fprintf(stderr,"[som-ai] READY: legacy %s; actions via %s; read-only WRAM via %s\n",
                trigger_file,action_file,wram_file);
        }
    }else fprintf(stderr,"[som-ai] disabled (Turbo filename missing or game failed to load)\n");
    return game_loaded;
}
RETRO_API void retro_unload_game(void){
    armed=0;autonomous_mode=0;game_loaded=0;
    ring_ai_enabled=0;ring_ai_pending=0;
    unlink(wram_file);unlink(wram_temp_file);unlink(ring_ai_file);unlink(message_file);
    unlink(npc_command_file);
    memset(&overlay,0,sizeof(overlay));
    undo_ready=0;
    if(backend_unload_game)backend_unload_game();
}
RETRO_API void retro_run(void){
    if(!backend_run)load_backend();
    som_check_trigger();
    som_check_npc_command();
    som_check_message();
    if(test_enabled&&on_turbo&&frame_number%SOM_MESSAGE_POLL_FRAMES==0u)
        recording=access(record_marker,F_OK)==0;
    som_auto_tick();
    if(armed&&!cancelled&&!completed&&game_loaded &&
       frame_number==start_frame+action_frames){
        completed=1;armed=0;
        fprintf(stderr,"[som-ai] COMPLETE %s at frame %lu; button sampled %lu times\n",
                som_action_name(active_action),frame_number,injected_queries);
    }
    backend_run();
    if(test_enabled&&on_turbo)som_ring_ai_tick();
    som_idle_tick();
    if(test_enabled&&on_turbo&&frame_number%SOM_WRAM_EXPORT_FRAMES==0u &&
       !som_export_wram()&&!wram_export_error_logged){
        wram_export_error_logged=1;
        fprintf(stderr,"[som-ai] WRAM export unavailable; live observer disabled\n");
    }
    frame_number++;
}
