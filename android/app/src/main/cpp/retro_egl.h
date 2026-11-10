#ifndef FLXTR_RETRO_EGL_H
#define FLXTR_RETRO_EGL_H
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static retro_hw_render_callback hardware={};
static EGLDisplay hardwareDisplay=EGL_NO_DISPLAY;
static EGLContext hardwareContext=EGL_NO_CONTEXT;
static EGLSurface hardwareSurface=EGL_NO_SURFACE;
static bool hardwareReset;
static uintptr_t framebuffer(){return 0;}
static retro_proc_address_t graphicsProc(const char *name){return reinterpret_cast<retro_proc_address_t>(eglGetProcAddress(name));}
static bool hardwareRequest(retro_hw_render_callback *request){
    if(!request)return false;
    if(request->context_type!=RETRO_HW_CONTEXT_OPENGLES2&&request->context_type!=RETRO_HW_CONTEXT_OPENGLES3&&request->context_type!=RETRO_HW_CONTEXT_OPENGLES_VERSION)return false;
    if(request->context_type==RETRO_HW_CONTEXT_OPENGLES_VERSION&&(request->version_major<2||request->version_major>3||(request->version_major==2&&request->version_minor)||(request->version_major==3&&request->version_minor>2)))return false;
    request->get_current_framebuffer=framebuffer;request->get_proc_address=graphicsProc;
    hardware=*request;return true;
}
static void hardwareClose(){
    if(hardwareReset&&hardware.context_destroy)hardware.context_destroy();
    hardwareReset=false;
    if(hardwareDisplay!=EGL_NO_DISPLAY){
        eglMakeCurrent(hardwareDisplay,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(hardwareContext!=EGL_NO_CONTEXT)eglDestroyContext(hardwareDisplay,hardwareContext);
        if(hardwareSurface!=EGL_NO_SURFACE)eglDestroySurface(hardwareDisplay,hardwareSurface);
        eglTerminate(hardwareDisplay);
    }
    hardwareDisplay=EGL_NO_DISPLAY;hardwareContext=EGL_NO_CONTEXT;hardwareSurface=EGL_NO_SURFACE;hardware={};
}
static bool hardwareOpen(ANativeWindow *target,unsigned width,unsigned height){
    if(!hardware.context_reset||!target||!width||!height||width>4096||height>4096)return false;
    int version=hardware.context_type==RETRO_HW_CONTEXT_OPENGLES2?2:hardware.context_type==RETRO_HW_CONTEXT_OPENGLES_VERSION?(int)hardware.version_major:3;
    hardwareDisplay=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(hardwareDisplay==EGL_NO_DISPLAY||!eglInitialize(hardwareDisplay,nullptr,nullptr)||!eglBindAPI(EGL_OPENGL_ES_API))return false;
const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,version==3?0x0040:EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,hardware.depth?24:0,EGL_STENCIL_SIZE,hardware.stencil?8:0,EGL_NONE};
    EGLConfig config;EGLint count=0,format=0;
    if(!eglChooseConfig(hardwareDisplay,attributes,&config,1,&count)||count!=1||!eglGetConfigAttrib(hardwareDisplay,config,EGL_NATIVE_VISUAL_ID,&format))return false;
    if(ANativeWindow_setBuffersGeometry(target,width,height,format))return false;
    hardwareSurface=eglCreateWindowSurface(hardwareDisplay,config,target,nullptr);
    const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,version,EGL_NONE};
    const EGLint versionedAttributes[]={EGL_CONTEXT_CLIENT_VERSION,version,EGL_CONTEXT_MINOR_VERSION_KHR,(EGLint)hardware.version_minor,EGL_NONE};
    hardwareContext=eglCreateContext(hardwareDisplay,config,EGL_NO_CONTEXT,hardware.context_type==RETRO_HW_CONTEXT_OPENGLES_VERSION&&hardware.version_minor?versionedAttributes:contextAttributes);
    if(hardwareSurface==EGL_NO_SURFACE||hardwareContext==EGL_NO_CONTEXT||!eglMakeCurrent(hardwareDisplay,hardwareSurface,hardwareSurface,hardwareContext))return false;
    eglSwapInterval(hardwareDisplay,0);glViewport(0,0,width,height);
    hardwareReset=true;hardware.context_reset();return true;
}
#endif
