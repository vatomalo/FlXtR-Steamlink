#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    Title title={0};
    default_viewing=VIEW_STRETCH;viewing=VIEW_PIXEL;
    subtitle_setting=0;subtitle_scale=3;subtitle_delay=-2;
    handoff_video_settings(&title,"en","https://example.org/en.vtt");
    assert(viewing==VIEW_STRETCH&&subtitle_setting==0&&!title.subs_eng[0]);
    assert(subtitle_scale==3&&subtitle_delay==-2);
    subtitle_setting=3;
    handoff_video_settings(&title,"en","https://example.org/en.vtt");
    assert(subtitle_setting==3&&!title.subs_eng[0]&&!title.subs_nor[0]);
    handoff_video_settings(&title,"no","https://example.org/no.vtt");
    assert(!strcmp(title.subs_nor,"https://example.org/no.vtt")&&subtitle_setting==3);
    subtitle_setting=1;
    handoff_video_settings(&title,"en","https://example.org/en.vtt");
    assert(!strcmp(title.subs_eng,"https://example.org/en.vtt")&&subtitle_setting==1);
    return 0;
}
