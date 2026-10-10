/* On-demand server/quality lookup, executed on the Steam Link itself. */
#define _POSIX_C_SOURCE 200809L
#include "resolver_bridge.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#define BASE "https://plsdontscrapemelove.flixer.gd"
typedef struct {char *data;size_t length,limit;} Buffer;
typedef struct {char label[80],meta[96],url[2048],subs_eng[1024],subs_nor[1024];int height;} Choice;
static Choice choices[40];static int count;
static size_t receive(void *ptr,size_t size,size_t nmemb,void *opaque){
    Buffer *b=opaque;if(size&&nmemb>SIZE_MAX/size)return 0;
    size_t n=size*nmemb;if(n>b->limit-b->length)return 0;
    char *p=realloc(b->data,b->length+n+1);if(!p)return 0;
    b->data=p;memcpy(p+b->length,ptr,n);b->length+=n;p[b->length]=0;return n;
}
static char *request(const char *url,struct curl_slist *headers,int media){
    Buffer b={NULL,0,media?512*1024:2*1024*1024};CURL *c=curl_easy_init();if(!c)return NULL;
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(c,CURLOPT_TIMEOUT,20L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,media?1L:0L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CAINFO,access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem");
    curl_easy_setopt(c,CURLOPT_USERAGENT,RESOLVER_UA);curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,media?"http,https":"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,media?(CURLPROTO_HTTP|CURLPROTO_HTTPS):CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTP|CURLPROTO_HTTPS);
#endif
    CURLcode rc=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);
    if(rc!=CURLE_OK||code!=200){fprintf(stderr,"%s HTTP %ld / error %d\n",media?"Quality list":"Provider",code,(int)rc);free(b.data);return NULL;}
    return b.data;
}
static json_object *parse(const char *s){
    if(!s)return NULL;
    json_tokener *t=json_tokener_new_ex(32);if(!t)return NULL;
    json_object *o=json_tokener_parse_ex(t,s,(int)strlen(s));
    if(json_tokener_get_error(t)!=json_tokener_success){json_object_put(o);o=NULL;}
    json_tokener_free(t);return o;
}
static json_object *field(json_object *o,const char *key){json_object *v=NULL;json_object_object_get_ex(o,key,&v);return v;}
static const char *string(json_object *o,const char *key){json_object *v=field(o,key);return v&&json_object_is_type(v,json_type_string)?json_object_get_string(v):"";}
static int valid_server(const char *s){size_t n=strlen(s);if(!n||n>31)return 0;for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)s[i])&&s[i]!='_'&&s[i]!='-')return 0;return 1;}
static int valid_url(const char *url){if(strlen(url)>=2048||(strncmp(url,"https://",8)&&strncmp(url,"http://",7)))return 0;for(const unsigned char *p=(const unsigned char*)url;*p;p++)if(*p<32||*p==127)return 0;return 1;}
static void base36(uint32_t n,char *out){char rev[16];int k=0;do{rev[k++]="0123456789abcdefghijklmnopqrstuvwxyz"[n%36];n/=36;}while(n);for(int i=0;i<k;i++)out[i]=rev[k-i-1];out[k]=0;}
static void fingerprint(char *out){
    char input[256];snprintf(input,sizeof(input),"1920x1080:24:%.50s:Linux armv7l:en-US:0:%.28s",RESOLVER_UA,RESOLVER_CANVAS+22);
    uint32_t h=0;for(const unsigned char *p=(const unsigned char*)input;*p;p++)h=h*31+*p;
    if(h&0x80000000u)h=0-h;
    base36(h,out);
}
static struct curl_slist *header(struct curl_slist *list,const char *name,const char *value){char line[1024];if(snprintf(line,sizeof(line),"%s: %s",name,value)>=(int)sizeof(line))return list;return curl_slist_append(list,line);}
static json_object *lookup(const char *path,const char *server){
    char *clock=request(BASE"/api/time",NULL,0);json_object *time=parse(clock);free(clock);if(!time)return NULL;
    int64_t timestamp=json_object_get_int64(field(time,"timestamp"));json_object_put(time);if(timestamp<=0)return NULL;
    char *key=resolver_key();if(!key){fprintf(stderr,"Decoder key: %s\n",resolver_error());return NULL;}
    unsigned char random[16],digest[EVP_MAX_MD_SIZE];unsigned digest_len=0;char encoded[32],nonce[24],sig[128],message[1024],ts[32],fp[16],url[1024];
    if(RAND_bytes(random,sizeof(random))!=1){free(key);return NULL;}
    EVP_EncodeBlock((unsigned char*)encoded,random,sizeof(random));int n=0;for(int i=0;encoded[i]&&n<22;i++)if(encoded[i]!='/'&&encoded[i]!='+'&&encoded[i]!='=')nonce[n++]=encoded[i];nonce[n]=0;
    snprintf(ts,sizeof(ts),"%lld",(long long)timestamp);snprintf(message,sizeof(message),"%s:%s:%s:%s",key,ts,nonce,path);
    HMAC(EVP_sha256(),key,(int)strlen(key),(unsigned char*)message,strlen(message),digest,&digest_len);EVP_EncodeBlock((unsigned char*)sig,digest,(int)digest_len);
    fingerprint(fp);snprintf(url,sizeof(url),BASE"%s",path);
    struct curl_slist *headers=NULL;
    headers=header(headers,"Origin","https://flixer.gd");headers=header(headers,"Referer","https://flixer.gd/");
    headers=header(headers,"x-fingerprint-lite","b4f8a1fc72e905d63e");headers=header(headers,"Accept","text/plain");
    headers=header(headers,"X-Api-Key",key);headers=header(headers,"X-Request-Timestamp",ts);headers=header(headers,"X-Request-Nonce",nonce);
    headers=header(headers,"X-Request-Signature",sig);headers=header(headers,"X-Client-Fingerprint",fp);
    if(*server){headers=header(headers,"X-Only-Sources","1");headers=header(headers,"X-Server",server);}
    else headers=header(headers,"bW90aGFmYWth","1");
    char *raw=request(url,headers,0);curl_slist_free_all(headers);char *decoded=raw?resolver_decode(raw,key):NULL;free(raw);free(key);
    if(!decoded){fprintf(stderr,"Source response could not be decoded: %s\n",resolver_error());return NULL;}
    json_object *root=parse(decoded);free(decoded);return root;
}
static void server_choice(const char *server){
    if(!valid_server(server)||count>=40)return;
    for(int i=0;i<count;i++)if(!strcmp(choices[i].label,server))return;
    snprintf(choices[count].label,sizeof(choices[count].label),"%s",server);strcpy(choices[count].meta,"CHOOSE QUALITY");count++;
}
static int compare_choices(const void *a,const void *b){return strcmp(((const Choice*)a)->label,((const Choice*)b)->label);}
static void servers(json_object *root){
    json_object *list=field(root,"sources");
    if(list&&json_object_is_type(list,json_type_array))for(size_t i=0;i<(size_t)json_object_array_length(list);i++)server_choice(string(json_object_array_get_idx(list,i),"server"));
    json_object *map=field(root,"servers");
    if(map&&json_object_is_type(map,json_type_object)){json_object_object_foreach(map,key,val){(void)val;server_choice(key);}}
    qsort(choices,(size_t)count,sizeof(choices[0]),compare_choices);
}
/* External captions are separate HTTPS resources in many provider responses.
 * Never treat subtitle URLs as video sources or trust local protocols. */
