#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <time.h>
#include "../src/api_setup.h"
int main(int argc,char **argv){
    char ip[INET_ADDRSTRLEN];(void)api_lan_ip(ip,sizeof(ip));
    assert(api_setup_start("127.0.0.1",0)==0);
    if(argc>1&&!strcmp(argv[1],"expire")){
        api_setup_tick(600000);assert(api_setup.listener==FLXTR_INVALID_SOCKET&&api_setup.url[0]==0);return 0;
    }
    puts(api_setup.url);fflush(stdout);
#ifndef _WIN32
    struct timespec delay={0,1000000};
#endif
    for(uint32_t tick=0;tick<90000;tick++){api_setup_tick(tick);
#ifdef _WIN32
        Sleep(1);
#else
        nanosleep(&delay,NULL);
#endif
    }
    api_setup_close();return 0;
}
