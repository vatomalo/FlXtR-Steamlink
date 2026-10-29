#include <jni.h>
#include <stdlib.h>
#include "resolver_bridge.h"
#include "wasm-rt-impl.h"
#include "wasm-rt-exceptions.h"
/* Java serializes complete sessions: the reused bridge has process-global state. */
static int active;
static void decoder_error(JNIEnv *env,const char *message){
    jclass error=(*env)->FindClass(env,"java/io/IOException");
    if(error){(*env)->ThrowNew(env,error,message);(*env)->DeleteLocalRef(env,error);}
}
JNIEXPORT jstring JNICALL Java_org_flxtr_android_StreamDecoder_nativeBegin(JNIEnv *env,jclass type){
    (void)type;
    if(active){resolver_free();active=0;}
    if(wasm_rt_impl_try()){decoder_error(env,"Response decoder initialization failed");return NULL;}
    active=1;resolver_init();
    char *key=resolver_key();if(!key){decoder_error(env,resolver_error());return NULL;}
    jstring value=(*env)->NewStringUTF(env,key);free(key);return value;
}
JNIEXPORT jstring JNICALL Java_org_flxtr_android_StreamDecoder_nativeDecode(JNIEnv *env,jclass type,jstring payload,jstring key){
    (void)type;
    if(!active||!payload||!key||(*env)->GetStringUTFLength(env,payload)>2*1024*1024||(*env)->GetStringUTFLength(env,key)!=64){decoder_error(env,"Invalid response decoder input");return NULL;}
    const char *input=(*env)->GetStringUTFChars(env,payload,NULL);
    const char *secret=(*env)->GetStringUTFChars(env,key,NULL);
    if(!input||!secret){if(input)(*env)->ReleaseStringUTFChars(env,payload,input);if(secret)(*env)->ReleaseStringUTFChars(env,key,secret);return NULL;}
    if(wasm_rt_impl_try()){
        (*env)->ReleaseStringUTFChars(env,payload,input);(*env)->ReleaseStringUTFChars(env,key,secret);
        decoder_error(env,"Stream response could not be decoded");return NULL;
    }
    char *decoded=resolver_decode(input,secret);
    (*env)->ReleaseStringUTFChars(env,payload,input);(*env)->ReleaseStringUTFChars(env,key,secret);
    if(!decoded){decoder_error(env,resolver_error());return NULL;}
    jstring value=(*env)->NewStringUTF(env,decoded);free(decoded);return value;
}
JNIEXPORT void JNICALL Java_org_flxtr_android_StreamDecoder_nativeEnd(JNIEnv *env,jclass type){
    (void)env;(void)type;if(active){resolver_free();active=0;}
}
