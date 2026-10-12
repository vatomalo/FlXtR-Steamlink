#define main legacy_menu_main
#include "playback_menu_test.c"
#undef main
int main(void){
    char temp[]="/tmp/flxtr-phone-player-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    media_duration=100;assert(playback_position()==10);
    menu_command('P');assert(paused);now+=3000000;assert(playback_position()==10);
    menu_command('P');assert(!paused&&playback_position()==10);
    menu_command('F');assert(request(1)==20);
    menu_command('R');assert(request(1)==0);
    media_duration=15;menu_command('F');assert(request(1)==14);
    menu_command('S');assert(request(7)==10);
    int pipefd[2];assert(!pipe(pipefd));control_fd=pipefd[0];assert(!fcntl(control_fd,F_SETFL,O_NONBLOCK));
    assert(write(pipefd[1],"PP",2)==2);menu_poll();assert(!paused);
    close(pipefd[0]);close(pipefd[1]);assert(!chdir("/tmp"));rmdir(temp);
    puts("PASS: phone pause/resume, seek bounds, stop and live control pipe");return 0;
}
