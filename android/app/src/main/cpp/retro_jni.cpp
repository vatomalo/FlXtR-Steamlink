#include <jni.h>
#include <android/native_window_jni.h>
#include <libretro.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

static ANativeWindow *window;
static std::vector<uint8_t> rom;
static std::vector<int16_t> sound;
static std::string directory;
static unsigned buttons,pixel=RETRO_PIXEL_FORMAT_0RGB1555;
static bool loaded,initialized;
static bool environment(unsigned cmd,void *data){
    switch(cmd){
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:pixel=*(unsigned*)data;return pixel<=RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool*)data=true;return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=directory.c_str();return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:return true;
        default:return false;
    }
}
static void video(const void *data,unsigned width,unsigned height,size_t pitch){
    if(!window||!data||data==RETRO_HW_FRAME_BUFFER_VALID||width>2048||height>2048)return;
    ANativeWindow_setBuffersGeometry(window,width,height,WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer buffer;if(ANativeWindow_lock(window,&buffer,nullptr))return;
    for(unsigned y=0;y<height;y++){
        auto *dest=(uint32_t*)buffer.bits+y*buffer.stride;
        const uint8_t *src=(const uint8_t*)data+y*pitch;
        for(unsigned x=0;x<width;x++){
            uint32_t r,g,b;
            if(pixel==RETRO_PIXEL_FORMAT_XRGB8888){uint32_t p=((const uint32_t*)src)[x];r=(p>>16)&255;g=(p>>8)&255;b=p&255;}
            else{uint16_t p=((const uint16_t*)src)[x];if(pixel==RETRO_PIXEL_FORMAT_RGB565){r=((p>>11)&31)*255/31;g=((p>>5)&63)*255/63;}else{r=((p>>10)&31)*255/31;g=((p>>5)&31)*255/31;}b=(p&31)*255/31;}
            dest[x]=0xff000000u|(b<<16)|(g<<8)|r;
        }
    }
    ANativeWindow_unlockAndPost(window);
}
static void sample(int16_t l,int16_t r){if(sound.size()<32768){sound.push_back(l);sound.push_back(r);}}
static size_t batch(const int16_t *data,size_t frames){if(frames<=16384&&sound.size()+frames*2<=32768)sound.insert(sound.end(),data,data+frames*2);return frames;}
static void poll(){}
static int16_t input(unsigned port,unsigned device,unsigned,unsigned id){if(port||device!=RETRO_DEVICE_JOYPAD)return 0;if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)buttons;return id<16?((buttons>>id)&1):0;}
extern "C" JNIEXPORT jdoubleArray JNICALL Java_org_flxtr_android_RetroActivity_startCore(JNIEnv *env,jclass,jstring path,jstring saves,jobject surface){
    if(initialized)return nullptr;
    const char *p=env->GetStringUTFChars(path,nullptr);FILE *file=fopen(p,"rb");env->ReleaseStringUTFChars(path,p);if(!file)return nullptr;
    fseek(file,0,SEEK_END);long length=ftell(file);rewind(file);if(length<=0||length>16*1024*1024){fclose(file);return nullptr;}rom.resize(length);bool read=fread(rom.data(),1,length,file)==(size_t)length;fclose(file);if(!read)return nullptr;
    const char *d=env->GetStringUTFChars(saves,nullptr);directory=d;env->ReleaseStringUTFChars(saves,d);
    window=ANativeWindow_fromSurface(env,surface);retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample(sample);retro_set_audio_sample_batch(batch);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();initialized=true;
    retro_game_info game={};game.data=rom.data();game.size=rom.size();loaded=retro_load_game(&game);if(!loaded){retro_deinit();initialized=false;if(window)ANativeWindow_release(window);window=nullptr;return nullptr;}
    retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);retro_system_av_info av={};retro_get_system_av_info(&av);
    jdouble info[]={av.timing.fps,av.timing.sample_rate,av.geometry.aspect_ratio>0?av.geometry.aspect_ratio:(double)av.geometry.base_width/av.geometry.base_height};auto result=env->NewDoubleArray(3);env->SetDoubleArrayRegion(result,0,3,info);return result;
}
extern "C" JNIEXPORT jshortArray JNICALL Java_org_flxtr_android_RetroActivity_frame(JNIEnv *env,jclass,jint mask){sound.clear();buttons=(unsigned)mask;if(loaded)retro_run();auto result=env->NewShortArray(sound.size());if(result)env->SetShortArrayRegion(result,0,sound.size(),sound.data());return result;}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_flxtr_android_RetroActivity_readMemory(JNIEnv *env,jclass,jboolean state){
    if(!loaded)return nullptr;size_t size=state?retro_serialize_size():retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);if(!size||size>8*1024*1024)return nullptr;std::vector<uint8_t> bytes(size);
    if(state){if(!retro_serialize(bytes.data(),size))return nullptr;}else{void *data=retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);if(!data)return nullptr;memcpy(bytes.data(),data,size);}
    auto result=env->NewByteArray(size);if(result)env->SetByteArrayRegion(result,0,size,(jbyte*)bytes.data());return result;
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_flxtr_android_RetroActivity_writeMemory(JNIEnv *env,jclass,jbyteArray bytes,jboolean state){
    if(!loaded||!bytes)return false;size_t size=env->GetArrayLength(bytes),expected=state?retro_serialize_size():retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);if(size!=expected||size>8*1024*1024)return false;std::vector<uint8_t> data(size);env->GetByteArrayRegion(bytes,0,size,(jbyte*)data.data());if(state)return retro_unserialize(data.data(),size);void *target=retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);if(!target)return false;memcpy(target,data.data(),size);return true;
}
extern "C" JNIEXPORT void JNICALL Java_org_flxtr_android_RetroActivity_resetCore(JNIEnv*,jclass){if(loaded)retro_reset();}
extern "C" JNIEXPORT void JNICALL Java_org_flxtr_android_RetroActivity_stopCore(JNIEnv*,jclass){if(loaded)retro_unload_game();if(initialized)retro_deinit();loaded=initialized=false;if(window)ANativeWindow_release(window);window=nullptr;rom.clear();sound.clear();}
