#ifndef FLXTR_API_SETUP_H
#define FLXTR_API_SETUP_H
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* Exists only while the API accounts screen is open. No daemon or passwords. */
static struct {
    int listener,client;
    uint32_t started,connected;
    size_t used,sent,length;
    char host[64],path[40],url[128],input[4096],output[6144],message[80];
} api_setup={.listener=-1,.client=-1};

static void api_client_close(void){
    if(api_setup.client>=0)close(api_setup.client);
    api_setup.client=-1;api_setup.used=api_setup.sent=api_setup.length=0;
    memset(api_setup.input,0,sizeof(api_setup.input));
    memset(api_setup.output,0,sizeof(api_setup.output));
}
static void api_setup_close(void){
    api_client_close();if(api_setup.listener>=0)close(api_setup.listener);
    api_setup.listener=-1;api_setup.url[0]=api_setup.path[0]=0;
}
static int api_socket_flags(int fd){
    return fcntl(fd,F_SETFD,FD_CLOEXEC)<0||fcntl(fd,F_SETFL,O_NONBLOCK)<0?-1:0;
}
static int api_lan_ip(char *out,size_t size){
    char preferred[64]="",row[256];FILE *routes=fopen("/proc/net/route","r");
    if(routes){while(fgets(row,sizeof(row),routes)){
        char name[64];unsigned long destination,gateway;unsigned flags;
        if(sscanf(row,"%63s %lx %lx %x",name,&destination,&gateway,&flags)==4&&destination==0&&(flags&1)){
            snprintf(preferred,sizeof(preferred),"%s",name);break;
        }
    }fclose(routes);}
    struct ifaddrs *list=NULL;if(getifaddrs(&list))return -1;
    int result=-1;
    for(struct ifaddrs *p=list;p;p=p->ifa_next){
        if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET)continue;
        if(!strcmp(p->ifa_name,"lo")||!strcmp(p->ifa_name,"lo0"))continue;
        struct sockaddr_in *a=(struct sockaddr_in*)p->ifa_addr;
        uint32_t ip=ntohl(a->sin_addr.s_addr);
        if((ip>>24)!=10&&(ip>>20)!=0xac1&&(ip>>16)!=0xc0a8)continue;
        if(result==0&&strcmp(p->ifa_name,preferred))continue;
        if(inet_ntop(AF_INET,&a->sin_addr,out,size)){result=0;if(!strcmp(p->ifa_name,preferred))break;}
    }
    freeifaddrs(list);return result;
}
static int api_setup_start(const char *ip,uint32_t now){
    api_setup_close();strcpy(api_setup.message,"SCAN WITH A PHONE ON THE SAME NETWORK");
    unsigned char token[16];int random=open("/dev/urandom",O_RDONLY);
    if(random<0)return -1;
    ssize_t got=read(random,token,sizeof(token));close(random);
    if(got!=(ssize_t)sizeof(token))return -1;
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;
    if(inet_pton(AF_INET,ip,&addr.sin_addr)!=1)return -1;
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
    if(api_socket_flags(fd)||bind(fd,(struct sockaddr*)&addr,sizeof(addr))||listen(fd,2)){close(fd);return -1;}
    socklen_t n=sizeof(addr);
    if(getsockname(fd,(struct sockaddr*)&addr,&n)){close(fd);return -1;}
    snprintf(api_setup.host,sizeof(api_setup.host),"%s:%u",ip,ntohs(addr.sin_port));
    api_setup.path[0]='/';
    for(int i=0;i<16;i++)snprintf(api_setup.path+1+i*2,3,"%02X",token[i]);
    strcat(api_setup.path,"/");
    snprintf(api_setup.url,sizeof(api_setup.url),"http://%s%s",api_setup.host,api_setup.path);
    api_setup.listener=fd;api_setup.started=now;return 0;
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
    int fd=mkstemp(temp);if(fd<0)return -2;
    int ok=fchmod(fd,0600)==0&&write(fd,key,n)==(ssize_t)n&&fsync(fd)==0;
    if(close(fd))ok=0;
    memset(key,0,sizeof(key));
    if(!ok||rename(temp,path)){unlink(temp);return -2;}
    snprintf(api_setup.message,sizeof(api_setup.message),"%s KEY SAVED - NOT YET VERIFIED",!strcmp(field,"subdl")?"SUBDL":"THEGAMESDB");
    return 0;
}
static void api_reply(int code,const char *body){
    int n=snprintf(api_setup.output,sizeof(api_setup.output),
        "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n\r\n%s",
        code,code==200?"OK":"Error",strlen(body),body);
    api_setup.length=n>0&&(size_t)n<sizeof(api_setup.output)?(size_t)n:0;
    memset(api_setup.input,0,sizeof(api_setup.input));
}
static void api_page(const char *message){
    char body[4800];
    snprintf(body,sizeof(body),"<!doctype html><html lang=en><meta name=viewport content='width=device-width,initial-scale=1'><title>FlXtR API accounts</title>"
        "<style>body{background:#101510;color:#eee;font:18px system-ui;max-width:36em;margin:2em auto;padding:0 1em}a{color:#8fea94}input,button{font:inherit;padding:.7em;max-width:95%%}input{display:block;width:90%%;margin:.8em 0}section{margin:2em 0}button{background:#8fea94;border:0;border-radius:.3em}</style>"
        "<h1>FlXtR API accounts</h1><p>%s</p><p>Use a trusted home network: this local setup page uses HTTP. It closes when you leave API accounts on the TV, or after 10 minutes.</p>"
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
    if(fields!=3||strcmp(path,api_setup.path)||(strcmp(version,"HTTP/1.1")&&strcmp(version,"HTTP/1.0"))){api_reply(403,"Setup link is invalid or expired.");return;}
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
            if(content++||!isdigit((unsigned char)*value)||*tail||parsed>512){*next='\r';api_reply(400,"Invalid length");return;}length=(size_t)parsed;
        }else if(!strncasecmp(p,"Content-Type:",13))type=!strcasecmp(p+13+strspn(p+13," \t"),"application/x-www-form-urlencoded");
        else if(!strncasecmp(p,"Transfer-Encoding:",18)){*next='\r';api_reply(400,"Unsupported encoding");return;}
        *next='\r';p=next+2;
    }
    if(!host){api_reply(403,"Missing host");return;}
    if(!strcmp(method,"GET")){api_page("Sign in on the provider website, then return here to paste your key.");return;}
    if(strcmp(method,"POST")||!content||!type||!length){api_reply(400,"Use the API key form.");return;}
    size_t offset=(size_t)(end+4-api_setup.input);
    if(api_setup.used<offset+length)return;
    if(api_setup.used!=offset+length||memchr(end+4,0,length)){api_reply(400,"Invalid body");return;}
    char *body=end+4,*equals=strchr(body,'=');
    if(!equals||strchr(body,'&')){api_reply(400,"Submit one key at a time.");return;}
    *equals=0;int result=api_key_save(body,equals+1);
    if(result==-1)api_reply(400,"Invalid key. Paste only the API key, without spaces. Go back to retry.");
    else if(result)api_reply(500,"Could not save key. Existing key was kept.");
    else api_page("Key saved on your device. Provider access has not yet been verified.");
}
static void api_setup_tick(uint32_t now){
    if(api_setup.listener<0)return;
    if(now-api_setup.started>=600000){api_setup_close();strcpy(api_setup.message,"SETUP EXPIRED - PRESS A FOR A NEW CODE");return;}
    if(api_setup.client<0){
        api_setup.client=accept(api_setup.listener,NULL,NULL);
        if(api_setup.client<0)return;
        if(api_socket_flags(api_setup.client)){api_client_close();return;}api_setup.connected=now;
    }
    if(now-api_setup.connected>=5000){api_client_close();return;}
    if(!api_setup.length){
        ssize_t got=recv(api_setup.client,api_setup.input+api_setup.used,sizeof(api_setup.input)-1-api_setup.used,0);
        if(got==0||(got<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){api_client_close();return;}
        if(got>0){api_setup.used+=(size_t)got;api_setup.input[api_setup.used]=0;api_request();
            if(!api_setup.length&&api_setup.used==sizeof(api_setup.input)-1)api_reply(413,"Request too large");}
    }
    if(api_setup.length){
        ssize_t sent=send(api_setup.client,api_setup.output+api_setup.sent,api_setup.length-api_setup.sent,MSG_NOSIGNAL);
        if(sent>0)api_setup.sent+=(size_t)sent;
        if(api_setup.sent==api_setup.length||(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK))api_client_close();
    }
}
#endif
