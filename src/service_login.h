#ifndef FLXTR_SERVICE_LOGIN_H
#define FLXTR_SERVICE_LOGIN_H
#include <stdio.h>
#include <string.h>
/* Worker-to-shell notifications contain a provider name only, never a key,
 * URL, response body or cookie. Separate files allow simultaneous failures. */
static inline const char *service_login_path(const char *service){
    if(!strcmp(service,"subdl"))return "subdl-login-required";
    if(!strcmp(service,"gamesdb"))return "gamesdb-login-required";
    if(!strcmp(service,"youtube"))return "youtube-login-required";
    return NULL;
}
static inline void service_login_required(const char *service){
    const char *path=service_login_path(service);if(!path)return;
    FILE *f=fopen(path,"w");if(f)fclose(f);
}
static inline int service_login_response(const char *url,long code,const char *body){
    const char *service=NULL;
    if(!strncmp(url,"https://api.subdl.com/",22))service="subdl";
    if(!strncmp(url,"https://api.thegamesdb.net/",27))service="gamesdb";
    if(!service)return 0;
    /* A generic 403 can mean quota or bot protection. Only explicit key
     * rejection or HTTP 401 should ask the user to sign in again. */
    int rejected=code==401;
    if(body&&(code==200||code==400||code==403)){
        char lower[2048];size_t i;
        for(i=0;body[i]&&i<sizeof(lower)-1;i++){
            unsigned char c=(unsigned char)body[i];lower[i]=(char)(c>='A'&&c<='Z'?c+32:c);
        }
        lower[i]=0;
        int error_body=code!=200||strstr(lower,"\"error\"")||strstr(lower,"\"errors\"")||
            strstr(lower,"\"status\":false")||strstr(lower,"\"status\": false");
        rejected=error_body&&(strstr(lower,"invalid api key")||strstr(lower,"invalid api_key")||
            strstr(lower,"invalid apikey")||strstr(lower,"api key is invalid")||
            strstr(lower,"api key expired")||strstr(lower,"api key is expired")||strstr(lower,"unauthorized"));
    }
    if(rejected)service_login_required(service);
    return rejected;
}
#endif
