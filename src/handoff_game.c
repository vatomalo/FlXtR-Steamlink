#define _POSIX_C_SOURCE 200809L
/* Isolated portable-state frontend. Never writes another session's saves. */
#include <SDL.h>
#include <json-c/json.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "vendor/libretro.h"
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio;
static unsigned pixel=RETRO_PIXEL_FORMAT_0RGB1555,buttons,frames,quirks;
static bool shutdown_requested;
static bool verify_mode;
static uint32_t frame_hash;
static json_object *options;
static const char *save_dir,*system_dir;
static unsigned width,height;
static bool environment(unsigned command,void *data){
    switch(command){
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:pixel=*(unsigned*)data;return pixel<=RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool*)data=true;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char**)data=system_dir;return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=save_dir;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=data;json_object *value=NULL;v->value=json_object_object_get_ex(options,v->key,&value)?json_object_get_string(value):NULL;return v->value!=NULL;}
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:return true;
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:quirks=(unsigned)*(uint64_t*)data;return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:shutdown_requested=true;return true;
    default:return false;
    }
}
static void video(const void *data,unsigned w,unsigned h,size_t pitch){
    if(!data||data==RETRO_HW_FRAME_BUFFER_VALID||!w||!h||w>2048||h>2048)return;
    if(verify_mode){const uint8_t *bytes=data;uint32_t hash=2166136261u;unsigned bpp=pixel==RETRO_PIXEL_FORMAT_XRGB8888?4:2;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*bpp;x++)hash=(hash^bytes[y*pitch+x])*16777619u;frame_hash=hash;frames++;return;}
    if(!texture||w!=width||h!=height){if(texture)SDL_DestroyTexture(texture);width=w;height=h;
        Uint32 format=pixel==RETRO_PIXEL_FORMAT_XRGB8888?SDL_PIXELFORMAT_ARGB8888:pixel==RETRO_PIXEL_FORMAT_RGB565?SDL_PIXELFORMAT_RGB565:SDL_PIXELFORMAT_ARGB1555;
        texture=SDL_CreateTexture(renderer,format,SDL_TEXTUREACCESS_STREAMING,w,h);}
    if(!texture||SDL_UpdateTexture(texture,NULL,data,(int)pitch))return;
    int sw,sh;SDL_GetRendererOutputSize(renderer,&sw,&sh);SDL_Rect dest={0,0,sw,sh};
    double aspect=(double)w/h;if(sw>sh*aspect){dest.w=(int)(sh*aspect);dest.x=(sw-dest.w)/2;}else{dest.h=(int)(sw/aspect);dest.y=(sh-dest.h)/2;}
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,&dest);SDL_RenderPresent(renderer);frames++;
}
static size_t batch(const int16_t *samples,size_t count){if(audio&&count<65536){if(SDL_GetQueuedAudioSize(audio)>384000)SDL_ClearQueuedAudio(audio);SDL_QueueAudio(audio,samples,(Uint32)(count*4));}return count;}
static void sample(int16_t left,int16_t right){int16_t pair[]={left,right};batch(pair,1);}
static void poll(void){}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id){(void)index;return !port&&device==RETRO_DEVICE_JOYPAD&&id<16?(buttons>>id)&1:0;}
static void *read_file(const char *path,size_t maximum,size_t *size){FILE *f=fopen(path,"rb");if(!f)return NULL;if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long length=ftell(f);rewind(f);if(length<=0||(unsigned long)length>maximum){fclose(f);return NULL;}void *bytes=malloc((size_t)length);if(!bytes){fclose(f);return NULL;}if(fread(bytes,1,(size_t)length,f)!=(size_t)length){free(bytes);fclose(f);return NULL;}fclose(f);*size=(size_t)length;return bytes;}
static const char *field(json_object *object,const char *key){json_object *v=NULL;return json_object_object_get_ex(object,key,&v)?json_object_get_string(v):"";}
static bool write_state_file(const char *manifest,const char *name,const void *data,size_t size){
    const char *slash=strrchr(manifest,'/');if(!slash)return false;
    size_t prefix=(size_t)(slash-manifest)+1,n=strlen(name);char target[4096],temporary[4102];
    if(prefix+n>=sizeof(target))return false;
    memcpy(target,manifest,prefix);memcpy(target+prefix,name,n+1);
    memcpy(temporary,target,prefix+n);memcpy(temporary+prefix+n,".next",6);
    FILE *out=fopen(temporary,"wb");if(!out)return false;
    bool written=fwrite(data,1,size,out)==size;if(fclose(out))written=false;
    return written&&!rename(temporary,target);
}
#define API(X) X(init) X(deinit) X(set_environment) X(set_video_refresh) X(set_audio_sample) X(set_audio_sample_batch) X(set_input_poll) X(set_input_state) X(get_system_info) X(get_system_av_info) X(load_game) X(unload_game) X(run) X(serialize_size) X(serialize) X(unserialize)
#define DECLARE(name) static __typeof__(&retro_##name) core_##name;
API(DECLARE)
int main(int argc,char **argv){
    if(argc!=2&&argc!=3)return 2;
    if(argc==3&&!strcmp(argv[1],"--verify")){verify_mode=true;argv[1]=argv[2];argc=2;}
    json_object *session=NULL;if(argc==2){session=json_object_from_file(argv[1]);if(!session)return 2;}
    const char *library=argc==3?argv[2]:field(session,"core");void *handle=dlopen(library,RTLD_NOW|RTLD_LOCAL);if(!handle){fprintf(stderr,"Cannot load handoff core: %s\n",dlerror());return 2;}
#define RESOLVE(name) core_##name=(__typeof__(core_##name))dlsym(handle,"retro_" #name);if(!core_##name)return 2;
    API(RESOLVE)
    struct retro_system_info info={0};core_get_system_info(&info);char core_info[256];snprintf(core_info,sizeof(core_info),"%s %s",info.library_name,info.library_version);
    if(argc==3){if(strcmp(argv[1],"--info"))return 2;json_object *out=json_object_new_object();json_object_object_add(out,"coreInfo",json_object_new_string(core_info));puts(json_object_to_json_string(out));json_object_put(out);dlclose(handle);return 0;}
    if(strcmp(field(session,"source"),"gambatte:77f2567d718b8973d02caa2daa8dce4953faf811")||strcmp(core_info,field(session,"coreInfo")))return 2;
    if(!json_object_object_get_ex(session,"options",&options))return 2;
    save_dir=field(session,"saveDirectory");if(!*save_dir)save_dir="handoff";system_dir="system";
    SDL_Window *window=NULL;
    if(!verify_mode){
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER))return 2;
    window=SDL_CreateWindow("Continue on TV",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(!window)return 2;
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);if(!renderer)return 2;
    }
    core_set_environment(environment);core_set_video_refresh(video);core_set_audio_sample(sample);core_set_audio_sample_batch(batch);core_set_input_poll(poll);core_set_input_state(input);core_init();
    size_t rom_size=0;void *rom=read_file(field(session,"rom"),16*1024*1024,&rom_size);if(!rom)return 2;
    struct retro_game_info game={field(session,"rom"),info.need_fullpath?NULL:rom,info.need_fullpath?0:rom_size,NULL};if(!core_load_game(&game))return 2;
    struct retro_system_av_info av;core_get_system_av_info(&av);
    if(!verify_mode){SDL_AudioSpec want={0},have;want.freq=(int)av.timing.sample_rate;want.format=AUDIO_S16SYS;want.channels=2;want.samples=1024;audio=SDL_OpenAudioDevice(NULL,0,&want,&have,0);if(audio)SDL_PauseAudioDevice(audio,0);}
    /* Some cores need an initial run before they can accept a state. Never
       acknowledge that boot frame: only a frame AFTER successful restoration. */
    core_run();size_t size=0;void *state=read_file(field(session,"state"),128*1024*1024,&size);
    if(quirks&125||!state||size!=core_serialize_size()||!core_unserialize(state,size))return 2;
    free(state);frames=0;
