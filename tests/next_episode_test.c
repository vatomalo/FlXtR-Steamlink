#define main legacy_menu_test_main
#include "playback_menu_test.c"
#undef main
int main(void){
 char temp[]="/tmp/flxtr-next-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
 menu_episodes=0;menu_command('n');assert(!stopped);
 menu_episodes=1;menu_open=0;menu_command('n');assert(request(6)==0);
 menu_open=1;menu_command('n');assert(request(6)==0);
 chdir("/tmp");rmdir(temp);puts("X next episode works with menu open/closed; movies ignore it");return 0;
}