static void select_subtitles(json_object *list,char *eng,char *nor){
    if(!list||!json_object_is_type(list,json_type_array))return;
    for(size_t i=0;i<(size_t)json_object_array_length(list);i++){
        json_object *track=json_object_array_get_idx(list,i);
        const char *url=string(track,"url");
        if(!*url)url=string(track,"file");
        if(!*url)url=string(track,"src");
        const char *lang=string(track,"lang");
        if(!*lang)lang=string(track,"language");
        if(!*lang)lang=string(track,"label");
        if(strncmp(url,"https://",8)||strlen(url)>=1024||strpbrk(url,"\r\n\t"))continue;
        int english=!strncasecmp(lang,"en",2)||!strncasecmp(lang,"english",7);
        int norwegian=!strncasecmp(lang,"no",2)||!strncasecmp(lang,"nb",2)||
            !strncasecmp(lang,"nn",2)||!strncasecmp(lang,"norwegian",9);
        if(english&&!*eng)snprintf(eng,1024,"%s",url);
        if(norwegian&&!*nor)snprintf(nor,1024,"%s",url);
    }
}
static void source_subtitles(json_object *root,json_object *source,char *eng,char *nor){
    if(source){select_subtitles(field(source,"subtitles"),eng,nor);
        select_subtitles(field(source,"tracks"),eng,nor);}
    select_subtitles(field(root,"subtitles"),eng,nor);
    select_subtitles(field(root,"tracks"),eng,nor);
}
/* Read HLS master playlist subtitle renditions without touching video variants. */
static int hls_attribute(const char *line,const char *key,char *out,size_t cap){
    char needle[48];snprintf(needle,sizeof(needle),"%s=",key);
    const char *p=line;
    while((p=strstr(p,needle))){
        if(p==line||p[-1]==','||p[-1]==':'){
            p+=strlen(needle);
            int quoted=*p=='"';if(quoted)p++;
            const char *end=p;
            while(*end&&(quoted?*end!='"':*end!=','))end++;
            size_t n=(size_t)(end-p);
            if(n>=cap)return 0;
            memcpy(out,p,n);out[n]=0;return n>0;
        }
        p+=strlen(needle);
    }
    return 0;
}
static int hls_resolve_uri(const char *base,const char *uri,char *out,size_t size){
    if(!base||strncmp(base,"https://",8)||!uri||!out||!size)return 0;
    size_t length=strlen(uri);
    if(!length||strchr(uri,'\\')||strpbrk(uri,"\r\n\t")||
       strstr(uri,"../")||!strncmp(uri,"//",2))return 0;
    if(!strncmp(uri,"https://",8)){
        if(length>=size)return 0;
        memcpy(out,uri,length+1);return 1;
    }
    if(strstr(uri,"://")||!strncmp(uri,"http:",5)||!strncmp(uri,"file:",5))return 0;
    size_t prefix=0;
    if(uri[0]=='/'){
        const char *host=strchr(base+8,'/');
        prefix=host?(size_t)(host-base):strlen(base);
    }else{
        const char *query=strchr(base,'?');
        size_t limit=query?(size_t)(query-base):strlen(base);
        const char *slash=NULL;
        for(size_t i=8;i<limit;i++)if(base[i]=='/')slash=base+i;
        if(!slash)return 0;
        prefix=(size_t)(slash-base)+1;
    }
    if(prefix>=size||length>=size-prefix)return 0;
    memcpy(out,base,prefix);
    memcpy(out+prefix,uri,length+1);
    return 1;
}
static void hls_subtitles(char *manifest,const char *base,char *eng,char *nor){
    if(strncmp(manifest,"#EXTM3U",7))return;
    char *save=NULL;
    for(char *line=strtok_r(manifest,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)){
        if(strncmp(line,"#EXT-X-MEDIA:",13))continue;
        char type[32],uri[1024],lang[64],label[64],resolved[1024];
        if(!hls_attribute(line,"TYPE",type,sizeof(type))||strcmp(type,"SUBTITLES")||
           !hls_attribute(line,"URI",uri,sizeof(uri)))continue;
        lang[0]=label[0]=0;
        hls_attribute(line,"LANGUAGE",lang,sizeof(lang));
        hls_attribute(line,"NAME",label,sizeof(label));
        const char *language=*lang?lang:label;
        int english=!strncasecmp(language,"en",2)||!strncasecmp(language,"english",7);
        int norwegian=!strncasecmp(language,"no",2)||!strncasecmp(language,"nb",2)||
                      !strncasecmp(language,"nn",2)||!strncasecmp(language,"norwegian",9);
        if(!(english&&!*eng)&&!(norwegian&&!*nor))continue;
        if(!hls_resolve_uri(base,uri,resolved,sizeof(resolved)))continue;
        if(english&&!*eng)snprintf(eng,1024,"%s",resolved);
        if(norwegian&&!*nor)snprintf(nor,1024,"%s",resolved);
    }
}
static void add_variants(char *manifest,const char *url,const char *server){
    if(!strncmp(manifest,"#EXTM3U",7)){
        char *save=NULL;for(char *line=strtok_r(manifest,"\r\n",&save);line&&count<40;line=strtok_r(NULL,"\r\n",&save)){
            if(strncmp(line,"#EXT-X-STREAM-INF:",18)||strstr(line,"hvc1")||strstr(line,"hev1")||strstr(line,"av01"))continue;
            char *resolution=strstr(line,"RESOLUTION=");int w,h;if(!resolution||sscanf(resolution,"RESOLUTION=%dx%d",&w,&h)!=2||w<1||w>1920||h<1||h>1080)continue;
            int duplicate=0;for(int i=1;i<count;i++)if(choices[i].height==h)duplicate=1;if(duplicate)continue;
            Choice *c=&choices[count++];snprintf(c->label,sizeof(c->label),"%dP",h);snprintf(c->meta,sizeof(c->meta),"%s / %d X %d",server,w,h);strcpy(c->url,url);c->height=h;
        }
    }
}
static int qualities(json_object *root,const char *server){
    json_object *list=field(root,"sources");const char *url="";json_object *selected=NULL;
    if(list&&json_object_is_type(list,json_type_array))for(size_t i=0;i<(size_t)json_object_array_length(list);i++){
        json_object *s=json_object_array_get_idx(list,i);if(!strcmp(string(s,"server"),server)){url=string(s,"url");selected=s;break;}
    }
    else if(list&&json_object_is_type(list,json_type_object)){selected=list;url=string(list,"file");if(!*url)url=string(list,"url");}
    if(!valid_url(url)){fprintf(stderr,"Selected server has no compatible direct link\n");return -1;}
    strcpy(choices[0].label,"AUTO");snprintf(choices[0].meta,sizeof(choices[0].meta),"%s / BEST COMPATIBLE",server);strcpy(choices[0].url,url);source_subtitles(root,selected,choices[0].subs_eng,choices[0].subs_nor);count=1;
    char *manifest=request(url,NULL,1);
    if(!manifest){fprintf(stderr,"Subtitle discovery for %s: manifest unavailable (English %s, Norwegian %s)\n",server,choices[0].subs_eng[0]?"available":"absent",choices[0].subs_nor[0]?"available":"absent");return 0;}
    /* strtok_r mutates playlists; inspect renditions before variants. */
    hls_subtitles(manifest,url,choices[0].subs_eng,choices[0].subs_nor);
    fprintf(stderr,"Subtitle discovery for %s: English %s, Norwegian %s\n",server,
        choices[0].subs_eng[0]?"available":"absent",choices[0].subs_nor[0]?"available":"absent");
    add_variants(manifest,url,server);
    for(int i=1;i<count;i++){
        snprintf(choices[i].subs_eng,sizeof(choices[i].subs_eng),"%s",choices[0].subs_eng);
        snprintf(choices[i].subs_nor,sizeof(choices[i].subs_nor),"%s",choices[0].subs_nor);
    }
    free(manifest);return 0;
}
int main(int argc,char **argv){
    if(argc!=8|| (strcmp(argv[1],"servers")&&strcmp(argv[1],"source")) ||(strcmp(argv[2],"movie")&&strcmp(argv[2],"tv")))return 2;
    int id=atoi(argv[3]),season=atoi(argv[4]),episode=atoi(argv[5]),page=atoi(argv[7]);
    if(id<=0||season<0||episode<0||page<1||page>7)return 2;
    int resolving=!strcmp(argv[1],"source");if(resolving&&!valid_server(argv[6]))return 2;
    char path[256];if(!strcmp(argv[2],"movie"))snprintf(path,sizeof(path),"/api/tmdb/movie/%d/images",id);
    else {if(!episode)return 2;snprintf(path,sizeof(path),"/api/tmdb/tv/%d/season/%d/episode/%d/images",id,season,episode);}
    if(curl_global_init(CURL_GLOBAL_DEFAULT)||resolver_init())return 1;
    json_object *root=lookup(path,resolving?argv[6]:"");int rc=1;
    if(root){if(resolving){if(qualities(root,argv[6]))goto done;}else servers(root);
        printf("# pages=%d total=%d\n",count?(count+5)/6:1,count);
        for(int i=(page-1)*6;i<count&&i<page*6;i++){Choice *c=&choices[i];if(resolving)printf("%s\t%s\t\t%s\tsource\t%d\t%d\t%d\t%d\t%s\t%s\n",c->label,c->meta,c->url,id,season,episode,c->height,c->subs_eng,c->subs_nor);
            else printf("%s\t%s\t\t%s\tserver\t%d\t%d\t%d\t%d\n",c->label,c->meta,c->url,id,season,episode,c->height);}
        rc=ferror(stdout)?1:0;
    }
done:json_object_put(root);resolver_free();curl_global_cleanup();return rc;
}
