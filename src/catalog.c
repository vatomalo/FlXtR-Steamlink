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
#ifndef _WIN32
#include <sys/file.h>
#endif
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include "windows_runtime.h"
#include "windows_json.h"
#include "response_cache.h"
#include "browse_filters.h"
#include "game_metadata.h"
#define API "https://plsdontscrapemelove.flixer.gd/api/tmdb"
#define PAGE_SIZE 6
typedef struct {char title[80],meta[96],poster[512],url[2048],subs_eng[1024],subs_nor[1024],subs_spa[1024],kind[16];int id,season,episode;} Entry;
typedef struct {char *data;size_t length,limit;} Buffer;
static Entry entries[PAGE_SIZE];
static int used,total,cache_slot;
#ifdef _WIN32
static int flxtr_background_worker;
static void flxtr_background(const char *kind){
    mkdir("catalog-cache",0700);char name[]="catalog-cache/background-XXXXXX";
    int fd=mkstemp(name);if(fd<0)return;
    FILE *out=fdopen(fd,"wb");
    if(!out){close(fd);unlink(name);return;}
    int good=fwrite(&used,sizeof(used),1,out)==1&&fwrite(entries,sizeof(entries),1,out)==1;
    if(fclose(out))good=0;
    const char *args[]={"greenlink-catalog","--background",kind,name,NULL};
    if(!good||flxtr_win_detach("greenlink-catalog",args))unlink(name);
}
#endif

