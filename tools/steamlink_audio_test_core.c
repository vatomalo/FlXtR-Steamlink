/* ROM-free Libretro fixture: tests both standard audio callbacks in RetroArch.
 * It never opens SDL or controls the frontend's audio clock. */
#include "../src/vendor/libretro.h"
#include <math.h>
#include <string.h>
static retro_video_refresh_t video;
static retro_audio_sample_t sample;
static retro_audio_sample_batch_t batch;
static retro_input_poll_t poll_input;
static retro_input_state_t input_state;
static retro_environment_t environment;
static uint64_t frame;
static int av_sync,previous_button;
void retro_set_environment(retro_environment_t cb){
    static const struct retro_variable options[]={
        {"flxtr_audio_test_mode","Diagnostic mode; stereo|av-sync"},{NULL,NULL}
    };
    bool yes=true;environment=cb;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,&yes);
    cb(RETRO_ENVIRONMENT_SET_VARIABLES,(void *)options);
}
void retro_set_video_refresh(retro_video_refresh_t cb){video=cb;}
void retro_set_audio_sample(retro_audio_sample_t cb){sample=cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){batch=cb;}
void retro_set_input_poll(retro_input_poll_t cb){poll_input=cb;}
void retro_set_input_state(retro_input_state_t cb){input_state=cb;}
void retro_set_controller_port_device(unsigned port,unsigned device){(void)port;(void)device;}
unsigned retro_api_version(void){return RETRO_API_VERSION;}
void retro_init(void){frame=0;av_sync=previous_button=0;}
void retro_deinit(void){}
void retro_get_system_info(struct retro_system_info *info){
    *info=(struct retro_system_info){"FlXtR audio diagnostic","1","",false,false};
}
void retro_get_system_av_info(struct retro_system_av_info *info){
    *info=(struct retro_system_av_info){{160,144,160,144,4.0f/3.0f},{60,48000}};
}
bool retro_load_game(const struct retro_game_info *game){
    struct retro_variable option={"flxtr_audio_test_mode",NULL};
    (void)game;frame=0;previous_button=0;
    av_sync=environment&&environment(RETRO_ENVIRONMENT_GET_VARIABLE,&option)&&
        option.value&&!strcmp(option.value,"av-sync");
    return true;
}
bool retro_load_game_special(unsigned type,const struct retro_game_info *game,size_t count){(void)type;(void)game;(void)count;return false;}
void retro_unload_game(void){}
void retro_reset(void){frame=0;}
void retro_run(void){
    int16_t pcm[800*2];static uint16_t pixels[160*144];
    int right=(int)(frame/60)%2;
    int button=0,pulse;
    if(poll_input)poll_input();
    if(input_state){
        const unsigned buttons[]={RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_B,
            RETRO_DEVICE_ID_JOYPAD_X,RETRO_DEVICE_ID_JOYPAD_Y};
        for(unsigned i=0;i<4;i++)button|=input_state(0,RETRO_DEVICE_JOYPAD,0,buttons[i])!=0;
    }
    pulse=frame%60==0||(button&&!previous_button);previous_button=button;
    if(av_sync)for(unsigned i=0;i<160*144;i++)pixels[i]=pulse?0x7fff:0;
    for(unsigned i=0;i<800;i++){
        double t=(double)(frame*800+i)/48000;
        int16_t value=(int16_t)(sin(6.283185307179586*(right?660:440)*t)*6000);
        pcm[2*i]=right?0:value;pcm[2*i+1]=right?value:0;
        if(av_sync)pcm[2*i]=pcm[2*i+1]=pulse?value:0;
    }
    if(right&&sample){for(unsigned i=0;i<800;i++)sample(pcm[2*i],pcm[2*i+1]);}
    else if(batch)batch(pcm,800);
    if(video)video(pixels,160,144,160*sizeof(*pixels));
    frame++;
}
size_t retro_serialize_size(void){return sizeof(frame);}
bool retro_serialize(void *data,size_t size){if(size<sizeof(frame))return false;memcpy(data,&frame,sizeof(frame));return true;}
bool retro_unserialize(const void *data,size_t size){if(size<sizeof(frame))return false;memcpy(&frame,data,sizeof(frame));return true;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned index,bool enabled,const char *code){(void)index;(void)enabled;(void)code;}
unsigned retro_get_region(void){return RETRO_REGION_NTSC;}
void *retro_get_memory_data(unsigned id){(void)id;return NULL;}
size_t retro_get_memory_size(unsigned id){(void)id;return 0;}
