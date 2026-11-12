/*
 * FlXtR Secret of Mana Turbo opt-in input proxy.
 * A Libretro core wrapper; loads the installed Snes9x 2005 Plus as a library.
 * It does NOT patch emulator code, ROM, save data, or system RAM.
 *
 * Exact usage and safety caveats: docs/secret-of-mana-ai-input-bridge.md
 */
#define _POSIX_C_SOURCE 200809L
#include "vendor/libretro.h"
#include "som_ai_actions.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#ifndef SOM_PROXY_BASE_CORE
#define SOM_PROXY_BASE_CORE "/home/apps/retroarch/cores/snes9x2005_plus_libretro.so"
#endif
#define SOM_TRIGGER_POLL_FRAMES 6u
#define SOM_TRIGGER_DEFAULT "/tmp/flxtr-som-ai-go"
#define SOM_HOLD_FRAMES 30u

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
#define SOM_PLAN_MAX 12u
static SomAction plan_actions[SOM_PLAN_MAX];
static unsigned plan_count,plan_next;
static int plan_running;
static unsigned long next_plan_frame;
static const char *trigger_file;
static const char *action_file="/tmp/flxtr-som-ai-action";
#define SOM_MENU_SCAN_START 0x1800u
#define SOM_MENU_SCAN_LENGTH 0x0200u
static uint8_t som_previous_menu[SOM_MENU_SCAN_LENGTH];
static int som_have_previous_menu;
static retro_environment_t environment_callback;
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

FORWARD_VOID(retro_set_video_refresh,(retro_video_refresh_t cb),(cb))
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
    if(port==0 && (device & RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD &&
       index==0 && id<16 && armed && !cancelled && !completed && on_turbo){
        if(actual){cancelled=1;armed=0;autonomous_mode=0;plan_running=0;fprintf(stderr,"[som-ai] cancelled: physical input at frame %lu\n",frame_number);return actual;}
        if(frame_number>=start_frame &&
           frame_number<start_frame+action_frames &&
           id==som_action_button(active_action)){
            injected_queries++;
            return 1;
        }
    }
    return actual;
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
        last_physical_buttons=current;
        if(autonomous_mode && current){
            autonomous_mode=0;plan_running=0;armed=0;cancelled=1;
            fprintf(stderr,"[som-ai] AUTO STOP physical controller takeover\n");
        }
    }
    /* Check all physical buttons before injecting input, regardless of which
     * individual IDs the emulator requests in this frame. */
    if(armed&&!cancelled&&!completed&&on_turbo&&physical_state)
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
 * Only allow another action after the previous action is completed. */
static void som_auto_tick(void){
    static const SomAction sequence[]={SOM_ACT_RIGHT,SOM_ACT_ATTACK,
        SOM_ACT_DOWN,SOM_ACT_ATTACK,SOM_ACT_LEFT,SOM_ACT_ATTACK,
        SOM_ACT_UP,SOM_ACT_ATTACK};
    if(plan_running){
        if(armed || frame_number<next_plan_frame)return;
        if(plan_next>=plan_count){
            plan_running=0;
            fprintf(stderr,"[som-ai] PLAN COMPLETE count=%u frame=%lu\n",plan_count,frame_number);
            return;
        }
        SomAction next=plan_actions[plan_next++];
        som_start_action(next);
        next_plan_frame=frame_number+som_action_frames(next)+
            (next==SOM_ACT_ATTACK?30u:9u);
        return;
    }
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
    if(!test_enabled||!game_loaded||!on_turbo||armed||
       frame_number%SOM_TRIGGER_POLL_FRAMES)return;
    if(trigger_file&&access(trigger_file,F_OK)==0){
        if(unlink(trigger_file)==0){som_start_action(SOM_ACT_RIGHT);return;}
        fprintf(stderr,"[som-ai] could not consume legacy trigger\n");
    }
    if(access(action_file,F_OK)!=0)return;
    char command[192]={0};
    FILE *input=fopen(action_file,"r");
    if(!input)return;
    int have=fgets(command,sizeof(command),input)!=NULL;
    fclose(input);
    if(unlink(action_file)!=0)return;
    if(!have)return;
    command[strcspn(command,"\r\n")]=0;
    if(!strncmp(command,"plan ",5)){
        SomAction incoming[SOM_PLAN_MAX];
        unsigned count=0;
        char *state=NULL;
        char *token=strtok_r(command+5," ",&state);
        while(token){
            if(count>=SOM_PLAN_MAX)break;
            SomAction action=som_parse_action(token);
            if(action!=SOM_ACT_RIGHT && action!=SOM_ACT_LEFT &&
               action!=SOM_ACT_UP && action!=SOM_ACT_DOWN &&
               action!=SOM_ACT_ATTACK)break;
            incoming[count++]=action;
            token=strtok_r(NULL," ",&state);
        }
        if(!count||token){
            fprintf(stderr,"[som-ai] PLAN REJECTED: requires 1..%u movement/attack actions\n",SOM_PLAN_MAX);
            return;
        }
        memcpy(plan_actions,incoming,count*sizeof(incoming[0]));
        plan_count=count;plan_next=0;plan_running=1;autonomous_mode=0;
        next_plan_frame=frame_number;
        fprintf(stderr,"[som-ai] PLAN START count=%u frame=%lu\n",count,frame_number);
        return;
    }
    if(!strcmp(command,"auto_on")){
        plan_running=0;autonomous_mode=1;auto_step=0;
        auto_deadline=frame_number+600; /* ten seconds maximum at 60fps */
        start_frame=frame_number>50?frame_number-50:0;action_frames=0;
        fprintf(stderr,"[som-ai] AUTO START: bounded 600-frame movement/attack demo\n");
        return;
    }
    if(!strcmp(command,"auto_off")){
        autonomous_mode=0;plan_running=0;armed=0;
        fprintf(stderr,"[som-ai] AUTO STOP\n");
        return;
    }
    if(autonomous_mode||plan_running){autonomous_mode=0;plan_running=0;armed=0;}
    SomAction action=som_parse_action(command);
    if(action==SOM_ACT_NONE){
        fprintf(stderr,"[som-ai] unknown action %s\n",command);
        return;
    }
    som_start_action(action);
}

