#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <libretro.h>
#include <dlfcn.h>
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <new>
#include <mutex>
#include <sys/stat.h>
static std::atomic<unsigned> renderedFrames{0};
#include "retro_egl.h"
#include <vfs/vfs_implementation.h>
static retro_vfs_interface filesystem={retro_vfs_file_get_path_impl,retro_vfs_file_open_impl,retro_vfs_file_close_impl,retro_vfs_file_size_impl,retro_vfs_file_tell_impl,retro_vfs_file_seek_impl,retro_vfs_file_read_impl,retro_vfs_file_write_impl,retro_vfs_file_flush_impl,retro_vfs_file_remove_impl,retro_vfs_file_rename_impl,retro_vfs_file_truncate_impl,retro_vfs_stat_impl,retro_vfs_mkdir_impl,retro_vfs_opendir_impl,retro_vfs_readdir_impl,retro_vfs_dirent_get_name_impl,retro_vfs_dirent_is_dir_impl,retro_vfs_closedir_impl};
static int stat64File(const char *path,int64_t *size){struct stat64 info;if(!path||stat64(path,&info))return 0;if(size)*size=info.st_size;return RETRO_VFS_STAT_IS_VALID|(S_ISDIR(info.st_mode)?RETRO_VFS_STAT_IS_DIRECTORY:0)|(S_ISCHR(info.st_mode)?RETRO_VFS_STAT_IS_CHARACTER_SPECIAL:0);}
struct FileInterface4 {retro_vfs_interface base;int (*stat_64)(const char*,int64_t*);};
static FileInterface4 filesystem4={filesystem,stat64File};
static std::mutex messageMutex;
static std::string coreMessage;
static void notification(const char *message){std::lock_guard<std::mutex> lock(messageMutex);coreMessage.assign(message?message:"",message?strnlen(message,1024):0);}

