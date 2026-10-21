#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void wait_service(void){
    Uint32 end=SDL_GetTicks()+10000;
    while(samba_pid&&(Sint32)(SDL_GetTicks()-end)<0){samba_poll();SDL_Delay(10);}
    assert(!samba_pid);
}
int main(void){
    char temp[]="/tmp/flxtr-samba-ui-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    FILE *f=fopen("samba-server.sh","w");assert(f);
    fputs("#!/bin/sh\nset -eu\ncase \"$1\" in\n"
        "disable) rm -f samba/.enabled;;\n"
        "configure) IFS= read -r a; IFS= read -r b; [ \"$a\" = 'DummyPassword123' ] && [ \"$a\" = \"$b\" ]; mkdir -p samba; touch samba/.configured;;\n"
        "enable) test -f samba/.configured; touch samba/.enabled;;\nesac\n",f);fclose(f);
    samba_password_text("DummyPassword123");samba_service("configure");
    memset(samba_password,0,sizeof(samba_password));assert(samba_pid>0);wait_service();
    assert(!access("samba/.configured",R_OK)&&!access("samba/.enabled",R_OK));
    assert(strstr(samba_message,"SAMBA ON"));
    f=fopen("samba-service.log","r");assert(f);char log[256]={0};size_t n=fread(log,1,sizeof(log)-1,f);(void)n;fclose(f);
    assert(!strstr(log,"DummyPassword"));
    samba_service("disable");assert(samba_pid>0);wait_service();assert(access("samba/.enabled",R_OK));
    unlink("samba/.configured");rmdir("samba");unlink("samba-service.log");unlink("samba-server.sh");chdir("/tmp");rmdir(temp);
    puts("PASS: UI service worker delivers password via stdin, configures/enables, disables and keeps logs free of credentials");
    return 0;
}