static unsigned long cache_poster_hash;
static char cache_key[256],cache_file[80];
static size_t receive(void *data,size_t size,size_t count,void *opaque){
    Buffer *b=opaque;
    if(size&&count>SIZE_MAX/size)return 0;
    size_t n=size*count;if(n>b->limit-b->length)return 0;
    char *p=realloc(b->data,b->length+n+1);if(!p)return 0;
    b->data=p;memcpy(p+b->length,data,n);b->length+=n;p[b->length]=0;return n;
}
static CURL *catalog_http;
static void catalog_http_reset(void){if(catalog_http)curl_easy_cleanup(catalog_http);catalog_http=NULL;}
static void catalog_http_close(void){catalog_http_reset();curl_global_cleanup();}
static int fetch_referred(const char *url,Buffer *b,const char *referer){
    if(!catalog_http)catalog_http=curl_easy_init();
    CURL *c=catalog_http;if(!c)return -1;curl_easy_reset(c);
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,b);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(c,CURLOPT_TIMEOUT,12L);
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CAINFO,FLXTR_CA_BUNDLE);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"FlXtR-Steamlink/0.2");
    if(referer&&*referer)curl_easy_setopt(c,CURLOPT_REFERER,referer);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    CURLcode rc;long code=0;
    for(int attempt=0;attempt<2;attempt++){
        if(attempt){
            /* Retry into an empty bounded buffer, never append partial bytes. */
            free(b->data);b->data=NULL;b->length=0;
            curl_easy_setopt(c,CURLOPT_WRITEDATA,b);
        }
        rc=curl_easy_perform(c);
        code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
        if(rc==CURLE_OK&&code==200)return 0;
        if((rc!=CURLE_RECV_ERROR&&rc!=CURLE_WRITE_ERROR&&
            rc!=CURLE_OPERATION_TIMEDOUT&&rc!=CURLE_PARTIAL_FILE)||attempt)break;
    }
    fprintf(stderr,"Catalog HTTP %ld / error %d\n",code,(int)rc);
    return -1;
}
static int fetch(const char *url,Buffer *b){return fetch_referred(url,b,NULL);}
static json_object *get_json(const char *path){
    char url[1024];
    if(snprintf(url,sizeof(url),API"%s",path)>=(int)sizeof(url))return NULL;
    char *cached=response_cache_load(url);
    for(int attempt=0;attempt<2;attempt++){
        Buffer b={cached,cached?strlen(cached):0,2*1024*1024};int hit=cached!=NULL;cached=NULL;
        if(!hit&&fetch(url,&b)){free(b.data);return NULL;}
        json_tokener *tok=json_tokener_new_ex(32);if(!tok){free(b.data);return NULL;}
        json_object *obj=json_tokener_parse_ex(tok,b.data,(int)b.length);
        if(json_tokener_get_error(tok)!=json_tokener_success||!json_object_is_type(obj,json_type_object)){json_object_put(obj);obj=NULL;}
        json_tokener_free(tok);
        if(obj&&!hit)response_cache_store(url,b.data);
        free(b.data);if(obj||!hit)return obj;
    }return NULL;
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
    /* Reuse descriptions already supplied by TMDB discover/results JSON.
     * Sidecar writes are local; no additional request delays page loading. */
    if((!strcmp(kind,"movie")||!strcmp(kind,"tv"))&&e->id>0){
        const char *overview=string(o,"overview");
        if(*overview){
            mkdir("media-info-cache",0700);
            char filename[100],temporary[116],description[400];
            snprintf(filename,sizeof(filename),"media-info-cache/%s-%d.txt",kind,e->id);
            snprintf(temporary,sizeof(temporary),"%s.next",filename);
            clean(description,sizeof(description),overview);
            FILE *info=fopen(temporary,"w");
            if(info){fprintf(info,"%s\n",description);
                if(!fclose(info))rename(temporary,filename);
                else unlink(temporary);
            }
        }
    }
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
#include "romsgames.h"
#include "series_navigation.h"
/* Keep previously visited title pages on disk, not only the last three.
 * Hashing the complete identity spreads different libraries, searches and pages
 * over a bounded 32-slot cache. Full key validation handles hash collisions. */
#define CATALOG_CACHE_SLOTS 32
#define CATALOG_CACHE_TTL (7 * 24 * 60 * 60)
static void cache_identity(const char *kind,int page,const char *query,int id,int season){
    struct stat st;memset(&st,0,sizeof(st));stat("library.local.tsv",&st);
    snprintf(cache_key,sizeof(cache_key),"%s|%s|%d|%d|%d|%lld|%lld|%s",(!strcmp(kind,"games-system")||!strcmp(kind,"games-platforms"))?"v10":(!strcmp(kind,"movie")||!strcmp(kind,"tv"))?"v10":(!strcmp(kind,"games-folders")||!strcmp(kind,"kiss"))?"v10":"v10",kind,page,id,season,(long long)st.st_mtime,(long long)st.st_size,query);
    unsigned long hash=2166136261UL;
    for(const unsigned char *p=(const unsigned char *)cache_key;*p;p++)
        hash=((hash^(unsigned long)*p)*16777619UL)&0xffffffffUL;
    cache_poster_hash=hash;
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
        /* Restore the URL for every URL-bearing cached entry. In particular,
         * game-item and game-rom need theirs to browse or download. */
        const char *cached_url=string(o,"url");
        if(*cached_url){
            if(strlen(cached_url)>=sizeof(e->url)||
               (strncmp(cached_url,"https://",8)&&strncmp(cached_url,"http://",7)))ok=0;
            else snprintf(e->url,sizeof(e->url),"%s",cached_url);
        }
        if(!strcmp(e->kind,"archive")||!strcmp(e->kind,"archive-file")||
           !strcmp(e->kind,"game-item")||!strcmp(e->kind,"game-rom")||
           !strcmp(e->kind,"bios-item")||!strcmp(e->kind,"bios-file")||
           !strcmp(e->kind,"bios-zip")){
            const char *prefix=(!strcmp(e->kind,"archive")||
                                !strcmp(e->kind,"game-item")||
                                !strcmp(e->kind,"bios-item"))?
                               ARCHIVE "/details/":ARCHIVE "/download/";
            if(strncmp(e->url,prefix,strlen(prefix)))ok=0;
        }
        char expected[80];snprintf(expected,sizeof(expected),"catalog-cache/art-%08lx-%d.bmp",cache_poster_hash,i);
        if(!e->title[0]||((strcmp(e->kind,"archive")&&strcmp(e->kind,"archive-file"))&&e->id<1)||(e->poster[0]&&((strncmp(e->poster,"favorites-art/",14)&&strcmp(e->poster,expected))||strstr(e->poster,"..")||access(e->poster,R_OK))))ok=0;
    }
    /* A successful metadata cache entry with blank posters must not suppress
     * future attempts to retrieve transiently failed artwork. */
    if(ok&&!getenv("FLXTR_NO_ART")&&strcmp(cache_key,"")!=0){
        for(int i=0;i<n;i++){
            if(entries[i].poster[0])continue;
            if(!strcmp(entries[i].kind,"kiss")||
               !strcmp(entries[i].kind,"movie")||
               !strcmp(entries[i].kind,"tv")){ok=0;break;}
        }
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
        if(e->url[0])json_object_object_add(o,"url",json_object_new_string(e->url));
        json_object_object_add(o,"id",json_object_new_int(e->id));json_object_object_add(o,"season",json_object_new_int(e->season));json_object_object_add(o,"episode",json_object_new_int(e->episode));
    }
    char temp[96];snprintf(temp,sizeof(temp),"%s.next",cache_file);
    if(!json_object_to_file(temp,root))rename(temp,cache_file);
    json_object_put(root);
}
/* Match human-readable Libretro filenames despite punctuation, spaces and
 * region/revision suffixes. Only artwork names are normalized; library titles
 * and ROM/core selection are never modified. */
