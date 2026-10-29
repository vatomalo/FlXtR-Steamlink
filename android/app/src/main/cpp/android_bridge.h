#ifndef FLXTR_ANDROID_BRIDGE_H
#define FLXTR_ANDROID_BRIDGE_H
#include <jni.h>
#include <SDL_system.h>
static int flxtr_android_play(const char *url,const char *title){
    if(strncmp(url,"https://",8))return -1;
    JNIEnv *env=(JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity=(jobject)SDL_AndroidGetActivity();
    if(!env||!activity)return -1;
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"launchVideo","(Ljava/lang/String;Ljava/lang/String;)V");
    jstring media=(*env)->NewStringUTF(env,url),label=(*env)->NewStringUTF(env,title);
    int failed=!method||!media||!label;
    if(!failed)(*env)->CallVoidMethod(env,activity,method,media,label);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);failed=1;}
    if(media)(*env)->DeleteLocalRef(env,media);
    if(label)(*env)->DeleteLocalRef(env,label);
    (*env)->DeleteLocalRef(env,type);(*env)->DeleteLocalRef(env,activity);
    return failed?-1:0;
}
#endif
