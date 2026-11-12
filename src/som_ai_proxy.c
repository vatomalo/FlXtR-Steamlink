/*
 * FlXtR Secret of Mana Turbo opt-in input proxy.
 * A Libretro core wrapper; loads the installed Snes9x 2005 Plus as a library.
 * It does NOT patch emulator code, ROM, save data, or system RAM.
 *
 * Exact usage and safety caveats: docs/secret-of-mana-ai-input-bridge.md
 */
#define _POSIX_C_SOURCE 200809L
#include "vendor/libretro.h"
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
static unsigned long start_frame, injected_right_queries;
static const char *trigger_file;
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
        if(actual){cancelled=1;fprintf(stderr,"[som-ai] cancelled: physical input at frame %lu\n",frame_number);return actual;}
        if(frame_number>=start_frame &&
           frame_number<start_frame+SOM_HOLD_FRAMES &&
           id==RETRO_DEVICE_ID_JOYPAD_RIGHT){
            injected_right_queries++;
            return 1;
        }
    }
    return actual;
}
static void som_input_poll(void){
    if(physical_poll)physical_poll();
    /* Check all physical buttons before injecting input, regardless of which
     * individual IDs the emulator requests in this frame. */
    if(armed&&!cancelled&&!completed&&on_turbo&&physical_state)
        for(unsigned id=0;id<16;id++)
            if(physical_state(0,RETRO_DEVICE_JOYPAD,0,id)){
                cancelled=1;
                fprintf(stderr,"[som-ai] physical controller override on frame %lu\n",frame_number);
                break;
            }
}

/* Trigger is a one-shot file created via SSH *after* gameplay begins.
 * Poll sparingly on the emulation thread; never read network or run commands. */
static void som_check_trigger(void){
    if(!test_enabled || !game_loaded || !on_turbo || armed ||
       !trigger_file || frame_number%SOM_TRIGGER_POLL_FRAMES) return;
    if(access(trigger_file,F_OK)!=0) return;
    if(unlink(trigger_file)!=0){
        fprintf(stderr,"[som-ai] refusing trigger that could not be consumed: %s\n",trigger_file);
        return;
    }
    armed=1;cancelled=0;completed=0;
    start_frame=frame_number;
    injected_right_queries=0;
    fprintf(stderr,"[som-ai] TRIGGERED at frame %lu; injecting RIGHT for %u frames\n",
            start_frame,SOM_HOLD_FRAMES);
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
    test_enabled=0;trigger_file=NULL;start_frame=0;injected_right_queries=0;
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
        if(test_enabled)fprintf(stderr,"[som-ai] READY: create %s during gameplay to inject 30 frames of RIGHT\n",trigger_file);
    }else fprintf(stderr,"[som-ai] disabled (Turbo filename missing or game failed to load)\n");
    return game_loaded;
}
RETRO_API void retro_unload_game(void){
    armed=0;game_loaded=0;
    if(backend_unload_game)backend_unload_game();
}
RETRO_API void retro_run(void){
    if(!backend_run)load_backend();
    som_check_trigger();
    if(armed&&!cancelled&&!completed&&game_loaded &&
       frame_number==start_frame+SOM_HOLD_FRAMES){
        completed=1;
        fprintf(stderr,"[som-ai] COMPLETE at frame %lu; RIGHT sampled %lu times\n",
                frame_number,injected_right_queries);
    }
    backend_run();
    frame_number++;
}
