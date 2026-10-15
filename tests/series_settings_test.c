#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
 char temp[]="/tmp/flxtr-series-settings-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
 assert(!series_autoplay);series_autoplay=1;save_settings();series_autoplay=0;load_settings();assert(series_autoplay==1);
 series_autoplay=0;save_settings();series_autoplay=1;load_settings();assert(!series_autoplay);
 int fd[2];assert(!pipe(fd));player_pid=99999;player_menu=1;player_control=fd[1];action(SDLK_F2);char c;assert(read(fd[0],&c,1)==1&&c=='n');close(fd[0]);close(fd[1]);player_pid=0;player_control=-1;
 DIR *d=opendir(".");struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')unlink(e->d_name);closedir(d);chdir("/tmp");rmdir(temp);
 puts("Autoplay defaults off, persists on/off, X sends next-episode command");return 0;
}
