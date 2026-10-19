#ifndef FLXTR_API_SETUP_H
#define FLXTR_API_SETUP_H
#include "platform_net.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "phone_features.h"

/* Exists only while the API accounts screen is open. No daemon or passwords. */
static struct {
    FlxtrSocket listener,client;
    int network_active,draining;
    uint32_t drain_started;
    uint32_t started,connected;
    size_t used,sent,length;
    char host[64],path[40],url[128],input[4096],output[16384],message[80];
} api_setup={.listener=FLXTR_INVALID_SOCKET,.client=FLXTR_INVALID_SOCKET};

static void api_client_close(void){
    phone_upload_abort();
    if(flxtr_socket_valid(api_setup.client))flxtr_socket_close(api_setup.client);
    api_setup.client=FLXTR_INVALID_SOCKET;api_setup.used=api_setup.sent=api_setup.length=0;api_setup.draining=0;
    memset(api_setup.input,0,sizeof(api_setup.input));
    memset(api_setup.output,0,sizeof(api_setup.output));
}
static void api_setup_close(void){
    api_client_close();if(flxtr_socket_valid(api_setup.listener))flxtr_socket_close(api_setup.listener);
    api_setup.listener=FLXTR_INVALID_SOCKET;
    if(api_setup.network_active){flxtr_network_stop();api_setup.network_active=0;}api_setup.url[0]=api_setup.path[0]=0;
    memset(&phone_party,0,sizeof(phone_party));phone_pending[0]=0;
}
static int api_socket_flags(FlxtrSocket fd){return flxtr_socket_flags(fd);}
static int api_lan_ip(char *out,size_t size){
    if(flxtr_network_start())return -1;
    int result=flxtr_lan_ipv4(out,size);flxtr_network_stop();return result;
}
static int api_setup_start(const char *ip,uint32_t now){
    api_setup_close();strcpy(api_setup.message,"SCAN WITH A PHONE ON THE SAME NETWORK");
    if(flxtr_network_start())return -1;
    api_setup.network_active=1;
    unsigned char token[16];if(flxtr_random_bytes(token,sizeof(token)))goto fail;
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;
    if(inet_pton(AF_INET,ip,&addr.sin_addr)!=1)goto fail;
    FlxtrSocket fd=socket(AF_INET,SOCK_STREAM,0);
    if(!flxtr_socket_valid(fd))goto fail;
    if(api_socket_flags(fd)||bind(fd,(struct sockaddr*)&addr,sizeof(addr))||listen(fd,2)){
        flxtr_socket_close(fd);goto fail;
    }
    FlxtrSocklen n=sizeof(addr);
    if(getsockname(fd,(struct sockaddr*)&addr,&n)){flxtr_socket_close(fd);goto fail;}
    snprintf(api_setup.host,sizeof(api_setup.host),"%s:%u",ip,ntohs(addr.sin_port));
    api_setup.path[0]='/';
    for(int i=0;i<16;i++)snprintf(api_setup.path+1+i*2,3,"%02X",token[i]);
    strcat(api_setup.path,"/");
    snprintf(api_setup.url,sizeof(api_setup.url),"http://%s%s",api_setup.host,api_setup.path);
    api_setup.listener=fd;api_setup.started=now;return 0;
fail:
    api_setup_close();return -1;
}
static int api_key_save(const char *field,const char *encoded){
    const char *path;size_t max;
    if(!strcmp(field,"subdl")){path="subdl-api-key.txt";max=200;}
    else if(!strcmp(field,"gamesdb")){path="thegamesdb.key";max=120;}
    else return -1;
    char key[202];size_t n=0;
    for(size_t i=0;encoded[i];i++){
        unsigned char c=(unsigned char)encoded[i];
        if(c=='%'){
            if(!encoded[i+1]||!encoded[i+2]||!isxdigit((unsigned char)encoded[i+1])||!isxdigit((unsigned char)encoded[i+2]))return -1;
            char hex[3]={encoded[i+1],encoded[i+2],0};c=(unsigned char)strtoul(hex,NULL,16);i+=2;
        }
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')||n>=max)return -1;
        key[n++]=(char)c;
    }
    if(n<8)return -1;
    key[n++]='\n';char temp[64];snprintf(temp,sizeof(temp),"%s.setup-XXXXXX",path);
    int fd=flxtr_temp_file(temp,sizeof(temp));if(fd<0)return -2;
    int ok=flxtr_fd_write(fd,key,n)==(ptrdiff_t)n&&flxtr_fd_sync(fd)==0;
    if(flxtr_fd_close(fd))ok=0;
    memset(key,0,sizeof(key));
    if(!ok||flxtr_replace_file(temp,path)){flxtr_path_unlink(temp);return -2;}
    snprintf(api_setup.message,sizeof(api_setup.message),"%s KEY SAVED - NOT YET VERIFIED",!strcmp(field,"subdl")?"SUBDL":"THEGAMESDB");
    return 0;
}
static void api_reply_type(int code,const char *body,const char *type){
    int n=snprintf(api_setup.output,sizeof(api_setup.output),
        "HTTP/1.1 %d %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: same-origin\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n\r\n%s",
        code,code==200?"OK":"Error",type,strlen(body),body);
    api_setup.length=n>0&&(size_t)n<sizeof(api_setup.output)?(size_t)n:0;
    memset(api_setup.input,0,sizeof(api_setup.input));
}
static void api_reply(int code,const char *body){api_reply_type(code,body,"text/html");}
static void api_asset(const char *path,const char *type){
    char content[12000];FILE *f=fopen(path,"r");if(!f){api_reply(404,"Page unavailable");return;}
    size_t n=fread(content,1,sizeof(content)-1,f);fclose(f);content[n]=0;api_reply_type(200,content,type);
}
static void api_page(const char *message){
    char body[4800];
    snprintf(body,sizeof(body),"<!doctype html><html lang=en><meta name=viewport content='width=device-width,initial-scale=1'><title>FlXtR API accounts</title>"
        "<style>body{background:#101510;color:#eee;font:18px system-ui;max-width:36em;margin:2em auto;padding:0 1em}a{color:#8fea94}input,button{font:inherit;padding:.7em;max-width:95%%}input{display:block;width:90%%;margin:.8em 0}section{margin:2em 0}button{background:#8fea94;border:0;border-radius:.3em}</style>"
        "<h1>FlXtR API accounts</h1><p><a href='./phone'>Phone remote, uploads and party quiz</a></p><p>%s</p><p>Use a trusted home network: this local setup page uses HTTP. It closes when you leave phone setup on the TV, or after 10 minutes without a request.</p>"
        "<section><h2>SubDL</h2><p>Saved on this device: %s</p><p><a href='https://subdl.com' target=_blank rel='noopener noreferrer'>Sign up / log in to SubDL</a>. Open your account panel and copy an API key. <a href='https://subdl.com/api-doc' target=_blank rel='noopener noreferrer'>API help</a></p>"
        "<form method=post action='%s'><label>SubDL API key<input type=password name=subdl minlength=8 maxlength=200 required autocomplete=off autocapitalize=none spellcheck=false></label><button>Save SubDL key</button></form></section>"
        "<section><h2>TheGamesDB</h2><p>Saved on this device: %s</p><p><a href='https://api.thegamesdb.net/key.php' target=_blank rel='noopener noreferrer'>Log in / request API access</a>. Follow the provider's account and API access steps, then copy your public API key.</p>"
        "<form method=post action='%s'><label>TheGamesDB public API key<input type=password name=gamesdb minlength=8 maxlength=120 required autocomplete=off autocapitalize=none spellcheck=false></label><button>Save TheGamesDB key</button></form></section>"
        "<p>Keys stay on this device and are used for provider requests. Never paste account passwords here. Saving does not verify provider access.</p></html>",
        message,access("subdl-api-key.txt",R_OK)?"No":"Yes",api_setup.path,access("thegamesdb.key",R_OK)?"No":"Yes",api_setup.path);
    api_reply(200,body);
}
static void api_request(void){
    char *end=strstr(api_setup.input,"\r\n\r\n");if(!end)return;
    char *line=strstr(api_setup.input,"\r\n");if(!line){api_reply(400,"Bad request");return;}
    char method[8],path[160],version[16],extra;
    *line=0;int fields=sscanf(api_setup.input,"%7s %159s %15s %c",method,path,version,&extra);*line='\r';
    size_t base=strlen(api_setup.path);
    if(fields!=3||strncmp(path,api_setup.path,base)||(strcmp(version,"HTTP/1.1")&&strcmp(version,"HTTP/1.0"))){api_reply(403,"Setup link is invalid or expired.");return;}
    const char *route=path+base;int upload=!strncmp(route,"upload/",7);
    int host=0,content=0,type=0;size_t length=0;
    for(char *p=line+2;p<end;){
        char *next=strstr(p,"\r\n");if(!next||next>end)break;
        *next=0;
        if(!strncasecmp(p,"Host:",5)){if(host++||strcmp(p+5+strspn(p+5," \t"),api_setup.host)){*next='\r';api_reply(403,"Invalid host");return;}}
        else if(!strncasecmp(p,"Origin:",7)){
            char expected[80];snprintf(expected,sizeof(expected),"http://%s",api_setup.host);
            if(strcmp(p+7+strspn(p+7," \t"),expected)){*next='\r';api_reply(403,"Invalid origin");return;}
        }else if(!strncasecmp(p,"Content-Length:",15)){
            char *tail;const char *value=p+15+strspn(p+15," \t");
            unsigned long parsed=strtoul(value,&tail,10);
            if(content++||!isdigit((unsigned char)*value)||*tail||parsed>(upload?512u*1024u*1024u:3072u)){*next='\r';api_reply(400,"Invalid length");return;}length=(size_t)parsed;
        }else if(!strncasecmp(p,"Content-Type:",13)){
            const char *actual=p+13+strspn(p+13," \t"),*expected=upload?"application/octet-stream":"application/x-www-form-urlencoded";
            size_t count=strlen(expected);type=!strncasecmp(actual,expected,count)&&(!actual[count]||actual[count]==';');
        }
        else if(!strncasecmp(p,"Transfer-Encoding:",18)){*next='\r';api_reply(400,"Unsupported encoding");return;}
        *next='\r';p=next+2;
    }
    if(!host){api_reply(403,"Missing host");return;}
    api_setup.started=phone_now;
    if(!strcmp(method,"GET")){
        if(!*route)api_page("Sign in on the provider website, then return here to paste your key.");
        else if(!strcmp(route,"phone"))api_asset("assets/phone.html","text/html");
        else if(!strcmp(route,"app.js"))api_asset("assets/phone.js","application/javascript");
        else if(!strcmp(route,"state")){char body[6000];phone_state(body,sizeof(body));api_reply_type(200,body,"application/json");}
        else api_reply(404,"Not found");
        return;
    }
    if(strcmp(method,"POST")||!content||!type||!length){api_reply(400,"Use the API key form.");return;}
    size_t offset=(size_t)(end+4-api_setup.input);
    if(upload){
        int result=phone_upload_begin(route+7,length);
        if(result){api_reply(result==-3?409:result==-2?507:400,"Upload refused: check file type, name, free space and whether it exists.");return;}
        result=phone_upload_write(end+4,api_setup.used-offset);
        memset(api_setup.input,0,sizeof(api_setup.input));api_setup.used=0;
        if(result<0){phone_upload_abort();api_reply(500,"Upload failed");}
        else if(result==1)api_reply(200,"File saved");
        return;
    }
    if(*route){api_reply(404,"Not found");return;}
    if(api_setup.used<offset+length)return;
    if(api_setup.used!=offset+length||memchr(end+4,0,length)){api_reply(400,"Invalid body");return;}
    char *body=end+4,*equals=strchr(body,'=');
    if(!equals||strchr(body,'&')){api_reply(400,"Submit one key at a time.");return;}
    *equals=0;
    if(strcmp(body,"subdl")&&strcmp(body,"gamesdb")){
        char response[160]="Request rejected";int code=phone_command(body,equals+1,response,sizeof(response));
        api_reply_type(code,response,"text/plain");return;
    }
    int result=api_key_save(body,equals+1);
    if(result==-1)api_reply(400,"Invalid key. Paste only the API key, without spaces. Go back to retry.");
    else if(result)api_reply(500,"Could not save key. Existing key was kept.");
    else api_page("Key saved on your device. Provider access has not yet been verified.");
}
static void api_setup_tick(uint32_t now){
    phone_party_tick(now);
    if(!flxtr_socket_valid(api_setup.listener))return;
    if(now-api_setup.started>=600000){api_setup_close();strcpy(api_setup.message,"SETUP EXPIRED - PRESS A FOR A NEW CODE");return;}
    if(!flxtr_socket_valid(api_setup.client)){
        api_setup.client=accept(api_setup.listener,NULL,NULL);
        if(!flxtr_socket_valid(api_setup.client))return;
        if(api_socket_flags(api_setup.client)){api_client_close();return;}api_setup.connected=now;
    }
    if(now-api_setup.connected>=(phone_upload>=0?15000u:5000u)){api_client_close();return;}
    /* Half-close the response before draining unread rejected request bytes.
     * Immediate closesocket with unread input can reset TCP and lose a 4xx
     * response on Windows. Keep draining bounded and nonblocking. */
    if(api_setup.draining){
        char discard[4096];ptrdiff_t count;
        for(int i=0;i<16;i++){
            count=flxtr_socket_recv(api_setup.client,discard,sizeof(discard));
            if(count==0||(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){
                api_client_close();return;
            }
            if(count<0)break;
        }
        if(now-api_setup.drain_started>=500)api_client_close();
        return;
    }
    if(phone_upload>=0){
        char chunk[16384];
        for(int i=0;i<16&&phone_upload>=0;i++){
            ptrdiff_t n=flxtr_socket_recv(api_setup.client,chunk,sizeof(chunk));
            if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
            if(n<=0){api_client_close();return;}
            api_setup.connected=api_setup.started=now;
            int result=phone_upload_write(chunk,(size_t)n);
            if(result<0){phone_upload_abort();api_reply(500,"Upload failed: existing files kept");}
            else if(result==1)api_reply(200,"File saved. ROM uploads appear after importing games.");
        }
    }
    if(!api_setup.length&&phone_upload<0){
        ptrdiff_t got=flxtr_socket_recv(api_setup.client,api_setup.input+api_setup.used,sizeof(api_setup.input)-1-api_setup.used);
        if(got==0||(got<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){api_client_close();return;}
        if(got>0){api_setup.used+=(size_t)got;api_setup.input[api_setup.used]=0;api_request();
            if(!api_setup.length&&api_setup.used==sizeof(api_setup.input)-1)api_reply(413,"Request too large");}
    }
    if(api_setup.length){
        ptrdiff_t sent=flxtr_socket_send(api_setup.client,api_setup.output+api_setup.sent,api_setup.length-api_setup.sent);
        if(sent>0)api_setup.sent+=(size_t)sent;
        if(api_setup.sent==api_setup.length){
            flxtr_socket_shutdown_send(api_setup.client);api_setup.draining=1;api_setup.drain_started=now;
        }else if(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)api_client_close();
    }
}
#endif
