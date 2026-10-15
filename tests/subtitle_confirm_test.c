#define main legacy_menu_test_main
#include "playback_menu_test.c"
#undef main
int main(void){
 char temp[]="/tmp/flxtr-confirm-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
 menu_subtitles=1;menu_open=1;menu_row=2;
 menu_command('a');menu_command('a');assert(!stopped&&menu_subtitles==1&&subtitle_pending==3);
 assert(access("playback-request",F_OK));menu_command('b');assert(request(1)==10);assert(menu_subtitles==3&&subtitle_pending==-1);
 menu_open=1;menu_row=2;menu_command('a');assert(subtitle_pending==4);menu_command('m');assert(!menu_open&&!stopped&&subtitle_pending==-1&&menu_subtitles==3);
 menu_open=1;menu_episodes=1;menu_command('n');assert(request(6)==0);
 chdir("/tmp");rmdir(temp);puts("Subtitle staging: no intermediate restart, B applies, M cancels, next episode retained");return 0;
}