static size_t art_normalize(const char *src,char *dst,size_t cap){
    size_t n=0;
    for(;*src&&n+1<cap;src++){
        unsigned char ch=(unsigned char)*src;
        if(ch=='('||ch=='[')break;
        if(isalnum(ch))dst[n++]=(char)tolower(ch);
    }
    dst[n]=0;return n;
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
            char file[80];snprintf(file,sizeof(file),"catalog-cache/art-%08lx-%d.bmp",cache_poster_hash,slot);
            if(!SDL_SaveBMP(small,file))snprintf(e->poster,sizeof(e->poster),"%s",file);
        }
        SDL_FreeSurface(small);SDL_FreeSurface(art);return;
    }
    if(!strncmp(path,"gameart:",8)){
        /* Only the system names chosen by games_list_filtered can reach here.
         * Encode path components, never accept a supplied host or arbitrary URL. */
        char *sep=strchr(path+8,'|');
        if(!sep)return;
        *sep++=0;
        const char *system=path+8;
        if(!*sep||strlen(sep)>120)return;
        const char *allowed[]={"Nintendo - Nintendo Entertainment System","Nintendo - Super Nintendo Entertainment System",
            "Nintendo - Game Boy Advance","Nintendo - Game Boy Color","Nintendo - Game Boy",
            "Sony - PlayStation","Sony - PlayStation Portable","SNK - Neo Geo","Sega - Mega Drive - Genesis"};
        int ok=0;
        for(size_t k=0;k<sizeof(allowed)/sizeof(allowed[0]);k++)
            if(!strcmp(system,allowed[k])){ok=1;break;}
        int console_icon=!strcmp(system,"FlXtR Console");
        if(!ok&&!console_icon)return;
        if(console_icon){
            const char *ids[]={"FC","SFC","GB","GBC","GBA","PS","PSP","NEOGEO","ARCADE","MAME","SCUMMVM"};
            int valid=0;
            for(size_t k=0;k<sizeof(ids)/sizeof(ids[0]);k++)
                if(!strcmp(sep,ids[k])){valid=1;break;}
            if(!valid)return;
        }
        char *sys=curl_easy_escape(NULL,system,0),*name=curl_easy_escape(NULL,sep,0);
        if(!sys||!name){curl_free(sys);curl_free(name);return;}
        char url[1024];
        int len=snprintf(url,sizeof(url),"https://thumbnails.libretro.com/%s/Named_Boxarts/%s.png",sys,name);
        curl_free(sys);curl_free(name);
        if(len<0||len>=(int)sizeof(url))return;
        Buffer b={NULL,0,4*1024*1024};
        if(console_icon){
            int n=snprintf(url,sizeof(url),
                "https://raw.githubusercontent.com/KyleBing/retro-game-console-icons/main/series_trimui/300w%%401x/%s.png",sep);
            if(n<0||n>=(int)sizeof(url))return;
        }
        /* Libretro Neo Geo boxart filenames include revisions and subtitles.
         * Resolve the real filename from the official directory listing. */
        if(!console_icon){
            char *encoded=curl_easy_escape(NULL,system,0);
            if(encoded){
                char directory[512];
                snprintf(directory,sizeof(directory),
                    "https://thumbnails.libretro.com/%s/Named_Boxarts/",encoded);
                curl_free(encoded);
                Buffer listing={NULL,0,12*1024*1024};
                if(!fetch(directory,&listing)){
                    for(char *p=listing.data;(p=strstr(p,"href=\""))!=NULL;p+=6){
                        char *first=p+6,*last=strchr(first,'"');
                        if(!last||last-first>600)continue;
                        char href[640];memcpy(href,first,(size_t)(last-first));href[last-first]=0;
                        int decoded_len=0;
                        char *decoded=curl_easy_unescape(NULL,href,0,&decoded_len);
                        if(!decoded)continue;
                        char normalized_want[160],normalized_file[640];
                        size_t wn=art_normalize(sep,normalized_want,sizeof(normalized_want));
                        size_t fn=art_normalize(decoded,normalized_file,sizeof(normalized_file));
                        int match=wn>=4&&fn>=wn&&!strncmp(normalized_file,normalized_want,wn)
                            &&strstr(decoded,".png")!=NULL;
                        curl_free(decoded);
                        if(match){
                            size_t a=strlen(directory),b=strlen(href);
                            if(a+b<sizeof(url)){
                                memcpy(url,directory,a);
                                memcpy(url+a,href,b+1);
                                break;
                            }
                        }
                    }
                }
                free(listing.data);
            }
        }

        /* Prefer box covers; title images and snapshots are fallbacks. */
        fprintf(stderr,"FlXtR game art request: %s\n",url);
        int got_png=!fetch(url,&b)&&b.length>8&&!memcmp(b.data,"\x89PNG\r\n\x1a\n",8);
        /* Look up canonical names separately for title screens and snaps. */
        for(int alt=0;alt<2&&!got_png&&!console_icon;alt++){
            char *encoded=curl_easy_escape(NULL,system,0);
            char *title=curl_easy_escape(NULL,sep,0);
            if(!encoded||!title){curl_free(encoded);curl_free(title);break;}
            const char *type=alt==0?"Named_Titles":"Named_Snaps";
            char directory[512],next_url[1024];
            int dl=snprintf(directory,sizeof(directory),"https://thumbnails.libretro.com/%s/%s/",encoded,type);
            curl_free(encoded);
            if(dl<0||dl>=(int)sizeof(directory)){curl_free(title);continue;}
            int len=snprintf(next_url,sizeof(next_url),"%s%s.png",directory,title);
            curl_free(title);
            if(len<0||len>=(int)sizeof(next_url))continue;
            Buffer listing={NULL,0,12*1024*1024};
            if(!fetch(directory,&listing)){
                char wanted[160];size_t wn=art_normalize(sep,wanted,sizeof(wanted));
                for(char *p=listing.data;(p=strstr(p,"href=\""))!=NULL;p+=6){
                    char *first=p+6,*last=strchr(first,'"');
                    if(!last||last-first>600)continue;
                    char href[640];memcpy(href,first,(size_t)(last-first));href[last-first]=0;
                    int decoded_len=0;
                    char *decoded=curl_easy_unescape(NULL,href,0,&decoded_len);
                    if(!decoded)continue;
                    char found[640];size_t fn=art_normalize(decoded,found,sizeof(found));
                    int match=wn>=4&&fn>=wn&&!strncmp(wanted,found,wn)&&strstr(decoded,".png")!=NULL;
                    curl_free(decoded);
                    if(match){
                        size_t x=strlen(directory),z=strlen(href);
                        if(x+z<sizeof(next_url)){memcpy(next_url,directory,x);memcpy(next_url+x,href,z+1);break;}
                    }
                }
            }
            free(listing.data);
            free(b.data);b.data=NULL;b.length=0;
            got_png=!fetch(next_url,&b)&&b.length>8&&!memcmp(b.data,"\x89PNG\r\n\x1a\n",8);
        }
        if(got_png){
            SDL_RWops *rw=SDL_RWFromConstMem(b.data,(int)b.length);
            SDL_Surface *art=rw?IMG_Load_RW(rw,1):NULL;
            if(art){
                if(art->w>0&&art->h>0&&art->w<=2048&&art->h<=2048){
                    float scale=154.0f/art->w;
                    if(art->h*scale>231)scale=231.0f/art->h;
                    int w=(int)(art->w*scale),h=(int)(art->h*scale);
                    SDL_Surface *small=(w>0&&h>0)?SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888):NULL;
                    if(small&&!SDL_BlitScaled(art,NULL,small,NULL)){
                        char file[80];
                        snprintf(file,sizeof(file),"catalog-cache/art-%08lx-%d.bmp",cache_poster_hash,slot);
                        if(!SDL_SaveBMP(small,file))snprintf(e->poster,sizeof(e->poster),"%s",file);
                    }
                    SDL_FreeSurface(small);
                }
                SDL_FreeSurface(art);
            }
        }
        free(b.data);
        return;
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
                }char file[80];snprintf(file,sizeof(file),"catalog-cache/art-%08lx-%d.bmp",cache_poster_hash,slot);
                if(!SDL_SaveBMP(s,file))snprintf(e->poster,sizeof(e->poster),"%s",file);
                SDL_FreeSurface(s);
            }
        }
    }free(b.data);
}
/* Render game pages immediately. A detached worker warms persistent artwork
 * without delaying controller navigation; repeat views reuse the same BMP. */
