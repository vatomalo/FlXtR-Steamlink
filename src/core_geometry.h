/* A libretro geometry probe runs only in its own short-lived catalog process.
 * Never execute retro_run(), initialize hardware rendering or retain a core
 * inside the UI/launcher. Libretro ABI declarations carry their upstream license. */
#ifndef FLXTR_CORE_GEOMETRY_H
#define FLXTR_CORE_GEOMETRY_H
/* Frontend loads function pointers; do not export core entrypoints on Windows. */
#define RETRO_API RETRO_CALLCONV
#include "vendor/libretro.h"
#undef RETRO_API
static const char *probe_system,*probe_save;
static unsigned probe_rotation;
static bool probe_environment(unsigned command,void *data){
    switch(command){
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            if(data){*(const char**)data=probe_system;return true;}return false;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            if(data){*(const char**)data=probe_save;return true;}return false;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if(data){*(bool*)data=true;return true;}return false;
        case RETRO_ENVIRONMENT_GET_OVERSCAN:
            if(data){*(bool*)data=false;return true;}return false;
        case RETRO_ENVIRONMENT_SET_ROTATION:
            if(data){probe_rotation=*(unsigned*)data%4;return true;}return false;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
            if(data)((struct retro_variable*)data)->value=NULL;
            return false;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if(data){*(bool*)data=false;return true;}return false;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return data&&*(enum retro_pixel_format*)data<=RETRO_PIXEL_FORMAT_RGB565;
        default:return false;
    }
}
static void probe_video(const void *data,unsigned w,unsigned h,size_t pitch){(void)data;(void)w;(void)h;(void)pitch;}
static void probe_audio(int16_t left,int16_t right){(void)left;(void)right;}
static size_t probe_audio_batch(const int16_t *data,size_t frames){(void)data;return frames;}
static void probe_input_poll(void){}
static int16_t probe_input_state(unsigned port,unsigned device,unsigned index,unsigned id){(void)port;(void)device;(void)index;(void)id;return 0;}
static int core_geometry_probe(const char *core,const char *rom,const char *system,const char *save,int want_display){
    void *library=SDL_LoadObject(core);if(!library)return 2;
#define PROBE_FUNCTION(type,name,args) type (*name)args=(type (*)args)SDL_LoadFunction(library,#name)
    PROBE_FUNCTION(void,retro_set_environment,(retro_environment_t));
    PROBE_FUNCTION(void,retro_set_video_refresh,(retro_video_refresh_t));
    PROBE_FUNCTION(void,retro_set_audio_sample,(retro_audio_sample_t));
    PROBE_FUNCTION(void,retro_set_audio_sample_batch,(retro_audio_sample_batch_t));
    PROBE_FUNCTION(void,retro_set_input_poll,(retro_input_poll_t));
    PROBE_FUNCTION(void,retro_set_input_state,(retro_input_state_t));
    PROBE_FUNCTION(unsigned,retro_api_version,(void));
    PROBE_FUNCTION(void,retro_init,(void));
    PROBE_FUNCTION(void,retro_deinit,(void));
    PROBE_FUNCTION(void,retro_get_system_info,(struct retro_system_info*));
    PROBE_FUNCTION(void,retro_get_system_av_info,(struct retro_system_av_info*));
    PROBE_FUNCTION(bool,retro_load_game,(const struct retro_game_info*));
    PROBE_FUNCTION(void,retro_unload_game,(void));
#undef PROBE_FUNCTION
    if(!retro_set_environment||!retro_set_video_refresh||!retro_set_audio_sample||
       !retro_set_audio_sample_batch||!retro_set_input_poll||!retro_set_input_state||
       !retro_api_version||!retro_init||!retro_deinit||!retro_get_system_info||!retro_get_system_av_info||
       !retro_load_game||!retro_unload_game){SDL_UnloadObject(library);return 2;}
    if(retro_api_version()!=RETRO_API_VERSION){SDL_UnloadObject(library);return 2;}
    probe_system=system;probe_save=save;probe_rotation=0;
    retro_set_environment(probe_environment);retro_set_video_refresh(probe_video);
    retro_set_audio_sample(probe_audio);retro_set_audio_sample_batch(probe_audio_batch);
    retro_set_input_poll(probe_input_poll);retro_set_input_state(probe_input_state);
    struct retro_system_info info={0};struct retro_system_av_info av={0};
    retro_init();retro_get_system_info(&info);
    struct retro_game_info game={rom,NULL,0,NULL};void *data=NULL;int loaded=0,result=2;
    if(!info.need_fullpath){
        FILE *f=fopen(rom,"rb");if(!f)goto done;
        if(fseek(f,0,SEEK_END)){fclose(f);goto done;}
        long size=ftell(f);
        if(size<1||size>64*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);goto done;}
        data=malloc((size_t)size);
        if(!data||fread(data,1,(size_t)size,f)!=(size_t)size){fclose(f);goto done;}
        fclose(f);game.data=data;game.size=(size_t)size;
    }
    if(!(loaded=retro_load_game(&game)))goto done;
    retro_get_system_av_info(&av);
    if(!av.geometry.base_width||!av.geometry.base_height||
       av.geometry.base_width>8192||av.geometry.base_height>8192)goto done;
    result=0;
done:
    if(loaded)retro_unload_game();
    retro_deinit();free(data);SDL_UnloadObject(library);
    if(!result){
        unsigned width=av.geometry.base_width,height=av.geometry.base_height;
        if(probe_rotation&1){unsigned swap=width;width=height;height=swap;}
        SDL_DisplayMode display={0};
        if(want_display&&!SDL_InitSubSystem(SDL_INIT_VIDEO)){SDL_GetCurrentDisplayMode(0,&display);SDL_QuitSubSystem(SDL_INIT_VIDEO);}
        printf("FLXTR_GEOMETRY %u %u %d %d\n",width,height,display.w,display.h);
    }
    return result;
}
#endif