if(verify_mode){for(int i=0;i<5;i++)core_run();size=core_serialize_size();state=calloc(1,size);if(!state||!core_serialize(state,size))return 2;uint32_t hash=2166136261u;for(size_t i=0;i<size;i++)hash=(hash^((uint8_t*)state)[i])*16777619u;if(!write_state_file(argv[1],"verify-state.bin",state,size))return 2;free(state);printf("{\"restored\":true,\"frames\":%u,\"frameHash\":%u,\"stateHash\":%u}\n",frames,frame_hash,hash);core_unload_game();core_deinit();free(rom);json_object_put(session);dlclose(handle);return frames?0:2;}
    SDL_GameController *controller=NULL;for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);break;}
    const int map[]={SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_BACK,SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    char ack[4096];snprintf(ack,sizeof(ack),"%s",argv[1]);char *slash=strrchr(ack,'/');if(!slash)return 2;strcpy(slash+1,"started");bool acknowledged=false,quit=false;Uint64 next=SDL_GetPerformanceCounter();double frequency=(double)SDL_GetPerformanceFrequency();
    while(!quit&&!shutdown_requested){SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT||(event.type==SDL_KEYDOWN&&event.key.keysym.sym==SDLK_ESCAPE))quit=true;if(event.type==SDL_CONTROLLERDEVICEADDED&&!controller)controller=SDL_GameControllerOpen(event.cdevice.which);}
        buttons=0;if(controller&&SDL_GameControllerGetAttached(controller)){for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++)if(SDL_GameControllerGetButton(controller,(SDL_GameControllerButton)map[i]))buttons|=1u<<i;if((buttons&12)==12)quit=true;}
        const Uint8 *keys=SDL_GetKeyboardState(NULL);if(keys[SDL_SCANCODE_Z])buttons|=1<<0;if(keys[SDL_SCANCODE_X])buttons|=1<<8;if(keys[SDL_SCANCODE_RETURN])buttons|=1<<3;if(keys[SDL_SCANCODE_RSHIFT])buttons|=1<<2;if(keys[SDL_SCANCODE_UP])buttons|=1<<4;if(keys[SDL_SCANCODE_DOWN])buttons|=1<<5;if(keys[SDL_SCANCODE_LEFT])buttons|=1<<6;if(keys[SDL_SCANCODE_RIGHT])buttons|=1<<7;
        core_run();if(frames&&!acknowledged){FILE *f=fopen(ack,"w");if(f){fputs("restored\n",f);fclose(f);acknowledged=true;}}
        next+=(Uint64)(frequency/(av.timing.fps>1?av.timing.fps:60));Uint64 now=SDL_GetPerformanceCounter();if(next>now)SDL_Delay((Uint32)((next-now)*1000/frequency));else if(now-next>frequency)next=now;
    }
    /* Keep TV progress separate from the incoming state and all existing saves. */
    if(acknowledged){size=core_serialize_size();if(size&&size<=128*1024*1024){state=calloc(1,size);if(state&&core_serialize(state,size))write_state_file(argv[1],"state-return.bin",state,size);free(state);}}
    core_unload_game();core_deinit();if(controller)SDL_GameControllerClose(controller);if(audio)SDL_CloseAudioDevice(audio);if(texture)SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();free(rom);json_object_put(session);dlclose(handle);return acknowledged?0:2;
}
