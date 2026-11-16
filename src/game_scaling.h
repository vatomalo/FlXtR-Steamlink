#ifndef FLXTR_GAME_SCALING_H
#define FLXTR_GAME_SCALING_H
enum {GAME_FIT,GAME_STRETCH,GAME_PIXEL,GAME_INTEGER,GAME_VIEWS};
static const char *const game_view_names[]={"FULLSCREEN FIT","STRETCH","1:1 CORE PIXELS","INTEGER FIT"};
static int game_view_valid(int mode){return mode>=0&&mode<GAME_VIEWS;}
/* RetroArch 1.7.4 through 1.21 keep Core Provided=22 and Custom=23.
 * Use frontend-owned core geometry for fit; never retain custom crop/scaling
 * from a previous fullscreen resolution. */
#ifdef FLXTR_GAME_CONFIG
static void game_scaling_config(FILE *f,int mode,int sw,int sh,int dw,int dh){
    fprintf(f,"video_force_aspect = \"%s\"\nvideo_scale_integer = \"false\"\nvideo_crop_overscan = \"false\"\nauto_overrides_enable = \"false\"\n",
        mode==GAME_STRETCH?"false":"true");
    if(mode==GAME_INTEGER){
        fputs("video_scale_integer = \"true\"\naspect_ratio_index = \"22\"\ncustom_viewport_x = \"0\"\ncustom_viewport_y = \"0\"\ncustom_viewport_width = \"0\"\ncustom_viewport_height = \"0\"\n",f);
    }else if(mode==GAME_STRETCH&&dw>0&&dh>0){
        fprintf(f,"aspect_ratio_index = \"23\"\ncustom_viewport_x = \"0\"\ncustom_viewport_y = \"0\"\ncustom_viewport_width = \"%d\"\ncustom_viewport_height = \"%d\"\n",dw,dh);
    }else if(mode==GAME_PIXEL){
        fprintf(f,"aspect_ratio_index = \"23\"\ncustom_viewport_x = \"%d\"\ncustom_viewport_y = \"%d\"\ncustom_viewport_width = \"%d\"\ncustom_viewport_height = \"%d\"\n",
            (dw-sw)/2,(dh-sh)/2,sw,sh);
    }else{
        fputs("aspect_ratio_index = \"22\"\ncustom_viewport_x = \"0\"\ncustom_viewport_y = \"0\"\ncustom_viewport_width = \"0\"\ncustom_viewport_height = \"0\"\n",f);
    }
    fprintf(stderr,"Game scaling: %s; native=%dx%d output=%dx%d\n",game_view_names[mode],sw,sh,dw,dh);
}
/* Steam Link: the Marvell display plane scales the native framebuffer to the
 * whole TV in hardware, at no GPU cost. Its SDL (both the system SDL3 and the
 * bundled native SDL2) reads SDL_PE_GFX_RESOLUTION at video init, before any
 * window exists. Without it the framebuffer stays 1280x720 and a smaller
 * RetroArch window is drawn unscaled in a corner. libGAL accepts only these
 * framebuffer sizes (640x360 and 320x240 are rejected and fail video init). */
static inline int game_hw_framebuffer(int output,int *w,int *h){
    if(output==240||output==480){*w=640;*h=480;return 0;}
    if(output==540){*w=960;*h=540;return 0;}
    if(output==720){*w=1280;*h=720;return 0;}
    if(output==1080){*w=1920;*h=1080;return 0;}
    return -1;
}
/* FLXTR_HW_SCALER=1/0 forces the path (host tests); otherwise only a 32-bit
 * ARM Linux build running on a Steam Link (Vivante /dev/galcore, /home/steam). */
static inline int game_hw_scaler(void){
    const char *forced=getenv("FLXTR_HW_SCALER");
    if(forced&&*forced)return strcmp(forced,"0")!=0;
#if defined(__linux__)&&defined(__arm__)&&!defined(__ANDROID__)&&!defined(FLXTR_DESKTOP)
    return !access("/dev/galcore",F_OK)&&!access("/home/steam",F_OK);
#else
    return 0;
#endif
}
/* The TV mode is always 16:9 on Steam Link; SDL only sees the framebuffer. */
#define GAME_HW_PANEL_ASPECT (16.0/9.0)
static inline int game_round(double v){return (int)(v+0.5);}
/* Viewport inside a framebuffer of fw x fh that the panel stretches to
 * panel_aspect. Pixels are non-square when the two aspects differ (640x480
 * on 16:9: each pixel shows 4/3 wide), so RetroArch's own square-pixel
 * "core provided" and integer modes would stretch the picture. aspect is the
 * core's display aspect; <=0 means base_w/base_h. Returns -1 when 1:1 does
 * not fit, like the square-pixel path. */
static inline int game_hw_viewport(int mode,int fw,int fh,double panel_aspect,int bw,int bh,double aspect,
                                   int *x,int *y,int *w,int *h){
    if(fw<1||fh<1||panel_aspect<=0)return -1;
    double par=panel_aspect/((double)fw/fh);
    if(aspect<=0)aspect=bw>0&&bh>0?(double)bw/bh:4.0/3.0;
    double shape=aspect/par; /* viewport width/height in framebuffer pixels */
    int vw=fw,vh=fh;
    if(mode==GAME_PIXEL){
        if(bw<1||bh<1)return -1;
        vw=game_round(bw/par);vh=bh; /* one core pixel per framebuffer row, square on the TV */
        if(vw>fw||vh>fh)return -1;
    }else if(mode==GAME_INTEGER&&bh>0&&bh<=fh){
        int k=fh/bh;
        while(k>1&&game_round(k*bh*shape)>fw)k--;
        vh=k*bh;vw=game_round(vh*shape);
        if(vw>fw){vw=fw;vh=game_round(fw/shape);}
    }else if(mode!=GAME_STRETCH){
        if(shape>=(double)fw/fh){vw=fw;vh=game_round(fw/shape);}
        else{vh=fh;vw=game_round(fh*shape);}
    }
    if(vw<1)vw=1;
    if(vh<1)vh=1;
    *w=vw;*h=vh;*x=(fw-vw)/2;*y=(fh-vh)/2;
    return 0;
}
static void game_hw_scaling_config(FILE *f,int mode,int fw,int fh,int x,int y,int w,int h){
    fprintf(f,"video_force_aspect = \"false\"\nvideo_scale_integer = \"false\"\nvideo_crop_overscan = \"false\"\nauto_overrides_enable = \"false\"\n"
              "aspect_ratio_index = \"23\"\ncustom_viewport_x = \"%d\"\ncustom_viewport_y = \"%d\"\ncustom_viewport_width = \"%d\"\ncustom_viewport_height = \"%d\"\n",
        x,y,w,h);
    fprintf(stderr,"Game scaling: %s; hardware-scaled framebuffer %dx%d to 16:9, viewport %dx%d at %d,%d\n",
        game_view_names[mode],fw,fh,w,h,x,y);
}
#endif
#endif
