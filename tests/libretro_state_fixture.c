#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <string.h>
#define retro_run original_run
#define retro_get_system_info original_info
#define retro_serialize_size original_serialize_size
#define retro_serialize original_serialize
#define retro_unserialize original_unserialize
#include "libretro_cheat_fixture.c"
#undef retro_run
#undef retro_get_system_info
#undef retro_serialize_size
#undef retro_serialize
#undef retro_unserialize
static unsigned state_counter;
void retro_get_system_info(struct retro_system_info *info){*info=(struct retro_system_info){"FlXtR State Fixture","1","nes",true,false};}
void retro_run(void){
    struct timespec delay={0,10000000};nanosleep(&delay,NULL);state_counter++;
    original_run();
}
size_t retro_serialize_size(void){return 16384;}
bool retro_serialize(void *data,size_t size){
    if(size!=16384)return false;
    memset(data,0,size);memcpy(data,"FLXS",4);memcpy((char*)data+4,&state_counter,sizeof(state_counter));return true;
}
bool retro_unserialize(const void *data,size_t size){
    if(size!=16384||memcmp(data,"FLXS",4))return false;
    memcpy(&state_counter,(const char*)data+4,sizeof(state_counter));
    const char *trace=getenv("FLXTR_STATE_TRACE");if(trace){FILE *f=fopen(trace,"a");if(f){fprintf(f,"LOADED %u\n",state_counter);fclose(f);}}
    return true;
}
