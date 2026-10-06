/* Short-lived metadata worker. The UI stays responsive while this process does
 * bounded HTTPS requests. Only six entries and six small posters leave it. */
#define _GNU_SOURCE
#include <SDL.h>
#include <SDL_image.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define API "https://plsdontscrapemelove.flixer.gd/api/tmdb"
#define PAGE_SIZE 6
typedef struct {char title[80],meta[96],poster[128],url[2048],kind[16];int id,season,episode;} Entry;
typedef struct {char *data;size_t length,limit;} Buffer;
static Entry entries[PAGE_SIZE];
static int used,total;
static size_t receive(void *data,size_t size,size_t count,void *opaque){
    Buffer *b=opaque;
    if(size&&count>SIZE_MAX/size)return 0;
    size_t n=size*count;if(n>b->limit-b->length)return 0;
    char *p=realloc(b->data,b->length+n+1);if(!p)return 0;
    b->data=p;memcpy(p+b->length,data,n);b->length+=n;p[b->length]=0;return n;
}
static int fetch(const char *url,Buffer *b){
    CURL *c=curl_easy_init();if(!c)return -1;
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,b);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(c,CURLOPT_TIMEOUT,12L);
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CAINFO,access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem");
    curl_easy_setopt(c,CURLOPT_USERAGENT,"FlXtR-Steamlink/0.2");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    CURLcode rc=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(c);
    if(rc!=CURLE_OK||code!=200){fprintf(stderr,"Catalog HTTP %ld / error %d\n",code,(int)rc);return -1;}
    return 0;
}
static json_object *get_json(const char *path){
    char url[1024];Buffer b={NULL,0,2*1024*1024};
    if(snprintf(url,sizeof(url),API"%s",path)>=(int)sizeof(url))return NULL;
    if(fetch(url,&b)){free(b.data);return NULL;}
    json_tokener *tok=json_tokener_new_ex(32);if(!tok){free(b.data);return NULL;}
    json_object *obj=json_tokener_parse_ex(tok,b.data,(int)b.length);
    if(json_tokener_get_error(tok)!=json_tokener_success){json_object_put(obj);obj=NULL;}
    json_tokener_free(tok);free(b.data);return obj;
}
static json_object *field(json_object *obj,const char *name){json_object *v=NULL;json_object_object_get_ex(obj,name,&v);return v;}
static const char *string(json_object *obj,const char *name){json_object *v=field(obj,name);return v&&json_object_is_type(v,json_type_string)?json_object_get_string(v):"";}
static int number(json_object *obj,const char *name){json_object *v=field(obj,name);return v&&json_object_is_type(v,json_type_int)?json_object_get_int(v):0;}
static void clean(char *out,size_t cap,const char *in){
    size_t n=0;for(;*in&&n+1<cap;in++){
        unsigned char c=(unsigned char)*in;
        if(c>=128){if((c&0xc0)!=0x80)out[n++]='?';}
        else out[n++]=c<32||c==127?' ':c;
    }out[n]=0;
}
static int positive(const char *s,int allow_zero){
    char *end;long n=strtol(s,&end,10);return *s&&!*end&&n>=(allow_zero?0:1)&&n<=10000000?(int)n:-1;
}
static int split(char *line,char **parts,int cap){
    int n=0;char *p=line;line[strcspn(line,"\r\n")]=0;
    while(n<cap){parts[n++]=p;char *tab=strchr(p,'\t');if(!tab)break;*tab=0;p=tab+1;}
    return n;
}
static int local_list(const char *kind,int page,const char *query){
    FILE *f=fopen("library.local.tsv","r");if(!f)return -1;
    char line[2600];int first=(page-1)*PAGE_SIZE;
    while(fgets(line,sizeof(line),f)){
        char *p[8];if((line[0]=='#'&&!strchr(line,'\t'))||split(line,p,8)!=8||strcmp(p[4],kind))continue;
        if(*query&&!strcasestr(p[0],query))continue;
        if(total++<first||used==PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),p[0]);clean(e->meta,sizeof(e->meta),p[1]);
        clean(e->poster,sizeof(e->poster),p[2]);clean(e->kind,sizeof(e->kind),kind);e->id=positive(p[5],0);
    }fclose(f);return 0;
}
static void from_json(Entry *e,json_object *o,const char *kind,int id,int season){
    const char *title=string(o,!strcmp(kind,"movie")?"title":"name");
    clean(e->title,sizeof(e->title),title);clean(e->kind,sizeof(e->kind),kind);
    e->id=id?id:number(o,"id");e->season=season;e->episode=0;
    const char *date=string(o,!strcmp(kind,"movie")?"release_date":"first_air_date");
    clean(e->poster,sizeof(e->poster),string(o,"poster_path"));
    if(!strcmp(kind,"season")){e->season=number(o,"season_number");snprintf(e->meta,sizeof(e->meta),"SEASON %d / %d EPISODES",e->season,number(o,"episode_count"));}
    else if(!strcmp(kind,"episode")){
        e->episode=number(o,"episode_number");clean(e->poster,sizeof(e->poster),string(o,"still_path"));
        snprintf(e->meta,sizeof(e->meta),"S%02d E%02d / %.10s",season,e->episode,string(o,"air_date"));
    }else snprintf(e->meta,sizeof(e->meta),"%.4s / %s",date,!strcmp(kind,"movie")?"MOVIE":"SERIES");
}
static int remote_list(const char *kind,int page,const char *query,int id,int season){
    char path[900];int first=(page-1)*PAGE_SIZE;
    if(!strcmp(kind,"season")||!strcmp(kind,"episode")){
        if(!strcmp(kind,"season"))snprintf(path,sizeof(path),"/tv/%d",id);
        else snprintf(path,sizeof(path),"/tv/%d/season/%d",id,season);
        json_object *root=get_json(path);if(!root)return -1;
        json_object *array=field(root,!strcmp(kind,"season")?"seasons":"episodes");
        if(!array||!json_object_is_type(array,json_type_array)){json_object_put(root);return -1;}
        total=(int)json_object_array_length(array);
        for(int i=first;i<total&&used<PAGE_SIZE;i++)from_json(&entries[used++],json_object_array_get_idx(array,i),kind,id,season);
        json_object_put(root);return 0;
    }
    char *escaped=curl_easy_escape(NULL,query,0);if(!escaped)return -1;
    int last=(first+PAGE_SIZE-1)/20+1;
    for(int remote=first/20+1;remote<=last;remote++){
        if(*query)snprintf(path,sizeof(path),"/search/%s?query=%s&page=%d",kind,escaped,remote);
        else snprintf(path,sizeof(path),"/%s/popular?page=%d",kind,remote);
        json_object *root=get_json(path);if(!root){curl_free(escaped);return -1;}
        json_object *array=field(root,"results");
        if(!array||!json_object_is_type(array,json_type_array)){json_object_put(root);curl_free(escaped);return -1;}
        total=number(root,"total_results");if(total>10000)total=10000;
        int n=(int)json_object_array_length(array);
        for(int i=0;i<n&&used<PAGE_SIZE;i++){
            int index=(remote-1)*20+i;
            if(index<first||index>=first+PAGE_SIZE)continue;
            from_json(&entries[used++],json_object_array_get_idx(array,i),kind,0,0);
        }
        json_object_put(root);if(remote*20>=total)break;
    }
    curl_free(escaped);return 0;
}
static int sources(int page,int id,int season,int episode){
    FILE *f=fopen("sources.local.tsv","r");if(!f)return 0;
    char line[2600];int first=(page-1)*PAGE_SIZE;
    while(fgets(line,sizeof(line),f)){
        char *p[7];if(line[0]=='#'||split(line,p,7)!=7)continue;
        if(positive(p[0],0)!=id||positive(p[1],1)!=season||positive(p[2],1)!=episode)continue;
        if(strcmp(p[3],episode?"tv":"movie"))continue;
        if(strncmp(p[6],"https://",8)&&strncmp(p[6],"http://",7))continue;
        if(strlen(p[6])>=sizeof(entries[0].url))continue;
        if(total++<first||used==PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),p[4]);clean(e->meta,sizeof(e->meta),p[5]);
        strcpy(e->url,p[6]);strcpy(e->kind,"source");e->id=id;e->season=season;e->episode=episode;
    }fclose(f);return 0;
}
static void poster(Entry *e,int slot){
    char path[128];strcpy(path,e->poster);e->poster[0]=0;
    if(path[0]!='/')return;
    for(size_t i=1;path[i];i++)if(!isalnum((unsigned char)path[i])&&path[i]!='.'&&path[i]!='_'&&path[i]!='-')return;
    if(strstr(path,".."))return;
    char url[256];snprintf(url,sizeof(url),"https://image.tmdb.org/t/p/w154%s",path);
    Buffer b={NULL,0,128*1024};
    if(!fetch(url,&b)){
        /* JPEG dimensions are checked before decoding; reject other formats. */
        int ok=0;size_t pos=2;
        if(b.length>4&&(unsigned char)b.data[0]==255&&(unsigned char)b.data[1]==216)
        while(pos+4<=b.length){
            if((unsigned char)b.data[pos++]!=255)break;
            unsigned marker=(unsigned char)b.data[pos++];
            unsigned len=((unsigned char)b.data[pos]<<8)|(unsigned char)b.data[pos+1];
            if(len<2||pos+len>b.length)break;
            if((marker==0xc0||marker==0xc1||marker==0xc2)&&len>=8){
                int h=((unsigned char)b.data[pos+3]<<8)|(unsigned char)b.data[pos+4];
                int w=((unsigned char)b.data[pos+5]<<8)|(unsigned char)b.data[pos+6];
                ok=w>0&&w<=192&&h>0&&h<=288;break;
            }pos+=len;
        }
        if(ok){SDL_RWops *rw=SDL_RWFromConstMem(b.data,(int)b.length);SDL_Surface *s=rw?IMG_Load_RW(rw,1):NULL;
            if(s){char file[80];snprintf(file,sizeof(file),"catalog-cache/poster-%d.next.bmp",slot);
                if(!SDL_SaveBMP(s,file))snprintf(e->poster,sizeof(e->poster),"catalog-cache/poster-%d.bmp",slot);
                SDL_FreeSurface(s);
            }
        }
    }free(b.data);
}
int main(int argc,char **argv){
    if(argc!=7){fprintf(stderr,"Usage: greenlink-catalog movie|tv|season|episode|source PAGE QUERY ID SEASON EPISODE\n");return 2;}
    const char *kind=argv[1];int page=positive(argv[2],0),id=positive(argv[4],1),season=positive(argv[5],1),episode=positive(argv[6],1);
    if(page<1||page>2000||id<0||season<0||episode<0||strlen(argv[3])>64)return 2;
    if(strcmp(kind,"movie")&&strcmp(kind,"tv")&&strcmp(kind,"season")&&strcmp(kind,"episode")&&strcmp(kind,"source"))return 2;
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 1;
    mkdir("catalog-cache",0700);
    int rc;
    if(!strcmp(kind,"source"))rc=sources(page,id,season,episode);
    else if((!strcmp(kind,"movie")||!strcmp(kind,"tv"))&&!*argv[3]&&!local_list(kind,page,argv[3]))rc=0;
    else rc=remote_list(kind,page,argv[3],id,season);
    if(rc){curl_global_cleanup();return 1;}
    for(int i=0;i<used;i++)if(!getenv("FLXTR_NO_ART"))poster(&entries[i],i);
    printf("# pages=%d total=%d\n",total?(total+PAGE_SIZE-1)/PAGE_SIZE:1,total);
    for(int i=0;i<used;i++){Entry *e=&entries[i];printf("%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n",e->title,e->meta,e->poster,e->url,e->kind,e->id,e->season,e->episode);}
    curl_global_cleanup();return ferror(stdout)?1:0;
}
