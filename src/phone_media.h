#ifndef FLXTR_PHONE_MEDIA_H
#define FLXTR_PHONE_MEDIA_H
#include <ctype.h>
#include <string.h>
#include "platform_paths.h"
static int phone_local_media(const char *path){
    if(!path||strncmp(path,"phone-files/",12)||!path[12]||path[12]=='.'||strstr(path+12,".."))return 0;
    for(const char *p=path+12;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='.'&&*p!='_'&&*p!='-')return 0;
    return flxtr_regular_file_nofollow(path);
}
#endif
