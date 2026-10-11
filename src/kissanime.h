#define KISS "https://kissanime.com.cv"
/* Bounded HTML adapters for the site's public catalog and episode embeds.
 * No scripts, adverts, browser state or obfuscated code are executed. */
static void kiss_unescape(char *out,size_t cap,const char *in){
    size_t n=0;
    while(*in&&n+1<cap){
        if(!strncmp(in,"&amp;",5)){out[n++]='&';in+=5;}
        else if(!strncmp(in,"&quot;",6)){out[n++]='"';in+=6;}
        else if(!strncmp(in,"&#039;",6)){out[n++]='\'';in+=6;}
        else if(!strncmp(in,"&apos;",6)){out[n++]='\'';in+=6;}
        else if(!strncmp(in,"&#",2)){char *end;long ch=strtol(in+2,&end,10);if(*end==';'){out[n++]=ch>=32&&ch<127?(char)ch:'?';in=end+1;}else out[n++]=*in++;}
        else if(in[0]=='\\'&&in[1]=='/'){out[n++]='/';in+=2;}
        else {unsigned char c=(unsigned char)*in++;if(c>=128){if((c&0xc0)!=0x80)out[n++]='?';}else out[n++]=c<32?' ':(char)c;}
    }out[n]=0;
}
static int kiss_attr(const char *tag,const char *name,char *out,size_t cap){
    const char *end=strchr(tag,'>');out[0]=0;if(!end)return 0;size_t len=strlen(name);
    for(const char *p=tag;p<end;p++)if((p==tag||isspace((unsigned char)p[-1]))&&!strncmp(p,name,len)){
        const char *v=p+len;while(v<end&&isspace((unsigned char)*v))v++;
        if(v>=end||*v++!='=')continue;
        while(v<end&&isspace((unsigned char)*v))v++;
        if(v>=end||(*v!='"'&&*v!='\''))continue;
        char quote=*v++;const char *finish=memchr(v,quote,(size_t)(end-v));if(!finish||finish-v>=2048)return 0;
        char raw[2048];memcpy(raw,v,(size_t)(finish-v));raw[finish-v]=0;kiss_unescape(out,cap,raw);return 1;
    }return 0;
}
static char *kiss_fetch(const char *url){Buffer b={NULL,0,2*1024*1024};if(fetch(url,&b)){free(b.data);return NULL;}return b.data;}
static int kiss_is_promo(const char *description){
    if(!description||strlen(description)<45)return 1;
    return strcasestr(description,"watch and download")!=NULL||
           strcasestr(description,"watch online")!=NULL||
           strcasestr(description,"download free")!=NULL||
           strcasestr(description,"english sub")!=NULL||
           strcasestr(description,"all episodes")!=NULL||
           strcasestr(description,"watch anime")!=NULL||
           strcasestr(description,"free streaming")!=NULL;
}
static void kiss_cache_description(int id,const char *html){
    if(id<=0||!html)return;
    char description[400]="";
    const char *meta=html;
    while((meta=strstr(meta,"<meta"))!=NULL){
        const char *end=strchr(meta,'>');
        if(!end)break;
        char name[80]="",property[80]="";
        kiss_attr(meta,"name",name,sizeof(name));
        kiss_attr(meta,"property",property,sizeof(property));
        if(!strcasecmp(name,"description")||!strcasecmp(property,"og:description")){
            kiss_attr(meta,"content",description,sizeof(description));
            if(description[0]&&!kiss_is_promo(description))break;
            description[0]=0;
        }
        meta=end+1;
    }
    /* Alternate anime detail layouts store their synopsis in a paragraph. */
    if(!description[0]){
        const char *section=strstr(html,"class=\"synp\"");
        if(!section)section=strstr(html,"class=\"entry-content\"");
        if(section){
            const char *p=strchr(section,'>');
            if(p){
                p++;
                char raw[1024];size_t n=0;
                while(*p&&n+1<sizeof(raw)&&strncmp(p,"</p>",4)&&strncmp(p,"</div>",6)){
                    if(*p=='<'){const char *close=strchr(p,'>');if(!close)break;p=close+1;
                        if(n&&raw[n-1]!=' ')raw[n++]=' ';
                    }else raw[n++]=*p++;
                }
                raw[n]=0;
                kiss_unescape(description,sizeof(description),raw);
            }
        }
    }
    if(kiss_is_promo(description))return;
    mkdir("media-info-cache",0700);
    char dest[100],temp[120];
    snprintf(dest,sizeof(dest),"media-info-cache/anime-%d.txt",id);
    snprintf(temp,sizeof(temp),"%s.next",dest);
    FILE *f=fopen(temp,"w");
    if(f){fprintf(f,"%s\n",description);
        if(!fclose(f))rename(temp,dest);else unlink(temp);
    }
}
static int kiss_cards(char *html,int first){
    char *p=strstr(html,"class=\"listupd\"");int n=0;if(!p)return -1;
    char *end=strstr(p,"class=\"hpage\"");if(!end)end=strstr(p,"id=\"sidebar\"");if(!end)end=p+strlen(p);
    while((p=strstr(p,"<article"))&&p<end){
        char *stop=strstr(p,"</article>");if(!stop||stop>end)break;
        char *a=strstr(p,"<a ");char title[80],ident[32],href[512];
        if(a&&a<stop&&kiss_attr(a,"href",href,sizeof(href))&&!strncmp(href,KISS"/anime/",sizeof(KISS"/anime/")-1)&&
           kiss_attr(a,"rel",ident,sizeof(ident))&&positive(ident,0)>0&&kiss_attr(a,"title",title,sizeof(title))){
            if(n>=first&&used<PAGE_SIZE){Entry *e=&entries[used++];strcpy(e->title,title);strcpy(e->kind,"kiss");e->id=positive(ident,0);
                strcpy(e->meta,"KISSANIME / EPISODES");char *img=strstr(a,"<img ");if(img&&img<stop)kiss_attr(img,"src",e->poster,sizeof(e->poster));}
            n++;
        }p=stop+10;
    }return n;
}
static int kiss_list_filtered(int page,const char *query,int genre,int order){
    if(genre<0||genre>=BROWSE_GENRES||order<0||order>=BROWSE_ORDERS)return -1;
    int first=(page-1)*PAGE_SIZE,last=(first+PAGE_SIZE-1)/20+1;char *escaped=curl_easy_escape(NULL,query,0);if(!escaped)return -1;
    for(int remote=first/20+1;remote<=last;remote++){
        char url[768];if(*query)snprintf(url,sizeof(url),KISS"/page/%d/?s=%s",remote,escaped);
        else snprintf(url,sizeof(url),KISS"/anime/?page=%d&order=%s%s%s",remote,kiss_orders[order],genre?"&genre%5B%5D=":"",kiss_genres[genre]);
        char *html=kiss_fetch(url);if(!html){curl_free(escaped);return -1;}
        int n=kiss_cards(html,first>(remote-1)*20?first-(remote-1)*20:0);
        int more=strstr(html,">Next")||strstr(html,"Next <i");free(html);
        if(n<0){curl_free(escaped);return -1;}
        total=(remote-1)*20+n+(more?1:0);if(!more)break;
    }curl_free(escaped);
    /* Fetch missing anime summaries separately, never blocking catalog navigation.
     * Use a detached worker so page selection stays instantaneous. */
    if(getenv("FLXTR_ENABLE_ANIME_INFO")){
    pid_t worker=fork();
    if(worker==0){
        pid_t detached=fork();
        if(detached<0)_exit(0);
        if(detached>0)_exit(0);
        int devnull=open("/dev/null",O_RDWR);
        if(devnull>=0){dup2(devnull,0);dup2(devnull,1);dup2(devnull,2);if(devnull>2)close(devnull);}
        for(int i=0;i<used;i++){
            if(strcmp(entries[i].kind,"kiss")||entries[i].id<=0)continue;
            char cached[100],url[128];
            snprintf(cached,sizeof(cached),"media-info-cache/anime-%d.txt",entries[i].id);
            FILE *old=fopen(cached,"r");
            if(old){char content[400]="";if(!fgets(content,sizeof(content),old))content[0]=0;fclose(old);
                if(!kiss_is_promo(content))continue;
                unlink(cached);
            }
            snprintf(url,sizeof(url),KISS"/?post_type=anime&p=%d",entries[i].id);
            char *html=kiss_fetch(url);
            if(html){kiss_cache_description(entries[i].id,html);free(html);}
        }
        _exit(0);
    }
    if(worker>0)waitpid(worker,NULL,0);
    }
    return 0;
}
static inline int kiss_list(int page,const char *query){return kiss_list_filtered(page,query,0,0);}