static void load_backend(void){
    if(backend)return;
    const char *path=getenv("FLXTR_AI_BASE_CORE");
    if(!path||!*path)path=SOM_PROXY_BASE_CORE;
    /* Never allow loading this proxy into itself. */
    if(strstr(path,"som_ai_proxy")){fputs("[som-ai] recursive core path refused\n",stderr);abort();}
    backend=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!backend){fprintf(stderr,"[som-ai] dlopen failed: %s\n",dlerror());abort();}
#define LOAD(name) do { \
    *(void**)(&backend_##name)=dlsym(backend,#name); \
    if(!backend_##name){fprintf(stderr,"[som-ai] backend lacks %s\n",#name);abort();} \
} while(0)
    LOAD(retro_set_video_refresh);LOAD(retro_set_audio_sample);
    LOAD(retro_set_audio_sample_batch);LOAD(retro_set_controller_port_device);
    LOAD(retro_reset);LOAD(retro_serialize_size);LOAD(retro_serialize);
    LOAD(retro_unserialize);LOAD(retro_cheat_reset);LOAD(retro_cheat_set);
    LOAD(retro_load_game_special);LOAD(retro_get_region);
    LOAD(retro_get_memory_data);LOAD(retro_get_memory_size);
    LOAD(retro_api_version);LOAD(retro_get_system_info);LOAD(retro_get_system_av_info);
#undef LOAD
#define LOAD_DIRECT(name) do { \
    *(void**)(&backend_##name)=dlsym(backend,"retro_"#name); \
    if(!backend_##name){fprintf(stderr,"[som-ai] missing retro_%s\n",#name);abort();} \
} while(0)
    LOAD_DIRECT(set_environment);LOAD_DIRECT(set_input_poll);LOAD_DIRECT(set_input_state);
    LOAD_DIRECT(init);LOAD_DIRECT(deinit);LOAD_DIRECT(load_game);LOAD_DIRECT(unload_game);LOAD_DIRECT(run);
#undef LOAD_DIRECT
}
RETRO_API void retro_set_environment(retro_environment_t cb){
    load_backend();environment_callback=cb;backend_set_environment(cb);
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
    (void)environment_callback;
}
RETRO_API bool retro_load_game(const struct retro_game_info *game){
    load_backend();
    frame_number=0;cancelled=0;completed=0;armed=0;on_turbo=0;
    active_action=SOM_ACT_NONE;action_frames=0;som_have_previous_menu=0;
    autonomous_mode=0;auto_step=0;auto_deadline=0;
    plan_running=0;plan_count=0;plan_next=0;next_plan_frame=0;
    last_physical_buttons=0;last_attack_frame=0;combo_count=0;
    test_enabled=0;trigger_file=NULL;start_frame=0;injected_queries=0;
    game_loaded=backend_load_game(game);
    const char *path=game?game->path:NULL;
    on_turbo=path&&strstr(path,"Secret of Mana, Turbo.smc")!=NULL;
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
            unlink(action_file);
            fprintf(stderr,"[som-ai] READY: legacy %s; actions via %s\n",trigger_file,action_file);
        }
    }else fprintf(stderr,"[som-ai] disabled (Turbo filename missing or game failed to load)\n");
    return game_loaded;
}
RETRO_API void retro_unload_game(void){
    armed=0;autonomous_mode=0;plan_running=0;game_loaded=0;
    if(backend_unload_game)backend_unload_game();
}
RETRO_API void retro_run(void){
    if(!backend_run)load_backend();
    som_check_trigger();
    som_auto_tick();
    if(armed&&!cancelled&&!completed&&game_loaded &&
       frame_number==start_frame+action_frames){
        completed=1;armed=0;
        fprintf(stderr,"[som-ai] COMPLETE %s at frame %lu; button sampled %lu times\n",
                som_action_name(active_action),frame_number,injected_queries);
    }
    backend_run();
    frame_number++;
}
