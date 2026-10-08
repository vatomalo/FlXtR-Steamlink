#include "app.h"
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <psputility.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
bool network_cancelled(void){
    SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);
    return atomic_load(&quitting) || (pad.Buttons&PSP_CTRL_CIRCLE);
}
int connect_wifi(int profile){
    static bool initialized;
    if(!initialized){
        int r=sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
        if(r<0){status("Network module: %08x",r);return -1;}
        sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
        if(sceNetInit(256*1024,0x30,4096,0x30,4096)<0 || sceNetInetInit()<0 ||
           sceNetResolverInit()<0 || sceNetApctlInit(0x1800,0x30)<0){status("Network initialization failed");return -1;}
        curl_global_init(CURL_GLOBAL_DEFAULT);initialized=true;
    }
    int state=0;sceNetApctlGetState(&state);if(state==4)return 0;
    sceNetApctlDisconnect();sceKernelDelayThread(100000);
    int r=sceNetApctlConnect(profile);if(r<0){status("Wi-Fi profile %d failed: %08x",profile,r);return -1;}
    for(int i=0;i<300 && !atomic_load(&quitting);i++){
        sceNetApctlGetState(&state);if(state==4){status("Wi-Fi connected");return 0;}
        SceCtrlData p;sceCtrlPeekBufferPositive(&p,1);if(p.Buttons&PSP_CTRL_CIRCLE)break;
        sceKernelDelayThread(100000);
    }sceNetApctlDisconnect();status("Wi-Fi timeout. Check saved connection/profile.");return -1;
}
