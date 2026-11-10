#define FLXTR_DESKTOP
#define FLXTR_USB_SETUP_TEST
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void file(const char *path,const char *value){FILE *f=fopen(path,"w");assert(f);fputs(value,f);assert(!fclose(f));}
static void poll_job(void){
    Uint32 start=SDL_GetTicks();while(usb_pid){usb_poll();assert(SDL_GetTicks()-start<5000);SDL_Delay(2);}
}
int main(void){
    char dir[]="/tmp/flxtr-usb-ui-test-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    assert(!mkdir("tools",0700));assert(!mkdir("scripts",0700));file("tools/usb_setup.py","fixture only\n");
    /* All jobs go to this stub. No real storage helper or device is executed. */
    file("scripts/music-runtime.sh","#!/bin/sh\ncase \"$2\" in\nscan) printf '/dev/block/sdb\\tFixture USB\\t32 GiB\\tSERIAL\\tfixture-id\\tLOCKED\\tActive apps\\tusb-port-2\\n';;\nplan) printf 'identity\\tfixture-id\\nconfirmation\\tERASE-TEST\\nlayout\\tFixture layout\\nactivate\\t0\\ndirections\\tUP DOWN LEFT RIGHT\\n';;\nexecute) touch executed;;\nesac\n");
    settings_on=1;settings_tab=SETTINGS_SERVICES;settings_row=5;assert(settings_id()==30);
    action(SDLK_RETURN);assert(usb_on&&usb_operation==0&&usb_pid>0);poll_job();assert(usb_stage==2&&usb_count==1&&usb_disks[0].locked);
    char reply[256];assert(phone_dispatch(reply,sizeof(reply))==409);
    usb_action(SDLK_RIGHT);assert(usb_operation==1);usb_action(SDLK_RETURN);assert(!usb_pid&&usb_stage==2&&access("executed",F_OK));
    usb_disks[0].locked=0;usb_action(SDLK_RETURN);poll_job();assert(usb_stage==3&&!usb_continue);
    usb_draw();usb_action(SDLK_RETURN);assert(usb_stage==2&&access("executed",F_OK));
    usb_action(SDLK_RETURN);poll_job();usb_action(SDLK_RIGHT);usb_action(SDLK_RETURN);assert(usb_stage==4&&!usb_code);
    usb_draw();usb_action(SDLK_RETURN);assert(!usb_pid&&access("executed",F_OK));
    for(int i=0;i<4;i++)usb_action(usb_directions[i]);
    assert(usb_code==4);
    usb_action(SDLK_RETURN);assert(usb_stage==5&&usb_pid>0);
    desktop_key(SDLK_q,KMOD_CTRL);assert(running&&usb_on);usb_action(SDLK_ESCAPE);assert(usb_on&&usb_stage==5);
    usb_draw();poll_job();assert(usb_stage==6&&usb_success&&!usb_activate&&!access("executed",F_OK));
    usb_action(SDLK_ESCAPE);assert(!usb_on);
    close_ui();SDL_Quit();
    puts("PASS: Services entry, safe restore default, active-disk lock, default cancel, erase code, fake execution, navigation lock and phone exclusion");
}
