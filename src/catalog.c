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
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "browse_filters.h"
#define API "https://plsdontscrapemelove.flixer.gd/api/tmdb"
#define PAGE_SIZE 6
typedef struct {char title[80],meta[96],poster[512],url[2048],subs_eng[1024],subs_nor[1024],subs_spa[1024],kind[16];int id,season,episode;} Entry;
typedef struct {char *data;size_t length,limit;} Buffer;
static Entry entries[PAGE_SIZE];
static int used,total,cache_slot;
static char cache_key[256],cache_file[80];
static size_t receive(void *data,size_t size,size_t count,void *opaque){
    Buffer *b=opaque;
    if(size&&count>SIZE_MAX/size)return 0;
    size_t n=size*count;if(n>b->limit-b->length)return 0;
    char *p=realloc(b->data,b->length+n+1);if(!p)return 0;
    b->data=p;memcpy(p+b->length,data,n);b->length+=n;p[b->length]=0;return n;
}
static int fetch_referred(const char *url,Buffer *b,const char *referer){
    CURL *c=curl_easy_init();if(!c)return -1;
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,b);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(c,CURLOPT_TIMEOUT,12L);
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CAINFO,access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem");
    curl_easy_setopt(c,CURLOPT_USERAGENT,"FlXtR-Steamlink/0.2");
    if(referer&&*referer)curl_easy_setopt(c,CURLOPT_REFERER,referer);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    CURLcode rc=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(c);
    if(rc!=CURLE_OK||code!=200){fprintf(stderr,"Catalog HTTP %ld / error %d\n",code,(int)rc);return -1;}
    return 0;
}
static int fetch(const char *url,Buffer *b){return fetch_referred(url,b,NULL);}
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
        else if(id||season){
            if(id<0||id>=BROWSE_GENRES||season<0||season>=BROWSE_ORDERS){curl_free(escaped);return -1;}
            int genre=!strcmp(kind,"movie")?movie_genres[id]:series_genres[id];
            if(genre<0){curl_free(escaped);return -1;}
            const char *sorts[]={"popularity.desc","vote_average.desc","primary_release_date.desc","original_title.asc"};
            const char *sort=sorts[season];if(!strcmp(kind,"tv")&&season==2)sort="first_air_date.desc";if(!strcmp(kind,"tv")&&season==3)sort="name.asc";
            snprintf(path,sizeof(path),"/discover/%s?sort_by=%s&include_adult=false&vote_count.gte=20&page=%d",kind,sort,remote);
            if(genre){size_t len=strlen(path);snprintf(path+len,sizeof(path)-len,"&with_genres=%d",genre);}
        }
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
#include "kissanime.h"
#include "archive.h"
#include "tv_catalog.h"
#include "games.h"
/* Keep previously visited title pages on disk, not only the last three.
 * Hashing the complete identity spreads different libraries, searches and pages
 * over a bounded 32-slot cache. Full key validation handles hash collisions. */