#define CORE_FUNCTIONS(X) X(init) X(deinit) X(set_environment) X(set_video_refresh) X(set_audio_sample) X(set_audio_sample_batch) X(set_input_poll) X(set_input_state) X(get_system_info) X(get_system_av_info) X(set_controller_port_device) X(load_game) X(unload_game) X(run) X(reset) X(serialize_size) X(serialize) X(unserialize) X(get_memory_data) X(get_memory_size)
struct Core {
#define DECLARE(name) decltype(&retro_##name) name=nullptr;
    CORE_FUNCTIONS(DECLARE)
#undef DECLARE
} core;
static void *coreHandle;
static ANativeWindow *window;
static std::vector<uint8_t> rom;
static std::vector<int16_t> sound;
static std::map<std::string,std::string> options;
static std::string saveDirectory,systemDirectory,gamePath;
static retro_disk_control_callback disks={};
static unsigned buttons,pixel=RETRO_PIXEL_FORMAT_0RGB1555;
static int16_t analogAxes[4];
static int16_t mouseX,mouseY,pointerX,pointerY;
static bool pointerPressed;
static bool loaded,initialized;
static bool supportsNoGame;
static retro_keyboard_callback keyboard={};
static void logCore(enum retro_log_level level,const char *format,...){va_list args;va_start(args,format);__android_log_vprint(level>=RETRO_LOG_ERROR?ANDROID_LOG_ERROR:ANDROID_LOG_INFO,"FlXtR-Libretro",format,args);va_end(args);}
static void definitions(const retro_core_option_definition *defs){if(!defs)return;for(auto d=defs;d->key;d++)options.emplace(d->key,d->default_value?d->default_value:(d->values[0].value?d->values[0].value:""));}
static void definitionsV2(const retro_core_options_v2 *v2){if(!v2||!v2->definitions)return;for(auto d=v2->definitions;d->key;d++)options.emplace(d->key,d->default_value?d->default_value:(d->values[0].value?d->values[0].value:""));}
static bool environment(unsigned cmd,void *data){
    switch(cmd){
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:if(!data)return false;keyboard=*(retro_keyboard_callback*)data;return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:supportsNoGame=data&&*(bool*)data;return true;
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:*(retro_hw_context_type*)data=RETRO_HW_CONTEXT_OPENGLES3;return true;
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:{auto *interface=(retro_vfs_interface_info*)data;if(!interface||interface->required_interface_version>4)return false;interface->required_interface_version=4;interface->iface=&filesystem4.base;return true;}
        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:*(unsigned*)data=1;return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:if(data)notification(((retro_message*)data)->msg);return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:if(data)notification(((retro_message_ext*)data)->msg);return true;
        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:*(uint64_t*)data=(1ULL<<RETRO_DEVICE_JOYPAD)|(1ULL<<RETRO_DEVICE_ANALOG)|(1ULL<<RETRO_DEVICE_MOUSE)|(1ULL<<RETRO_DEVICE_POINTER);return true;
        case RETRO_ENVIRONMENT_SET_HW_RENDER:return hardwareRequest((retro_hw_render_callback*)data);
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:pixel=*(unsigned*)data;return pixel<=RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool*)data=true;return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char**)data=systemDirectory.c_str();return true;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=saveDirectory.c_str();return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:{auto *v=(retro_variable*)data;auto found=options.find(v->key?v->key:"");v->value=found==options.end()?nullptr:found->second.c_str();return v->value!=nullptr;}
        case RETRO_ENVIRONMENT_SET_VARIABLES:{for(auto v=(retro_variable*)data;v&&v->key;v++){std::string value=v->value?v->value:"";size_t start=value.find(';');if(start!=std::string::npos){start++;while(start<value.size()&&value[start]==' ')start++;value=value.substr(start,value.find('|',start)-start);}options.emplace(v->key,value);}return true;}
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)data=2;return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:definitions((retro_core_option_definition*)data);return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:definitions(((retro_core_options_intl*)data)->us);return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:definitionsV2((retro_core_options_v2*)data);return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:definitionsV2(((retro_core_options_v2_intl*)data)->us);return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((retro_log_callback*)data)->log=logCore;return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:*(unsigned*)data=RETRO_LANGUAGE_ENGLISH;return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
        case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:*(unsigned*)data=1;return true;
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:disks=*(retro_disk_control_callback*)data;return true;
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:memcpy(&disks,data,sizeof(disks));return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:case RETRO_ENVIRONMENT_SET_GEOMETRY:return true;
        default:return false;
    }
}
static void video(const void *data,unsigned width,unsigned height,size_t pitch){
    if(data==RETRO_HW_FRAME_BUFFER_VALID){if(hardwareReset&&eglSwapBuffers(hardwareDisplay,hardwareSurface))renderedFrames.fetch_add(1,std::memory_order_relaxed);return;}
    if(!window||!data||data==RETRO_HW_FRAME_BUFFER_VALID||!width||!height||width>2048||height>2048)return;
    if(pitch<width*(pixel==RETRO_PIXEL_FORMAT_XRGB8888?4:2))return;
    ANativeWindow_setBuffersGeometry(window,width,height,WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer buffer;if(ANativeWindow_lock(window,&buffer,nullptr))return;
    if(buffer.width<(int)width||buffer.height<(int)height){ANativeWindow_unlockAndPost(window);return;}
    for(unsigned y=0;y<height;y++){
        auto *dest=(uint32_t*)buffer.bits+y*buffer.stride;const uint8_t *src=(const uint8_t*)data+y*pitch;
        for(unsigned x=0;x<width;x++){uint32_t r,g,b;
            if(pixel==RETRO_PIXEL_FORMAT_XRGB8888){uint32_t p=((const uint32_t*)src)[x];r=(p>>16)&255;g=(p>>8)&255;b=p&255;}
            else{uint16_t p=((const uint16_t*)src)[x];if(pixel==RETRO_PIXEL_FORMAT_RGB565){r=((p>>11)&31)*255/31;g=((p>>5)&63)*255/63;}else{r=((p>>10)&31)*255/31;g=((p>>5)&31)*255/31;}b=(p&31)*255/31;}
            dest[x]=0xff000000u|(b<<16)|(g<<8)|r;
        }
    }
    if(!ANativeWindow_unlockAndPost(window))renderedFrames.fetch_add(1,std::memory_order_relaxed);
}
static void sample(int16_t l,int16_t r){if(sound.size()<32768){sound.push_back(l);sound.push_back(r);}}
static size_t batch(const int16_t *data,size_t frames){if(frames<=16384&&sound.size()+frames*2<=32768)sound.insert(sound.end(),data,data+frames*2);return frames;}
static void poll(){}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id){if(port)return 0;device&=RETRO_DEVICE_MASK;if(device==RETRO_DEVICE_POINTER){if(index)return 0;return id==RETRO_DEVICE_ID_POINTER_X?pointerX:id==RETRO_DEVICE_ID_POINTER_Y?pointerY:id==RETRO_DEVICE_ID_POINTER_PRESSED?pointerPressed:0;}if(device==RETRO_DEVICE_MOUSE)return id==RETRO_DEVICE_ID_MOUSE_X?mouseX:id==RETRO_DEVICE_ID_MOUSE_Y?mouseY:id==RETRO_DEVICE_ID_MOUSE_LEFT?(pointerPressed||((buttons>>8)&1)):id==RETRO_DEVICE_ID_MOUSE_RIGHT?(buttons&1):0;if(device==RETRO_DEVICE_ANALOG)return index<2&&id<2?analogAxes[index*2+id]:0;if(device!=RETRO_DEVICE_JOYPAD)return 0;if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)buttons;return id<16?((buttons>>id)&1):0;}
static void cleanup(){if(loaded)core.unload_game();hardwareClose();if(initialized)core.deinit();loaded=initialized=false;if(window)ANativeWindow_release(window);window=nullptr;if(coreHandle)dlclose(coreHandle);coreHandle=nullptr;rom.clear();sound.clear();options.clear();disks={};pixel=RETRO_PIXEL_FORMAT_0RGB1555;core=Core();}
static std::string string(JNIEnv *env,jstring value){if(!value)return "";const char *s=env->GetStringUTFChars(value,nullptr);if(!s)return "";std::string result=s;env->ReleaseStringUTFChars(value,s);return result;}
extern "C" JNIEXPORT jstring JNICALL Java_org_flxtr_android_RetroActivity_coreInfo(JNIEnv *env,jclass,jstring library){
    if(initialized)return nullptr;std::string path=string(env,library);void *handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)return nullptr;
    auto getInfo=reinterpret_cast<decltype(&retro_get_system_info)>(dlsym(handle,"retro_get_system_info"));retro_system_info info={};
    jstring result=nullptr;if(getInfo){getInfo(&info);std::string description=std::string(info.library_name?info.library_name:"")+" "+(info.library_version?info.library_version:"");result=env->NewStringUTF(description.c_str());}dlclose(handle);return result;
}
extern "C" JNIEXPORT jdoubleArray JNICALL Java_org_flxtr_android_RetroActivity_startCore(JNIEnv *env,jclass,jstring path,jstring saves,jstring system,jstring library,jobject surface){
    if(initialized)return nullptr;
    renderedFrames.store(0,std::memory_order_relaxed);
    notification("");
    supportsNoGame=false;
    keyboard={};
    gamePath=string(env,path);saveDirectory=string(env,saves);systemDirectory=string(env,system);std::string lib=string(env,library);
    if(lib.empty()){
#define BUILTIN(name) core.name=&retro_##name;
        CORE_FUNCTIONS(BUILTIN)
#undef BUILTIN
    }else{
        coreHandle=dlopen(lib.c_str(),RTLD_NOW|RTLD_LOCAL);if(!coreHandle){__android_log_print(ANDROID_LOG_ERROR,"FlXtR-Libretro","Core load failed: %s",dlerror());return nullptr;}
#define RESOLVE(name) core.name=reinterpret_cast<decltype(core.name)>(dlsym(coreHandle,"retro_" #name));if(!core.name){cleanup();return nullptr;}
        CORE_FUNCTIONS(RESOLVE)
#undef RESOLVE
    }
    window=ANativeWindow_fromSurface(env,surface);if(!window){cleanup();return nullptr;}
    core.set_environment(environment);core.set_video_refresh(video);core.set_audio_sample(sample);core.set_audio_sample_batch(batch);core.set_input_poll(poll);core.set_input_state(input);core.init();initialized=true;
    retro_system_info info={};core.get_system_info(&info);retro_game_info game={};game.path=gamePath.c_str();
if(!gamePath.empty()&&!info.need_fullpath){FILE *file=fopen(gamePath.c_str(),"rb");if(!file){cleanup();return nullptr;}fseek(file,0,SEEK_END);long length=ftell(file);rewind(file);if(length<=0||length>64*1024*1024){fclose(file);cleanup();return nullptr;}rom.resize(length);bool read=fread(rom.data(),1,length,file)==(size_t)length;fclose(file);if(!read){cleanup();return nullptr;}game.data=rom.data();game.size=rom.size();}
    if(gamePath.empty()&&!supportsNoGame){cleanup();return nullptr;}
    loaded=core.load_game(gamePath.empty()?nullptr:&game);if(!loaded){cleanup();return nullptr;}
    core.set_controller_port_device(0,RETRO_DEVICE_JOYPAD);retro_system_av_info av={};core.get_system_av_info(&av);
    if(av.geometry.base_width==0||av.geometry.base_height==0||av.timing.fps<=0){cleanup();return nullptr;}
    if(hardware.context_reset&&!hardwareOpen(window,av.geometry.base_width,av.geometry.base_height)){cleanup();return nullptr;}
    jdouble values[]={av.timing.fps,av.timing.sample_rate,av.geometry.aspect_ratio>0?av.geometry.aspect_ratio:(double)av.geometry.base_width/av.geometry.base_height,(double)av.geometry.base_width,(double)av.geometry.base_height};auto result=env->NewDoubleArray(5);if(result)env->SetDoubleArrayRegion(result,0,5,values);else cleanup();return result;
}
extern "C" JNIEXPORT jshortArray JNICALL Java_org_flxtr_android_RetroActivity_frame(JNIEnv *env,jclass,jint mask,jshort leftX,jshort leftY,jshort rightX,jshort rightY,jshort deltaX,jshort deltaY,jshort absoluteX,jshort absoluteY,jboolean pressed){mouseX=deltaX;mouseY=deltaY;pointerX=absoluteX;pointerY=absoluteY;pointerPressed=pressed;analogAxes[0]=leftX;analogAxes[1]=leftY;analogAxes[2]=rightX;analogAxes[3]=rightY;sound.clear();buttons=(unsigned)mask;if(loaded)core.run();auto result=env->NewShortArray(sound.size());if(result)env->SetShortArrayRegion(result,0,sound.size(),sound.data());return result;}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_flxtr_android_RetroActivity_readMemory(JNIEnv *env,jclass,jboolean state)try{
    if(!loaded)return nullptr;size_t size=state?core.serialize_size():core.get_memory_size(RETRO_MEMORY_SAVE_RAM);if(!size||size>128*1024*1024)return nullptr;std::vector<uint8_t> bytes(size);
    if(state){if(!core.serialize(bytes.data(),size))return nullptr;}else{void *data=core.get_memory_data(RETRO_MEMORY_SAVE_RAM);if(!data)return nullptr;memcpy(bytes.data(),data,size);}
    auto result=env->NewByteArray(size);if(result)env->SetByteArrayRegion(result,0,size,(jbyte*)bytes.data());return result;
}
catch(const std::bad_alloc&){return nullptr;}
extern "C" JNIEXPORT jboolean JNICALL Java_org_flxtr_android_RetroActivity_writeMemory(JNIEnv *env,jclass,jbyteArray bytes,jboolean state)try{
    if(!loaded||!bytes)return false;size_t size=env->GetArrayLength(bytes),expected=state?core.serialize_size():core.get_memory_size(RETRO_MEMORY_SAVE_RAM);if(size!=expected||size>128*1024*1024)return false;std::vector<uint8_t> data(size);env->GetByteArrayRegion(bytes,0,size,(jbyte*)data.data());if(state)return core.unserialize(data.data(),size);void *target=core.get_memory_data(RETRO_MEMORY_SAVE_RAM);if(!target)return false;memcpy(target,data.data(),size);return true;
}
catch(const std::bad_alloc&){return false;}
extern "C" JNIEXPORT jint JNICALL Java_org_flxtr_android_RetroActivity_diskCount(JNIEnv*,jclass){return loaded&&disks.get_num_images?disks.get_num_images():0;}
extern "C" JNIEXPORT jint JNICALL Java_org_flxtr_android_RetroActivity_diskIndex(JNIEnv*,jclass){return loaded&&disks.get_image_index?disks.get_image_index():0;}
extern "C" JNIEXPORT jboolean JNICALL Java_org_flxtr_android_RetroActivity_swapDisk(JNIEnv*,jclass,jint index){return loaded&&disks.set_eject_state&&disks.set_image_index&&index>=0&&disks.get_num_images&&(unsigned)index<disks.get_num_images()&&disks.set_eject_state(true)&&disks.set_image_index(index)&&disks.set_eject_state(false);}
extern "C" JNIEXPORT void JNICALL Java_org_flxtr_android_RetroActivity_resetCore(JNIEnv*,jclass){if(loaded)core.reset();}
extern "C" JNIEXPORT void JNICALL Java_org_flxtr_android_RetroActivity_stopCore(JNIEnv*,jclass){cleanup();}
extern "C" JNIEXPORT jint JNICALL Java_org_flxtr_android_RetroActivity_renderedFrames(JNIEnv*,jclass){return (jint)renderedFrames.load(std::memory_order_relaxed);}
extern "C" JNIEXPORT void JNICALL Java_org_flxtr_android_RetroActivity_sendKey(JNIEnv*,jclass,jboolean down,jint key,jint character,jint modifiers){if(initialized&&keyboard.callback)keyboard.callback(down,(unsigned)key,(uint32_t)character,(uint16_t)modifiers);}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_flxtr_android_RetroActivity_messageBytes(JNIEnv *env,jclass){std::lock_guard<std::mutex> lock(messageMutex);auto result=env->NewByteArray(coreMessage.size());if(result&&!coreMessage.empty())env->SetByteArrayRegion(result,0,coreMessage.size(),(const jbyte*)coreMessage.data());return result;}
