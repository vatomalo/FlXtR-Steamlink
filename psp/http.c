#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#define WINDOW (256u*1024u)
typedef struct {unsigned char *data;size_t cap,used;uint64_t total,start;bool has_range;} Response;
static size_t body(char *p,size_t s,size_t n,void *o){
    Response *r=o;if(s && n>SIZE_MAX/s)return 0;n*=s;
    if(n>r->cap-r->used)return 0;
    memcpy(r->data+r->used,p,n);r->used+=n;return n;
}
static size_t header(char *p,size_t s,size_t n,void *o){
    Response *r=o;size_t len=s*n;char line[256];
    if(len<sizeof(line)){
        memcpy(line,p,len);line[len]=0;
        if(!strncasecmp(line,"HTTP/",5)){r->used=0;r->has_range=false;}
        if(!strncasecmp(line,"Content-Range: bytes ",21)){
            unsigned long long a,b,t;
            if(sscanf(line+21,"%llu-%llu/%llu",&a,&b,&t)==3 && a<=b && b<t){
                r->start=a;r->total=t;r->has_range=true;
            }
        }
    }return len;
}
static int progress(void *p,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d){
    (void)p;(void)a;(void)b;(void)c;(void)d;
    return network_cancelled();
}
int transfer(const char *url,const char *range,unsigned char *data,size_t cap,size_t *used,uint64_t *total){
    Response r={.data=data,.cap=cap};CURL *c=curl_easy_init();if(!c)return -1;
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTP|CURLPROTO_HTTPS);
    curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,!strncmp(url,"https://",8)?CURLPROTO_HTTPS:(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(c,CURLOPT_CAINFO,"cacert.pem");
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(c,CURLOPT_TIMEOUT,40L);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"FlXtR-PSP-beta/1");
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,body);curl_easy_setopt(c,CURLOPT_WRITEDATA,&r);
    curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,header);curl_easy_setopt(c,CURLOPT_HEADERDATA,&r);
    curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
    if(range)curl_easy_setopt(c,CURLOPT_RANGE,range);
    CURLcode result=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(c);*used=r.used;if(total)*total=r.total;
    if(result!=CURLE_OK){status("Network: %s",curl_easy_strerror(result));return -1;}
    if(range){
        unsigned long long requested=0;sscanf(range,"%llu-",&requested);
        if(code!=206 || !r.has_range || r.start!=requested){status("Server does not support exact byte ranges");return -1;}
    }else if(code!=200){status("HTTP %ld",code);return -1;}
    return 0;
}
bool stream_open(Stream *s,const char *url){
    memset(s,0,sizeof(*s));if(strlen(url)>=sizeof(s->url))return false;
    strcpy(s->url,url);s->buffer=malloc(WINDOW);if(!s->buffer)return false;
    if(transfer(url,"0-262143",s->buffer,WINDOW,&s->used,&s->length)<0 || !s->length){stream_close(s);return false;}
    return true;
}
bool stream_read(void *opaque,uint64_t offset,void *dest,size_t length){
    Stream *s=opaque;unsigned char *out=dest;
    if(offset>s->length || length>s->length-offset)return false;
    while(length){
        if(offset<s->start || offset-s->start>=s->used){
            char range[80];uint64_t last=s->length-1;
            if(last-offset>=WINDOW)last=offset+WINDOW-1;
            snprintf(range,sizeof(range),"%llu-%llu",(unsigned long long)offset,(unsigned long long)last);
            s->used=0;s->start=offset;uint64_t total=0;
            if(transfer(s->url,range,s->buffer,WINDOW,&s->used,&total)<0 || total!=s->length || !s->used)return false;
        }
        size_t at=(size_t)(offset-s->start),n=s->used-at;if(n>length)n=length;
        memcpy(out,s->buffer+at,n);offset+=n;out+=n;length-=n;
    }return true;
}
void stream_close(Stream *s){free(s->buffer);s->buffer=NULL;}
