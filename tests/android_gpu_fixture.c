#include <libretro.h>
#include <GLES2/gl2.h>
#include <string.h>
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static retro_input_state_t input;
static unsigned state;
static unsigned char memory[64];
static void reset_context(void){glBindFramebuffer(GL_FRAMEBUFFER,0);}
void retro_set_environment(retro_environment_t callback){environment=callback;}
void retro_set_video_refresh(retro_video_refresh_t callback){video=callback;}
void retro_set_audio_sample(retro_audio_sample_t callback){(void)callback;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback){audio=callback;}
void retro_set_input_poll(retro_input_poll_t callback){poll=callback;}
void retro_set_input_state(retro_input_state_t callback){input=callback;}
void retro_init(void){}
void retro_deinit(void){}
void retro_get_system_info(struct retro_system_info *info){memset(info,0,sizeof(*info));info->library_name="FlXtR GLES fixture";info->library_version="1";info->need_fullpath=true;}
void retro_get_system_av_info(struct retro_system_av_info *info){memset(info,0,sizeof(*info));info->geometry.base_width=info->geometry.max_width=64;info->geometry.base_height=info->geometry.max_height=64;info->geometry.aspect_ratio=1;info->timing.fps=60;info->timing.sample_rate=44100;}
void retro_set_controller_port_device(unsigned port,unsigned device){(void)port;(void)device;}
bool retro_load_game(const struct retro_game_info *game){(void)game;struct retro_hw_render_callback request={0};request.context_type=RETRO_HW_CONTEXT_OPENGLES2;request.context_reset=reset_context;return environment(RETRO_ENVIRONMENT_SET_HW_RENDER,&request);}
void retro_unload_game(void){}
void retro_run(void){int16_t samples[512]={0};poll();glViewport(0,0,64,64);glClearColor(input(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X)>0?1:0,0.5f,0.25f,1);glClear(GL_COLOR_BUFFER_BIT);video(RETRO_HW_FRAME_BUFFER_VALID,64,64,0);audio(samples,256);state++;}
void retro_reset(void){state=0;}
size_t retro_serialize_size(void){return sizeof(state);}
bool retro_serialize(void *data,size_t size){if(size!=sizeof(state))return false;memcpy(data,&state,size);return true;}
bool retro_unserialize(const void *data,size_t size){if(size!=sizeof(state))return false;memcpy(&state,data,size);return true;}
void *retro_get_memory_data(unsigned id){return id==RETRO_MEMORY_SAVE_RAM?memory:0;}
size_t retro_get_memory_size(unsigned id){return id==RETRO_MEMORY_SAVE_RAM?sizeof(memory):0;}
