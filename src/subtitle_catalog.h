#ifndef FLXTR_SUBTITLE_CATALOG_H
#define FLXTR_SUBTITLE_CATALOG_H
/* Every caption track a resolver meets, in any language, written beside the
 * source for the player's menu: code<TAB>name<TAB>url. */
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "subtitle_languages.h"
#define SUBTITLE_CATALOG_MAX 64
#define SUBTITLE_CATALOG_PER_LANGUAGE 3
typedef struct {int language;char name[40],url[1024];} SubtitleCatalogTrack;
static SubtitleCatalogTrack subtitle_catalog[SUBTITLE_CATALOG_MAX];
static int subtitle_catalog_count;
static void subtitle_catalog_add(const char *language,const char *label,const char *url){
    if(!url||strncmp(url,"https://",8)||strlen(url)>=sizeof(subtitle_catalog[0].url)||strpbrk(url,"\r\n\t "))return;
    int index=subtitle_language_index(language);
    if(index<0)index=subtitle_language_index(label);
    const char *shown=label&&*label?label:language&&*language?language:"";
    if(index<0&&!*shown)return;
    int same=0;
    for(int i=0;i<subtitle_catalog_count;i++){
        if(!strcmp(subtitle_catalog[i].url,url))return;
        same+=subtitle_catalog[i].language==index&&index>=0;
    }
    if(same>=SUBTITLE_CATALOG_PER_LANGUAGE||subtitle_catalog_count>=SUBTITLE_CATALOG_MAX)return;
    SubtitleCatalogTrack *t=&subtitle_catalog[subtitle_catalog_count++];
    t->language=index;snprintf(t->url,sizeof(t->url),"%s",url);
    /* The menu's font is ASCII: a known language shows its own name. */
    size_t n=0;
    const char *from=index>=0?subtitle_language_table[index].name:shown;
    for(;*from&&n+1<sizeof(t->name);from++)
        if((unsigned char)*from>=32&&(unsigned char)*from<127)t->name[n++]=(char)toupper((unsigned char)*from);
    t->name[n]=0;
    if(index>=0){
        char lower[64];size_t k=0;
        for(const char *p=shown;*p&&k+1<sizeof(lower);p++)lower[k++]=(char)tolower((unsigned char)*p);
        lower[k]=0;
        const char *extra=strstr(lower,"sdh")||strstr(lower,"hearing")?" SDH":
            strstr(lower,"forced")?" FORCED":strstr(lower,"brazil")||strstr(lower,"br)")||!strcmp(lower,"br_pt")||!strcmp(lower,"pt-br")?" (BR)":
            strstr(lower,"latin")?" (LATIN)":"";
        if(*extra)snprintf(t->name+n,sizeof(t->name)-n,"%s",extra);
    }
    if(!t->name[0])snprintf(t->name,sizeof(t->name),"OTHER");
}
static void subtitle_catalog_prune(void){
    DIR *d=opendir("subtitle-tracks");if(!d)return;
    struct dirent *e;time_t now=time(NULL);
    while((e=readdir(d))){
        if(e->d_name[0]=='.'||strlen(e->d_name)>60)continue;
        char path[96];snprintf(path,sizeof(path),"subtitle-tracks/%.60s",e->d_name);
        struct stat s;if(!stat(path,&s)&&S_ISREG(s.st_mode)&&now-s.st_mtime>2*86400)unlink(path);
    }
    closedir(d);
}
static int subtitle_catalog_write(const char *media_url){
    if(!media_url||!*media_url)return 0;
    char path[96],temp[112];subtitle_tracks_path(media_url,path,sizeof(path));
    if(!subtitle_catalog_count){unlink(path);return 0;}
#ifdef _WIN32
    mkdir("subtitle-tracks");
#else
    mkdir("subtitle-tracks",0700);
#endif
    subtitle_catalog_prune();
    snprintf(temp,sizeof(temp),"%s.%ld",path,(long)getpid());
    FILE *f=fopen(temp,"w");if(!f)return 0;
    for(int i=0;i<subtitle_catalog_count;i++){
        const SubtitleCatalogTrack *t=&subtitle_catalog[i];
        fprintf(f,"%s\t%s\t%s\n",t->language>=0?subtitle_language_table[t->language].code:"und",t->name,t->url);
    }
    if(fclose(f)||rename(temp,path)){unlink(temp);return 0;}
    return subtitle_catalog_count;
}
#endif
