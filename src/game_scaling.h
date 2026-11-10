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
#endif
#endif
