/* Public website adapters. TMDB identities come from the existing catalogue.
 * Browser embed pages are never passed to FFmpeg as video sources. */
#ifndef FLXTR_WEB_PROVIDERS_H
#define FLXTR_WEB_PROVIDERS_H
#include "provider_order.h"
#define FMOVIES_PLAYER "https://videm.buzz"
static int web_extract(const char *text,const char *start,char end,char *out,size_t cap){
    const char *p=strstr(text,start);if(!p)return 0;p+=strlen(start);
    const char *q=strchr(p,end);if(!q||q==p||(size_t)(q-p)>=cap)return 0;
    memcpy(out,p,(size_t)(q-p));out[q-p]=0;return !strpbrk(out,"\r\n");
}
static char *web_get(const char *url,const char *referer,const char *key,const char *stage){
    if(strncmp(url,"https://",8)||!valid_url(url))return NULL;
    Buffer b={NULL,0,2*1024*1024};CURL *c=curl_easy_init();if(!c)return NULL;
    struct curl_slist *headers=NULL;
    if(key)headers=header(headers,"x-player-key",key);
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(c,CURLOPT_REFERER,referer);curl_easy_setopt(c,CURLOPT_USERAGENT,"Mozilla/5.0");
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,4L);curl_easy_setopt(c,CURLOPT_TIMEOUT,12L);
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(c,CURLOPT_CAINFO,FLXTR_CA_BUNDLE);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    CURLcode rc=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(c);curl_slist_free_all(headers);
    if(rc!=CURLE_OK||code!=200){fprintf(stderr,"%s failed (HTTP %ld, curl %d)\n",stage,code,(int)rc);free(b.data);return NULL;}return b.data;
}
static int web_source_at(json_object *source,const char *provider,const char *origin){
    const char *url=string(source,"url");if(!*url)url=string(source,"file");
    char absolute[2048];
    if(origin&&url[0]=='/'&&url[1]!='/'){
        int n=snprintf(absolute,sizeof(absolute),"%s%s",origin,url);if(n<=0||n>=(int)sizeof(absolute))return 0;url=absolute;
    }
    if(strncmp(url,"https://",8)||!valid_url(url))return 0;
    memset(choices,0,sizeof(choices));count=1;strcpy(choices[0].label,"AUTO");
    snprintf(choices[0].meta,sizeof(choices[0].meta),"%s / BEST COMPATIBLE",provider);
    snprintf(choices[0].url,sizeof(choices[0].url),"%s",url);
    select_subtitles(field(source,"tracks"),choices[0].subs_eng,choices[0].subs_nor,choices[0].subs_spa);
    return 1;
}
static int web_source(json_object *source,const char *provider){return web_source_at(source,provider,NULL);}
static json_object *web_assignment(const char *page,const char *marker){
    const char *p=strstr(page,marker);return p?parse(p+strlen(marker)):NULL;
}
static int web_sflix(int id,int season,int episode){
    /* SFlix server 1 is MoviesAPI, now redirected to VidSpark. Read the same
     * public player header from its current application bundle, not a fixed key. */
    char path[160],url[2048],asset[256],key[128];
    if(episode)snprintf(path,sizeof(path),"tv/%d/%d/%d",id,season,episode);
    else snprintf(path,sizeof(path),"movie/%d",id);
    snprintf(url,sizeof(url),"https://vidspark.to/%s",path);
    char *page=web_get(url,"https://ssflix.pro/",NULL,"SFlix player page request");if(!page)return 0;
    int found=web_extract(page,"src=\"/assets/index-",'"',asset,sizeof(asset));free(page);
    if(!found){fprintf(stderr,"SFlix player asset discovery failed\n");return 0;}
    /* Only the site's own asset path is accepted. */
    for(const char *p=asset;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_'&&*p!='.')return 0;
    snprintf(url,sizeof(url),"https://vidspark.to/assets/index-%s",asset);
    char *script=web_get(url,"https://vidspark.to/",NULL,"SFlix player asset request");if(!script)return 0;
    found=web_extract(script,"var fz=`",'`',key,sizeof(key));free(script);
    if(!found){fprintf(stderr,"SFlix player key discovery failed\n");return 0;}
    for(const char *p=key;*p;p++)if(!isxdigit((unsigned char)*p))return 0;
    snprintf(url,sizeof(url),"https://vidspark.to/api/vidora/v1/%s",path);
    char *raw=web_get(url,"https://vidspark.to/",key,"SFlix source API request");json_object *root=parse(raw);free(raw);
    json_object *sources=field(root,"sources");int ok=0;
    if(root&&json_object_get_boolean(field(root,"result"))&&sources&&json_object_is_type(sources,json_type_array)&&json_object_array_length(sources))
        ok=web_source(json_object_array_get_idx(sources,0),"SFLIX");
    if(!ok)fprintf(stderr,"SFlix source API returned no compatible source\n");
    if(root)json_object_put(root);
    return ok;
}
static int web_fmovies(int id,int season,int episode){
    /* FMovies server 3 -> VidAPI XPS -> VIDEM. The public embed supplies
     * short-lived source references. Tokens and media URLs are never logged. */
    char embed[256];
    if(episode)snprintf(embed,sizeof(embed),FMOVIES_PLAYER"/embed/tv/%d/%d/%d",id,season,episode);
    else snprintf(embed,sizeof(embed),FMOVIES_PLAYER"/embed/movie/%d",id);
    char *page=web_get(embed,"https://www.f-moviess.org/",NULL,"FMovies player page request");if(!page)return 0;
    json_object *q=web_assignment(page,"var Q = ");free(page);if(!q){fprintf(stderr,"FMovies player metadata discovery failed\n");return 0;}
    const char *token=string(q,"t");json_object *rows=field(field(q,"ssr"),"servers");int ok=0;
    CURL *escape=curl_easy_init();
    if(escape&&*token&&rows&&json_object_is_type(rows,json_type_array)){
        for(size_t i=0;i<(size_t)json_object_array_length(rows)&&i<2&&!ok;i++){
            const char *ref=string(json_object_array_get_idx(rows,i),"ref");
            if(!*ref||strlen(ref)>1024||strlen(token)>1024)continue;
            char *r=curl_easy_escape(escape,ref,0),*t=curl_easy_escape(escape,token,0);
            if(r&&t){char url[8192];int n=snprintf(url,sizeof(url),FMOVIES_PLAYER"/api.php?a=play&ref=%s&t=%s",r,t);
                if(n>0&&n<2048){char *raw=web_get(url,embed,NULL,"FMovies source API request");json_object *source=parse(raw);free(raw);
                    if(source){ok=web_source_at(source,"FMOVIES",FMOVIES_PLAYER);json_object_put(source);}}}
            curl_free(r);curl_free(t);
        }
    }
    if(escape)curl_easy_cleanup(escape);
    json_object_put(q);if(!ok)fprintf(stderr,"FMovies source API returned no compatible source\n");return ok;
}
static int web_provider(const char *provider,int id,int season,int episode){
    int ok=!strcmp(provider,"fmovies")?web_fmovies(id,season,episode):web_sflix(id,season,episode);
    if(!ok){count=0;fprintf(stderr,"%s returned no native-playable source for TMDB %d S%d E%d\n",provider,id,season,episode);return 1;}
    char *manifest=web_get(choices[0].url,!strcmp(provider,"sflix")?"https://vidspark.to/":FMOVIES_PLAYER"/",NULL,!strcmp(provider,"sflix")?"SFlix HLS playlist request":"FMovies HLS playlist request");
    if(!manifest||strncmp(manifest,"#EXTM3U",7)){free(manifest);count=0;fprintf(stderr,"%s did not return a usable HLS playlist\n",provider);return 1;}
    add_variants(manifest,choices[0].url,!strcmp(provider,"sflix")?"SFLIX":"FMOVIES");free(manifest);return 0;
}
static void web_servers(void){
    if(count>38)count=38;
    server_choice("fmovies");snprintf(choices[count-1].meta,sizeof(choices[0].meta),"FMOVIES / f-moviess.org");
    server_choice("sflix");snprintf(choices[count-1].meta,sizeof(choices[0].meta),"SFLIX / ssflix.pro");
}
#endif
