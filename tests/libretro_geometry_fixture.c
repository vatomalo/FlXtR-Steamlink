#include "../src/vendor/libretro.h"
#include <stdlib.h>
#include <string.h>
static retro_environment_t environment;
static int large,rotated;
void retro_set_environment(retro_environment_t cb){environment=cb;}
void retro_set_video_refresh(retro_video_refresh_t cb){(void)cb;}
void retro_set_audio_sample(retro_audio_sample_t cb){(void)cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){(void)cb;}
void retro_set_input_poll(retro_input_poll_t cb){(void)cb;}
void retro_set_input_state(retro_input_state_t cb){(void)cb;}
unsigned retro_api_version(void){return RETRO_API_VERSION;}
void retro_init(void){large=rotated=0;}
void retro_deinit(void){}
void retro_get_system_info(struct retro_system_info *info){
 *info=(struct retro_system_info){"Fixture","1","smc|sfc|nes",true,false};
}
bool retro_load_game(const struct retro_game_info *game){
 large=strstr(game->path,"Large")!=NULL;rotated=strstr(game->path,"Rotated")!=NULL;
 unsigned rotate=rotated?1:0;environment(RETRO_ENVIRONMENT_SET_ROTATION,&rotate);return true;
}
void retro_get_system_av_info(struct retro_system_av_info *info){
 *info=(struct retro_system_av_info){{large?512:256,large?448:224,512,448,4.0f/3.0f},{60,48000}};
}
void retro_unload_game(void){}