/* Cache an anime synopsis while the existing episode-page request is open.
 * No extra network call on browsing, and no scripts or embeds are evaluated. */
typedef struct {int id,number;} KissEpisode;
static int kiss_episode_compare(const void *a,const void *b){return ((const KissEpisode*)a)->number-((const KissEpisode*)b)->number;}
static int kiss_episodes(int id,int page){
    char url[128];snprintf(url,sizeof(url),KISS"/?post_type=anime&p=%d",id);char *html=kiss_fetch(url);if(!html)return -1;
    kiss_cache_description(id,html);
    KissEpisode *episodes=calloc(5000,sizeof(*episodes));if(!episodes){free(html);return -1;}
    int n=0;char *p=html;
    while((p=strstr(p,"<a "))&&n<5000){char cls[128],number[32],ident[32];
        if(kiss_attr(p,"class",cls,sizeof(cls))&&strstr(cls,"ep-item")&&kiss_attr(p,"data-number",number,sizeof(number))&&kiss_attr(p,"data-id",ident,sizeof(ident))){
            int ep=positive(number,1),post=positive(ident,0);if(ep>=0&&post>0)episodes[n++]=(KissEpisode){post,ep};
        }p+=3;
    }
    qsort(episodes,(size_t)n,sizeof(*episodes),kiss_episode_compare);total=0;
    for(int i=0;i<n;i++){
        if(i&&episodes[i].id==episodes[i-1].id)continue;
        if(total++<(page-1)*PAGE_SIZE||used==PAGE_SIZE)continue;
        Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"EPISODE %d",episodes[i].number);strcpy(e->meta,"KISSANIME / AUTOMATIC SOURCE");
        strcpy(e->kind,"kiss-episode");e->id=episodes[i].id;e->episode=episodes[i].number;
    }free(episodes);free(html);return 0;
}
static int kiss_direct(const char *html,char *out,size_t cap){
    /* Plain video/source tags and standard player file fields only. */
    const char *p=html;
    while((p=strstr(p,"<source"))){char url[2048];if(kiss_attr(p,"src",url,sizeof(url))&&!strncmp(url,"https://",8)&&(strstr(url,".m3u8")||strstr(url,".mp4"))){snprintf(out,cap,"%s",url);return 1;}p+=7;}
    const char *keys[]={"\"file\"","file:","\"src\""};
    for(int k=0;k<3;k++){p=html;while((p=strstr(p,keys[k]))){p+=strlen(keys[k]);while(*p&&(*p==':'||isspace((unsigned char)*p)))p++;
        if(*p!='\''&&*p!='"')continue;
        char quote=*p++;const char *end=strchr(p,quote);if(!end||end-p>=2048)continue;
        char raw[2048],url[2048];memcpy(raw,p,(size_t)(end-p));raw[end-p]=0;kiss_unescape(url,sizeof(url),raw);
        if(!strncmp(url,"https://",8)&&(strstr(url,".m3u8")||strstr(url,".mp4"))){snprintf(out,cap,"%s",url);return 1;}
    }}return 0;
}
#include "megaplay.h"
static int kiss_source(int id,int episode,const char *server){
    char url[2048];snprintf(url,sizeof(url),KISS"/?p=%d",id);char *html=kiss_fetch(url);if(!html)return -1;
    char *embed=strstr(html,"id=\"pembed\"");embed=embed?strstr(embed,"<iframe"):NULL;
    int ok=embed&&kiss_attr(embed,"src",url,sizeof(url));
    if(!ok){free(html);return -1;}
    if(!*server){
        const char *names[]={"SUB","DUB"};for(int i=0;i<2;i++){if(i&&!strstr(html,">HD-DUB</a>"))continue;
            Entry *e=&entries[used++];strcpy(e->title,names[i]);strcpy(e->kind,"server");e->id=id;e->episode=episode;strcpy(e->meta,"KISSANIME");}
        total=used;free(html);return 0;
    }
    free(html);
    if(strcmp(server,"SUB")&&strcmp(server,"DUB"))return -1;
    char referer[2048];snprintf(referer,sizeof(referer),KISS"/?p=%d",id);
    if(!strncmp(url,"https://gogoanime.com.by/streaming.php?ep=",sizeof("https://gogoanime.com.by/streaming.php?ep=")-1)){
        char *type=strstr(url,"&type=");if(type)snprintf(type,(size_t)(url+sizeof(url)-type),"&type=%s",!strcmp(server,"DUB")?"dub":"sub");
    }
    for(int hop=0;hop<3;hop++){
        if(strncmp(url,"https://gogoanime.com.by/",sizeof("https://gogoanime.com.by/")-1)&&strncmp(url,"https://megaplay.buzz/",sizeof("https://megaplay.buzz/")-1)){fprintf(stderr,"Unsupported KissAnime embed host\n");return -1;}
        Buffer b={NULL,0,2*1024*1024};
        if(fetch_referred(url,&b,referer)){free(b.data);return -1;}
        html=b.data;if(!html)return -1;
        snprintf(referer,sizeof(referer),"%s",url);
        char direct[2048];
        char sub_en[1024]={0},sub_no[1024]={0},sub_es[1024]={0};
        int resolved=kiss_direct(html,direct,sizeof(direct));
        if(!resolved&&!strncmp(url,"https://megaplay.buzz/",22))resolved=mega_source(html,referer,direct,sizeof(direct),sub_en,sub_no,sub_es);
        if(resolved){Entry *e=&entries[used++];strcpy(e->title,"AUTO");strcpy(e->meta,"KISSANIME");strcpy(e->kind,"source");strcpy(e->url,direct);strcpy(e->subs_eng,sub_en);strcpy(e->subs_nor,sub_no);strcpy(e->subs_spa,sub_es);e->id=id;e->episode=episode;total=1;free(html);return 0;}
        if(strstr(html,"id=\"megaplay-player\"")&&strstr(html,"data-realid=")){
            fprintf(stderr,"KissAnime: MegaPlay source API failed or returned an unsupported response\n");
            free(html);return -1;
        }
        embed=strstr(html,"<iframe");ok=embed&&kiss_attr(embed,"src",url,sizeof(url));free(html);if(!ok)break;
    }
    fprintf(stderr,"KissAnime: no supported media URL found in embed HTML; this does not establish that the video is unavailable\n");return -1;
}