static void game_art_path(const char *key,char *out,size_t capacity){
    unsigned long hash=2166136261UL;
    for(const unsigned char *p=(const unsigned char *)key;*p;p++)
        hash=((hash^*p)*16777619UL)&0xffffffffUL;
    snprintf(out,capacity,"game-art-cache/%08lx.bmp",hash);
}
static void game_art_prefetch(void){
    /* Queue missing covers from EVERY visited page. A single detached worker
     * drains this durable queue, including pages visited while it was busy. */
    mkdir("game-art-cache",0700);
    int queue=open("game-art-cache/.requests",O_CREAT|O_WRONLY|O_APPEND,0600);
    if(queue>=0){
        if(!flock(queue,LOCK_EX)){
            FILE *out=fdopen(dup(queue),"a");
            if(out){
                for(int i=0;i<used;i++)if(!strncmp(entries[i].poster,"gameart:",8)){
                    char file[80];game_art_path(entries[i].poster,file,sizeof(file));
                    struct stat st;
                    if(stat(file,&st)||st.st_size<54)fprintf(out,"%s\n",entries[i].poster);
                }
                fclose(out);
            }
            flock(queue,LOCK_UN);
        }
        close(queue);
    }
    int lock=open("game-art-cache/.worker.lock",O_CREAT|O_RDWR,0600);
    if(lock>=0&&!flock(lock,LOCK_EX|LOCK_NB)){
        catalog_http_reset(); /* Do not share a live HTTPS connection across fork. */
#ifdef _WIN32
        if(!flxtr_background_worker){close(lock);lock=-1;flxtr_background("art");}
        else
#else
        pid_t child=fork();
        if(child==0)
#endif
        {
#ifndef _WIN32
            int devnull=open("/dev/null",O_RDWR);
            if(devnull>=0){dup2(devnull,0);dup2(devnull,1);dup2(devnull,2);if(devnull>2)close(devnull);}
#endif
            for(int pass=0;pass<24;pass++){
                int fd=open("game-art-cache/.requests",O_RDWR|O_CREAT,0600);
                if(fd<0)break;
                char requests[8192];ssize_t n=0;
                if(!flock(fd,LOCK_EX)){
                    n=read(fd,requests,sizeof(requests)-1);
                    if(n>0&&ftruncate(fd,0)!=0)n=-1;
                    flock(fd,LOCK_UN);
                }
                close(fd);
                if(n<=0)break;
                requests[n]=0;
                char *save=NULL;
                for(char *line=strtok_r(requests,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
                    if(strncmp(line,"gameart:",8)||strlen(line)>400)continue;
                    char file[80];game_art_path(line,file,sizeof(file));
                    struct stat st;if(!stat(file,&st)&&st.st_size>=54)continue;
                    Entry e={0};snprintf(e.poster,sizeof(e.poster),"%s",line);
                    poster(&e,0);
                    if(strncmp(e.poster,"catalog-cache/",14))continue;
                    char temp[104];snprintf(temp,sizeof(temp),"%s.tmp.%ld",file,(long)getpid());
                    FILE *in=fopen(e.poster,"rb"),*out=in?fopen(temp,"wb"):NULL;
                    int good=in&&out;unsigned char buf[4096];size_t count;
                    if(good){
                        while((count=fread(buf,1,sizeof(buf),in))>0){
                            if(fwrite(buf,1,count,out)!=count){good=0;break;}
                        }
                        if(ferror(in))good=0;
                    }
                    if(in)fclose(in);
                    if(out&&fclose(out))good=0;
                    if(good)rename(temp,file);else unlink(temp);
                }
            }
            close(lock);_exit(0);
        }
        if(lock>=0)close(lock);
    }else if(lock>=0)close(lock);
    for(int i=0;i<used;i++)if(!strncmp(entries[i].poster,"gameart:",8)){
        char file[80];game_art_path(entries[i].poster,file,sizeof(file));
        snprintf(entries[i].poster,sizeof(entries[i].poster),"%s",file);
    }
}

/* Optional TheGamesDB lookups run after the listing is emitted, in a
 * detached worker; absent key means no network traffic. Never commit the key. */
static void game_info_prefetch(void){
    char key[160]="";
    FILE *keyfile=fopen("thegamesdb.key","r");
    if(!keyfile)return;
    if(!fgets(key,sizeof(key),keyfile)){fclose(keyfile);return;}
    fclose(keyfile);
    key[strcspn(key,"\r\n")]=0;
    size_t keylen=strlen(key);
    if(keylen<8||keylen>120)return;
    for(size_t i=0;i<keylen;i++)
        if(!isalnum((unsigned char)key[i])&&key[i]!='-'&&key[i]!='_')return;
    catalog_http_reset();
#ifdef _WIN32
    if(!flxtr_background_worker){flxtr_background("info");return;}
#else
    pid_t pid=fork();
    if(pid<0)return;
    if(pid>0){waitpid(pid,NULL,0);return;}
    pid_t detached=fork();
    if(detached!=0)_exit(0);
    int nullfd=open("/dev/null",O_RDWR);
    if(nullfd>=0){dup2(nullfd,0);dup2(nullfd,1);dup2(nullfd,2);if(nullfd>2)close(nullfd);}
#endif
    mkdir("media-info-cache",0700);
    for(int i=0;i<used;i++){
        Entry *e=&entries[i];
        if(strcmp(e->kind,"game")&&strcmp(e->kind,"game-zip"))continue;
        char output[96];game_info_path(e->title,output,sizeof(output));
        if(!access(output,R_OK))continue;
        char *name=curl_easy_escape(NULL,e->title,0);
        if(!name)continue;
        char url[1024];
        int n=snprintf(url,sizeof(url),
            "https://api.thegamesdb.net/v1.1/Games/ByGameName?apikey=%s&name=%s&fields=overview",key,name);
        curl_free(name);
        if(n<0||n>=(int)sizeof(url))continue;
        Buffer buffer={NULL,0,256*1024};
        if(fetch(url,&buffer)){free(buffer.data);continue;}
        json_object *root=json_tokener_parse(buffer.data?buffer.data:"");
        free(buffer.data);
        if(!root)continue;
        json_object *data=field(root,"data"),*games=data?field(data,"games"):NULL;
        const char *overview=NULL;
        if(games&&json_object_is_type(games,json_type_array)){
            size_t len=json_object_array_length(games);
            for(size_t j=0;j<len;j++){
                json_object *item=json_object_array_get_idx(games,j);
                const char *title=string(item,"game_title");
                if(!strcasecmp(title,e->title)){
                    const char *candidate=string(item,"overview");
                    if(strlen(candidate)>40){overview=candidate;break;}
                }
            }
        }
        if(overview){
            char description[400],tmp[112];
            clean(description,sizeof(description),overview);
            snprintf(tmp,sizeof(tmp),"%s.next",output);
            FILE *f=fopen(tmp,"w");
            if(f){fprintf(f,"%s\n",description);
                if(!fclose(f))rename(tmp,output);else unlink(tmp);
            }
        }
        json_object_put(root);
    }
    _exit(0);
}
#ifndef FLXTR_VERSION
#define FLXTR_VERSION "development"
#endif
#ifdef _WIN32
#define main flxtr_catalog_main
#endif
#include "core_geometry.h"
#include "rom_cheat_runtime.h"
int main(int argc,char **argv){
    games_executable=argv[0];
    if(argc==2&&!strcmp(argv[1],"--game-cover-index")){
        for(int page=1;page<=1+(GAME_LIMIT-1)/PAGE_SIZE;page++){
            used=0;if(games_list_filtered(page,0,0))return 1;
            for(int i=0;i<used;i++)if(!strncmp(entries[i].url,"romid:",6)&&entries[i].poster[0])
                printf("%s\t%s\t%s\n",entries[i].url+6,entries[i].title,entries[i].poster);
            if(page*PAGE_SIZE>=total)break;
        }
        return 0;
    }
    if(argc==7&&!strcmp(argv[1],"--probe-core"))return core_geometry_probe(argv[2],argv[3],argv[4],argv[5],!strcmp(argv[6],"native"));
#ifdef _WIN32
    if(argc==4&&!strcmp(argv[1],"--background")){
        if(strlen(argv[3])!=31||strncmp(argv[3],"catalog-cache/background-",25))return 2;
        for(int i=25;i<31;i++)if(!isalnum((unsigned char)argv[3][i]))return 2;
        FILE *in=fopen(argv[3],"rb");if(!in)return 2;
        int good=fread(&used,sizeof(used),1,in)==1&&used>=0&&used<=PAGE_SIZE&&fread(entries,sizeof(entries),1,in)==1;
        fclose(in);unlink(argv[3]);if(!good)return 2;
        flxtr_background_worker=1;if(curl_global_init(CURL_GLOBAL_DEFAULT))return 1;
        if(!strcmp(argv[2],"art"))game_art_prefetch();
        else if(!strcmp(argv[2],"info"))game_info_prefetch();
        else if(!strcmp(argv[2],"anime"))kiss_background_summaries();
        else return 2;
        return 0;
    }
#endif
#ifdef FLXTR_DESKTOP
    if(argc>=3&&(!strcmp(argv[1],"core-list")||!strcmp(argv[1],"core-install"))){
        int page=positive(argv[2],0);if(page<1||page>3||argc!=8)return 2;
        const char *install=!strcmp(argv[1],"core-install")?argv[3]:NULL;
#ifdef _WIN32
        char python[PATH_MAX],script[PATH_MAX];
        snprintf(python,sizeof(python),"%s/music-runtime/bin/python.exe",flxtr_win_binary_dir());
        snprintf(script,sizeof(script),"%s/tools/core_manager.py",flxtr_win_binary_dir());
        const char *args[]={"python","-I",script,install?"--install":"--list",install?install:"--page",install?"--page":argv[2],install?argv[2]:NULL,NULL};
        pid_t child=flxtr_win_spawn(python,args,NULL,NULL,NULL,0,1);
        if(child<0)return 1;
        int status=0;return waitpid(child,&status,0)==child?WEXITSTATUS(status):1;
#else
        desktop_host_environment();
        if(install)execlp("python3","python3","-I","tools/core_manager.py","--install",install,"--page",argv[2],(char*)NULL);
        else execlp("python3","python3","-I","tools/core_manager.py","--list","--page",argv[2],(char*)NULL);
        return 1;
#endif
    }
#endif
    if(argc==2&&!strcmp(argv[1],"--version")){puts(FLXTR_VERSION);return 0;}
    if(argc==2&&!strcmp(argv[1],"--run-game"))return games_run();
    if(argc!=7&&argc!=8){fprintf(stderr,"Usage: greenlink-catalog movie|tv|season|episode|source|quality PAGE QUERY ID SEASON EPISODE [SERVER]\n");return 2;}
    const char *kind=argv[1];int page=positive(argv[2],0),id=positive(argv[4],1),season=positive(argv[5],1),episode=positive(argv[6],1);
    if(page<1||page>2000||id<0||season<0||episode<0||strlen(argv[3])>(!strcmp(kind,"archive-files")||!strcmp(kind,"games-archive-files")||!strcmp(kind,"games-file-download")||!strcmp(kind,"games-zip-list")||!strcmp(kind,"games-zip-extract")?2047:64))return 2;
    if(strcmp(kind,"series-adjacent")&&strcmp(kind,"romsgames-platforms")&&strcmp(kind,"romsgames")&&strcmp(kind,"games-platforms")&&strcmp(kind,"games-bios-files")&&strcmp(kind,"games-bios-search")&&strcmp(kind,"games-archive-search")&&strcmp(kind,"games-archive-files")&&strcmp(kind,"games-file-download")&&strcmp(kind,"games-downloads-list")&&strcmp(kind,"games-downloads-delete")&&strcmp(kind,"games-zip-list")&&strcmp(kind,"games-zip-extract")&&strcmp(kind,"games-archive")&&strcmp(kind,"games-download")&&strcmp(kind,"games")&&strcmp(kind,"games-folders")&&strcmp(kind,"games-system")&&strcmp(kind,"movie")&&strcmp(kind,"tv")&&strcmp(kind,"season")&&strcmp(kind,"episode")&&strcmp(kind,"source")&&strcmp(kind,"quality")&&strcmp(kind,"kiss")&&strcmp(kind,"kiss-episodes")&&strcmp(kind,"kiss-source")&&strcmp(kind,"kiss-quality")&&strcmp(kind,"archive")&&strcmp(kind,"archive-folders")&&strcmp(kind,"archive-category")&&strcmp(kind,"archive-files")&&strcmp(kind,"tv-pick")&&strcmp(kind,"tv-break"))return 2;
    if((!strcmp(kind,"source")||!strcmp(kind,"quality"))&&!getenv("FLXTR_NO_NETWORK")&&!access("./greenlink-resolver",X_OK)){
        int quality=!strcmp(kind,"quality");if(quality&&argc!=8)return 2;
        execl("./greenlink-resolver","greenlink-resolver",quality?"source":"servers",episode?"tv":"movie",argv[4],argv[5],argv[6],quality?argv[7]:"",argv[2],(char*)NULL);return 1;
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 1;
    mkdir("catalog-cache",0700);
    int cacheable=strcmp(kind,"series-adjacent")&&strcmp(kind,"games-downloads-list")&&strcmp(kind,"games-downloads-delete")&&strcmp(kind,"games-zip-list")&&strcmp(kind,"games-zip-extract")&&strcmp(kind,"games-bios-search")&&strcmp(kind,"games-bios-files")&&strcmp(kind,"games-archive")&&strcmp(kind,"games-download")&&strcmp(kind,"games")&&strcmp(kind,"tv-break")&&strcmp(kind,"tv-pick")&&strcmp(kind,"source")&&strcmp(kind,"quality")&&strcmp(kind,"kiss-source")&&strcmp(kind,"kiss-quality");
    /* Game folders are local and cheap; always scan them so missing covers
     * can be queued even after page metadata was cached. */
    if(!strcmp(kind,"games-system")||!strcmp(kind,"games-folders"))cacheable=0;
    cache_identity(kind,page,argv[3],id,season);
    if(cacheable&&cache_load())goto output;
    if(!strncmp(kind,"games",5))fprintf(stderr,"FlXtR game art enabled=%d; catalog kind=%s\n",!getenv("FLXTR_NO_ART"),kind);
    int rc;
    if(!strcmp(kind,"games-platforms"))rc=games_platforms(page);
    else if(!strcmp(kind,"series-adjacent"))rc=series_adjacent(id,season,episode,!strcmp(argv[3],"previous")?-1:1);
    else if(!strcmp(kind,"romsgames-platforms"))rc=rg_platforms(page);
    else if(!strcmp(kind,"romsgames"))rc=rg_list(page,id);
    else if(!strcmp(kind,"games-archive"))rc=games_archive_list(page);
    else if(!strcmp(kind,"games-bios-search"))rc=games_bios_search(page,id,argv[3]);
    else if(!strcmp(kind,"games-bios-files"))rc=games_bios_files(page,argv[3],id);
    else if(!strcmp(kind,"games-archive-search"))rc=games_archive_search(page,id,argv[3]);
    else if(!strcmp(kind,"games-archive-files"))rc=games_archive_files(page,argv[3],id);
    else if(!strcmp(kind,"games-file-download")){
        char item[129],name[513];const char *separator=strchr(argv[3],'|');
        if(!separator||(size_t)(separator-argv[3])>=sizeof(item)||strlen(separator+1)>=sizeof(name))rc=-1;
        else{memcpy(item,argv[3],(size_t)(separator-argv[3]));item[separator-argv[3]]=0;snprintf(name,sizeof(name),"%s",separator+1);rc=games_archive_file_download(item,name,id);}
    }
    else if(!strcmp(kind,"games-downloads-list"))rc=downloaded_zip_list(page);
    else if(!strcmp(kind,"games-downloads-delete"))rc=downloaded_zip_delete(id);
    else if(!strcmp(kind,"games-zip-list"))rc=zip_game_by_id(season? -id:id,page,0);
    else if(!strcmp(kind,"games-zip-extract"))rc=zip_game_by_id(season? -id:id,1,episode);
    else if(!strcmp(kind,"games-download"))rc=games_archive_download(id);
    else if(!strcmp(kind,"games"))rc=games_list(page,id);
    else if(!strcmp(kind,"games-folders"))rc=games_folders(page);
    else if(!strcmp(kind,"games-system"))rc=games_system(page,id);
    else if(!strcmp(kind,"tv-break"))rc=archive_commercial(id);
    else if(!strcmp(kind,"tv-pick"))rc=tv_pick(argv[3],id,season);
    else if(!strcmp(kind,"archive-folders"))rc=archive_folders(page);
    else if(!strcmp(kind,"archive-category"))rc=archive_category_list(page,id,argv[3]);
    else if(!strcmp(kind,"archive"))rc=archive_list(page,argv[3]);
    else if(!strcmp(kind,"archive-files"))rc=archive_files(page,argv[3]);
    else if(!strcmp(kind,"kiss"))rc=kiss_list_filtered(page,argv[3],id,season);
    else if(!strcmp(kind,"kiss-episodes"))rc=kiss_episodes(id,page);
    else if(!strcmp(kind,"kiss-source"))rc=kiss_source(id,episode,"");
    else if(!strcmp(kind,"kiss-quality"))rc=kiss_source(id,episode,argc==8?argv[7]:"");
    else if(!strcmp(kind,"source"))rc=sources(page,id,season,episode);
    else if((!strcmp(kind,"movie")||!strcmp(kind,"tv"))&&!*argv[3]&&!id&&!season&&!local_list(kind,page,argv[3]))rc=0;
    else rc=remote_list(kind,page,argv[3],id,season);
    if(rc){catalog_http_close();return 1;}
    if(!strcmp(kind,"games")||!strcmp(kind,"games-system"))game_info_prefetch();
    /* Invalidate before replacing art: cancellation cannot pair old JSON with new posters. */
    if(cacheable)unlink(cache_file);
    if(!getenv("FLXTR_NO_ART")&&(!strcmp(kind,"games")||!strcmp(kind,"games-system")||!strcmp(kind,"games-folders")))game_art_prefetch();
    for(int i=0;i<used;i++){if(strcmp(kind,"games-platforms")&&strcmp(kind,"games-file-download")&&strcmp(kind,"games-download")&&strcmp(kind,"tv-pick")&&strcmp(kind,"tv-break")&&!getenv("FLXTR_NO_ART")){if(strncmp(entries[i].poster,"game-art-cache/",15))poster(&entries[i],i);}else entries[i].poster[0]=0;}
    /* Page-scoped posters change as the user browses. Snapshot them under
     * the full catalog identity before adding them to the Favorites index.
     * This also restores posters for anime Favorites whose saved field is empty. */
    for(int i=0;i<used;i++){
        Entry *e=&entries[i];
        if(strncmp(e->poster,"catalog-cache/",14)||strstr(e->poster,".."))continue;
        struct stat art;
        if(stat(e->poster,&art)||!S_ISREG(art.st_mode)||
           art.st_size<54||art.st_size>192*288*4+4096)continue;
        unsigned long hash=2166136261UL;
        const char *parts[]={e->kind,e->title,e->url};
        for(size_t k=0;k<sizeof(parts)/sizeof(parts[0]);k++)
            for(const unsigned char *p=(const unsigned char *)parts[k];*p;p++)
                hash=(hash^*p)*16777619UL;
        hash=(hash^(unsigned)e->id)*16777619UL;
        hash=(hash^(unsigned)e->season)*16777619UL;
        hash=(hash^(unsigned)e->episode)*16777619UL;
        mkdir("favorites-art",0700);
        char target[96],temp[112];
        snprintf(target,sizeof(target),"favorites-art/%08lx.bmp",hash&0xffffffffUL);
        snprintf(temp,sizeof(temp),"%s.next",target);
        FILE *src=fopen(e->poster,"rb"),*dst=src?fopen(temp,"wb"):NULL;
        int good=src&&dst;unsigned char bytes[4096];size_t amount;
        if(good){
            while((amount=fread(bytes,1,sizeof(bytes),src))>0)
                if(fwrite(bytes,1,amount,dst)!=amount){good=0;break;}
            if(ferror(src))good=0;
        }
        if(src)fclose(src);
        if(dst&&fclose(dst))good=0;
        if(good&&!rename(temp,target))snprintf(e->poster,sizeof(e->poster),"%s",target);
        else unlink(temp);
    }
    /* Store only identity-bound, stable image paths for Favorites recovery.
     * Page-scoped catalog-cache bitmaps are reused and must never be indexed. */
    if(used>0&&strcmp(kind,"games-platforms")&&strcmp(kind,"games-folders")){
        FILE *index=fopen("favorites-art-index.tsv","a");
        if(index){
            for(int i=0;i<used;i++){
                Entry *e=&entries[i];
                if((!strncmp(e->poster,"game-art-cache/",15)||
                    !strncmp(e->poster,"favorites-art/",14))&&
                   !strstr(e->poster,"..")&&
                   !strchr(e->title,'\t')&&!strchr(e->title,'\n')&&
                   !strchr(e->kind,'\t')&&!strchr(e->kind,'\n')&&
                   !strchr(e->poster,'\t')&&!strchr(e->poster,'\n'))
                    fprintf(index,"%s\t%d\t%d\t%d\t%s\t%s\n",
                        e->kind,e->id,e->season,e->episode,e->title,e->poster);
            }
            fclose(index);
        }
    }
    if(cacheable)cache_save();
output:
    printf("# pages=%d total=%d\n",total?(total+PAGE_SIZE-1)/PAGE_SIZE:1,total);
    for(int i=0;i<used;i++){Entry *e=&entries[i];if(!strcmp(e->kind,"source")&&(e->subs_eng[0]||e->subs_nor[0]||e->subs_spa[0]))
            printf("%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t0\t%s\t%s\t%s\n",e->title,e->meta,e->poster,e->url,e->kind,e->id,e->season,e->episode,e->subs_eng,e->subs_nor,e->subs_spa);
        else printf("%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n",e->title,e->meta,e->poster,e->url,e->kind,e->id,e->season,e->episode);}
    catalog_http_close();return ferror(stdout)?1:0;
}

#ifdef _WIN32
#undef main
FLXTR_WINDOWS_CONSOLE_ENTRY(flxtr_catalog_main)
#endif
