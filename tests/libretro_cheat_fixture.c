/* Headless on-device integration test: report cheat callbacks, never use hardware. */
#include "../src/vendor/libretro.h"
#include <stdio.h>
#include <stdlib.h>
static retro_video_refresh_t video;
void retro_set_environment(retro_environment_t cb){(void)cb;}
void retro_set_video_refresh(retro_video_refresh_t cb){video=cb;}
void retro_set_audio_sample(retro_audio_sample_t cb){(void)cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){(void)cb;}
void retro_set_input_poll(retro_input_poll_t cb){(void)cb;}
void retro_set_input_state(retro_input_state_t cb){(void)cb;}
void retro_init(void){}
void retro_deinit(void){}
unsigned retro_api_version(void){return RETRO_API_VERSION;}
void retro_get_system_info(struct retro_system_info *info){*info=(struct retro_system_info){"FlXtR Cheat Fixture","1","nes",true,false};}
void retro_get_system_av_info(struct retro_system_av_info *info){*info=(struct retro_system_av_info){{16,16,16,16,1.0},{60.0,44100.0}};}
void retro_set_controller_port_device(unsigned port,unsigned device){(void)port;(void)device;}
void retro_reset(void){}
void retro_run(void){static unsigned short pixels[16*16];if(video)video(pixels,16,16,32);}
bool retro_load_game(const struct retro_game_info *game){return game!=NULL;}
bool retro_load_game_special(unsigned type,const struct retro_game_info *games,size_t count){(void)type;(void)games;(void)count;return false;}
void retro_unload_game(void){}
unsigned retro_get_region(void){return RETRO_REGION_NTSC;}
size_t retro_serialize_size(void){return 0;}
bool retro_serialize(void *data,size_t size){(void)data;(void)size;return false;}
bool retro_unserialize(const void *data,size_t size){(void)data;(void)size;return false;}
void *retro_get_memory_data(unsigned id){(void)id;return NULL;}
size_t retro_get_memory_size(unsigned id){(void)id;return 0;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned index,bool enabled,const char *code){
    const char *trace=getenv("FLXTR_CHEAT_TRACE");if(!trace)return;
    FILE *f=fopen(trace,"a");if(f){fprintf(f,"%u %d %s\n",index,enabled,code);fclose(f);}
}
