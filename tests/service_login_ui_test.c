#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char directory[]="/tmp/flxtr-login-XXXXXX",previous[PATH_MAX];
    assert(getcwd(previous,sizeof(previous)));
    assert(mkdtemp(directory));assert(!chdir(directory));
    assert(!SDL_Init(SDL_INIT_TIMER));
    assert(!service_login_poll());
    service_login_required("subdl");service_login_required("youtube");
    assert(service_login_poll());assert(accounts_on);
    assert(flxtr_socket_valid(api_setup.listener));
    assert(strstr(api_setup.message,"SUBDL LOGIN REQUIRED"));
    assert(qrcodegen_getSize(accounts_qr)>0);
    assert(access(service_login_path("subdl"),F_OK));
    assert(!service_login_poll()); /* No replacement of an open modal. */
    action(SDLK_ESCAPE);assert(!accounts_on);
    assert(!flxtr_socket_valid(api_setup.listener));
    assert(service_login_poll());assert(accounts_on);
    assert(strstr(api_setup.message,"YOUTUBE LOGIN REQUIRED"));
    assert(access(service_login_path("youtube"),F_OK));
    action(SDLK_ESCAPE);assert(!service_login_poll());
    SDL_Quit();assert(!chdir(previous));assert(!rmdir(directory));
    puts("PASS: automatic QR prompt, queued providers, dismissal and listener cleanup");
}