#define CATALOG_CACHE_SLOTS 32
#define CATALOG_CACHE_TTL (7 * 24 * 60 * 60)
static void cache_identity(const char *kind,int page,const char *query,int id,int season){
    struct stat st;memset(&st,0,sizeof(st));stat("library.local.tsv",&st);
    snprintf(cache_key,sizeof(cache_key),"v4|%s|%d|%d|%d|%lld|%lld|%s",kind,page,id,season,(long long)st.st_mtime,(long long)st.st_size,query);
    unsigned long hash=2166136261UL;
    for(const unsigned char *p=(const unsigned char *)cache_key;*p;p++)
        hash=((hash^(unsigned long)*p)*16777619UL)&0xffffffffUL;
    cache_slot=(int)(hash%CATALOG_CACHE_SLOTS);
    snprintf(cache_file,sizeof(cache_file),"catalog-cache/page-%d.json",cache_slot);
}
static int cache_load(void){
    struct stat st;if(stat(cache_file,&st)||st.st_size>65536||st.st_size<2||time(NULL)-st.st_mtime>CATALOG_CACHE_TTL)return 0;
    json_object *root=json_object_from_file(cache_file);if(!root)return 0;
    json_object *rows=field(root,"entries");int n=rows&&json_object_is_type(rows,json_type_array)?(int)json_object_array_length(rows):-1;
    int ok=!strcmp(string(root,"key"),cache_key)&&n>=0&&n<=PAGE_SIZE&&number(root,"total")>=0;
    if(ok)for(int i=0;i<n;i++){
        json_object *o=json_object_array_get_idx(rows,i);Entry *e=&entries[i];
        clean(e->title,sizeof(e->title),string(o,"title"));clean(e->meta,sizeof(e->meta),string(o,"meta"));
        clean(e->poster,sizeof(e->poster),string(o,"poster"));clean(e->kind,sizeof(e->kind),string(o,"kind"));
        e->id=number(o,"id");e->season=number(o,"season");e->episode=number(o,"episode");
        if(!strcmp(e->kind,"archive")||!strcmp(e->kind,"archive-file")){
            const char *url=string(o,"url"),*prefix=!strcmp(e->kind,"archive")?ARCHIVE "/details/":ARCHIVE "/download/";
            if(strncmp(url,prefix,strlen(prefix))||strlen(url)>=sizeof(e->url))ok=0;else clean(e->url,sizeof(e->url),url);
        }
        char expected[80];snprintf(expected,sizeof(expected),"catalog-cache/page-%d-poster-%d.bmp",cache_slot,i);
        if(!e->title[0]||((strcmp(e->kind,"archive")&&strcmp(e->kind,"archive-file"))&&e->id<1)||(e->poster[0]&&(strcmp(e->poster,expected)||access(expected,R_OK))))ok=0;
    }
    if(ok){used=n;total=number(root,"total");}
    else {used=total=0;memset(entries,0,sizeof(entries));}
    json_object_put(root);return ok;
}
static void cache_save(void){
    json_object *root=json_object_new_object(),*rows=json_object_new_array();
    json_object_object_add(root,"key",json_object_new_string(cache_key));json_object_object_add(root,"total",json_object_new_int(total));
    json_object_object_add(root,"entries",rows);
    for(int i=0;i<used;i++){
        Entry *e=&entries[i];json_object *o=json_object_new_object();json_object_array_add(rows,o);
        json_object_object_add(o,"title",json_object_new_string(e->title));json_object_object_add(o,"meta",json_object_new_string(e->meta));
        json_object_object_add(o,"poster",json_object_new_string(e->poster));json_object_object_add(o,"kind",json_object_new_string(e->kind));
        if(!strcmp(e->kind,"archive")||!strcmp(e->kind,"archive-file"))json_object_object_add(o,"url",json_object_new_string(e->url));
        json_object_object_add(o,"id",json_object_new_int(e->id));json_object_object_add(o,"season",json_object_new_int(e->season));json_object_object_add(o,"episode",json_object_new_int(e->episode));
    }
    char temp[96];snprintf(temp,sizeof(temp),"%s.next",cache_file);
    if(!json_object_to_file(temp,root))rename(temp,cache_file);
    json_object_put(root);
}
static void poster(Entry *e,int slot){
    char path[512];strcpy(path,e->poster);e->poster[0]=0;
    if(!strncmp(path,"localthumb:",11)){
        const char *source=path+11;
        const char *home=games_home();size_t hlen=strlen(home);
        struct stat st;
        /* Only readable thumbnails beneath RetroArch's configured home. */
        if(strncmp(source,home,hlen)||source[hlen]!='/'||strstr(source,"..")||
           stat(source,&st)||!S_ISREG(st.st_mode)||st.st_size>2*1024*1024)return;
        SDL_Surface *art=IMG_Load(source);
        if(!art)return;
        if(art->w<1||art->h<1||art->w>2048||art->h>2048){SDL_FreeSurface(art);return;}
        float scale=154.0f/art->w;if(art->h*scale>231)scale=231.0f/art->h;
        int w=(int)(art->w*scale),h=(int)(art->h*scale);
        if(w<1||h<1){SDL_FreeSurface(art);return;}
        SDL_Surface *small=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        if(small&&!SDL_BlitScaled(art,NULL,small,NULL)){
            char file[80];snprintf(file,sizeof(file),"catalog-cache/page-%d-poster-%d.bmp",cache_slot,slot);
            if(!SDL_SaveBMP(small,file))snprintf(e->poster,sizeof(e->poster),"%s",file);
        }
        SDL_FreeSurface(small);SDL_FreeSurface(art);return;
    }
    int kiss=!strncmp(path,KISS "/wp-content/uploads/",sizeof(KISS "/wp-content/uploads/")-1);
    int archive=!strncmp(path,ARCHIVE "/services/img/",sizeof(ARCHIVE "/services/img/")-1);
    if(archive&&!archive_identifier(path+sizeof(ARCHIVE "/services/img/")-1))return;
    if(!kiss&&!archive&&path[0]!='/')return;
    for(size_t i=archive?sizeof(ARCHIVE)-1:kiss?sizeof(KISS)-1:1;path[i];i++)if(!isalnum((unsigned char)path[i])&&path[i]!='.'&&path[i]!='_'&&path[i]!='-'&&!((kiss||archive)&&path[i]=='/'))return;
    if(strstr(path,".."))return;
    char url[768];if(kiss||archive)snprintf(url,sizeof(url),"%s",path);else snprintf(url,sizeof(url),"https://image.tmdb.org/t/p/w154%s",path);
    Buffer b={NULL,0,(kiss||archive)?512*1024:128*1024};
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
                ok=w>0&&w<=((kiss||archive)?1024:192)&&h>0&&h<=((kiss||archive)?1536:288);break;
            }pos+=len;
        }
        if(ok){SDL_RWops *rw=SDL_RWFromConstMem(b.data,(int)b.length);SDL_Surface *s=rw?IMG_Load_RW(rw,1):NULL;
            if(s){
                if(s->w>192||s->h>288){float scale=154.0f/s->w;if(s->h*scale>231)scale=231.0f/s->h;
                    SDL_Surface *small=SDL_CreateRGBSurfaceWithFormat(0,(int)(s->w*scale),(int)(s->h*scale),32,SDL_PIXELFORMAT_ARGB8888);
                    if(!small||SDL_BlitScaled(s,NULL,small,NULL)){SDL_FreeSurface(small);SDL_FreeSurface(s);free(b.data);return;}
                    SDL_FreeSurface(s);s=small;
                }char file[80];snprintf(file,sizeof(file),"catalog-cache/page-%d-poster-%d.bmp",cache_slot,slot);
                if(!SDL_SaveBMP(s,file))snprintf(e->poster,sizeof(e->poster),"%s",file);
                SDL_FreeSurface(s);
            }
        }
    }free(b.data);
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--run-game"))return games_run();
    if(argc!=7&&argc!=8){fprintf(stderr,"Usage: greenlink-catalog movie|tv|season|episode|source|quality PAGE QUERY ID SEASON EPISODE [SERVER]\n");return 2;}
    const char *kind=argv[1];int page=positive(argv[2],0),id=positive(argv[4],1),season=positive(argv[5],1),episode=positive(argv[6],1);
    if(page<1||page>2000||id<0||season<0||episode<0||strlen(argv[3])>(!strcmp(kind,"archive-files")||!strcmp(kind,"games-archive-files")||!strcmp(kind,"games-file-download")||!strcmp(kind,"games-zip-files")?2047:64))return 2;
    if(strcmp(kind,"games-zip-files")&&strcmp(kind,"games-platforms")&&strcmp(kind,"games-bios-files")&&strcmp(kind,"games-bios-search")&&strcmp(kind,"games-archive-search")&&strcmp(kind,"games-archive-files")&&strcmp(kind,"games-file-download")&&strcmp(kind,"games-archive")&&strcmp(kind,"games-download")&&strcmp(kind,"games")&&strcmp(kind,"games-folders")&&strcmp(kind,"games-system")&&strcmp(kind,"movie")&&strcmp(kind,"tv")&&strcmp(kind,"season")&&strcmp(kind,"episode")&&strcmp(kind,"source")&&strcmp(kind,"quality")&&strcmp(kind,"kiss")&&strcmp(kind,"kiss-episodes")&&strcmp(kind,"kiss-source")&&strcmp(kind,"kiss-quality")&&strcmp(kind,"archive")&&strcmp(kind,"archive-files")&&strcmp(kind,"tv-pick")&&strcmp(kind,"tv-break"))return 2;
    if((!strcmp(kind,"source")||!strcmp(kind,"quality"))&&!getenv("FLXTR_NO_NETWORK")&&!access("./greenlink-resolver",X_OK)){
        int quality=!strcmp(kind,"quality");if(quality&&argc!=8)return 2;
        execl("./greenlink-resolver","greenlink-resolver",quality?"source":"servers",episode?"tv":"movie",argv[4],argv[5],argv[6],quality?argv[7]:"",argv[2],(char*)NULL);return 1;
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 1;
    mkdir("catalog-cache",0700);
    int cacheable=strcmp(kind,"games-zip-files")&&strcmp(kind,"games-file-download")&&strcmp(kind,"games-archive-files")&&strcmp(kind,"games-bios-search")&&strcmp(kind,"games-bios-files")&&strcmp(kind,"games-archive")&&strcmp(kind,"games-download")&&strcmp(kind,"games")&&strcmp(kind,"tv-break")&&strcmp(kind,"tv-pick")&&strcmp(kind,"source")&&strcmp(kind,"quality")&&strcmp(kind,"kiss-source")&&strcmp(kind,"kiss-quality");
    if(cacheable){cache_identity(kind,page,argv[3],id,season);if(cache_load())goto output;}
    int rc;
    if(!strcmp(kind,"games-platforms"))rc=games_platforms(page);
    else if(!strcmp(kind,"games-archive"))rc=games_archive_list(page);
    else if(!strcmp(kind,"games-bios-search"))rc=games_bios_search(page,id,argv[3]);
    else if(!strcmp(kind,"games-bios-files"))rc=games_bios_files(page,argv[3],id);
    else if(!strcmp(kind,"games-archive-search"))rc=games_archive_search(page,id,argv[3]);
    else if(!strcmp(kind,"games-archive-files"))rc=games_archive_files(page,argv[3],id);
    else if(!strcmp(kind,"games-file-download"))rc=games_archive_file_download(argv[3],id);
    else if(!strcmp(kind,"games-zip-files"))rc=games_zip_files(page,argv[3],id);
    else if(!strcmp(kind,"games-download"))rc=games_archive_download(id);
    else if(!strcmp(kind,"games"))rc=games_list(page,id);
    else if(!strcmp(kind,"games-folders"))rc=games_folders(page);
    else if(!strcmp(kind,"games-system"))rc=games_system(page,id);
    else if(!strcmp(kind,"tv-break"))rc=archive_commercial(id);
    else if(!strcmp(kind,"tv-pick"))rc=tv_pick(positive(argv[3],1),id);
    else if(!strcmp(kind,"archive"))rc=archive_list(page,argv[3]);
    else if(!strcmp(kind,"archive-files"))rc=archive_files(page,argv[3]);
    else if(!strcmp(kind,"kiss"))rc=kiss_list_filtered(page,argv[3],id,season);
    else if(!strcmp(kind,"kiss-episodes"))rc=kiss_episodes(id,page);
    else if(!strcmp(kind,"kiss-source"))rc=kiss_source(id,episode,"");
    else if(!strcmp(kind,"kiss-quality"))rc=kiss_source(id,episode,argc==8?argv[7]:"");
    else if(!strcmp(kind,"source"))rc=sources(page,id,season,episode);
    else if((!strcmp(kind,"movie")||!strcmp(kind,"tv"))&&!*argv[3]&&!id&&!season&&!local_list(kind,page,argv[3]))rc=0;
    else rc=remote_list(kind,page,argv[3],id,season);
    if(rc){curl_global_cleanup();return 1;}
    /* Invalidate before replacing art: cancellation cannot pair old JSON with new posters. */
    if(cacheable)unlink(cache_file);
    for(int i=0;i<used;i++){if(strcmp(kind,"games-platforms")&&strcmp(kind,"games-file-download")&&strcmp(kind,"games-download")&&strcmp(kind,"tv-pick")&&strcmp(kind,"tv-break")&&!getenv("FLXTR_NO_ART"))poster(&entries[i],i);else entries[i].poster[0]=0;}
    if(cacheable)cache_save();
output:
    printf("# pages=%d total=%d\n",total?(total+PAGE_SIZE-1)/PAGE_SIZE:1,total);
    for(int i=0;i<used;i++){Entry *e=&entries[i];if(!strcmp(e->kind,"source")&&(e->subs_eng[0]||e->subs_nor[0]||e->subs_spa[0]))
            printf("%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t0\t%s\t%s\t%s\n",e->title,e->meta,e->poster,e->url,e->kind,e->id,e->season,e->episode,e->subs_eng,e->subs_nor,e->subs_spa);
        else printf("%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n",e->title,e->meta,e->poster,e->url,e->kind,e->id,e->season,e->episode);}
    curl_global_cleanup();return ferror(stdout)?1:0;
}
