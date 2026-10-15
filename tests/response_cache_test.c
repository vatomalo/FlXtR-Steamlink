#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
#include <utime.h>
int main(void){
    char dir[]="/tmp/flxtr-response-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    const char *url=ROMSGAMES "/roms/super-nintendo/?page=1&sort=popularity";
    const char *body="<a href='/snes-rom-demo/'><img alt='Demo'></a>";
    assert(!response_cache_load(url));response_cache_store(url,body);
    char *s=response_cache_load(url);assert(s&&!strcmp(s,body));free(s);
    assert(!rg_list(1,2)&&used==1&&!strcmp(entries[0].title,"Demo"));
    response_cache_store(API "/tv/100","{\"id\":100,\"name\":\"Cached show\"}");
    json_object *show=get_json("/tv/100");assert(show&&number(show,"id")==100);json_object_put(show);
    char path[80];response_cache_path(url,path,sizeof(path));
    struct utimbuf old={time(NULL)-RESPONSE_CACHE_TTL-1,time(NULL)-RESPONSE_CACHE_TTL-1};assert(!utime(path,&old));assert(!response_cache_load(url));
    response_cache_store(url,body);FILE *f=fopen(path,"w");assert(f);fputs("{broken",f);fclose(f);assert(!response_cache_load(url));
    response_cache_store(url,body);
    char other[128],other_path[80];int collision=0;
    for(int i=0;i<1000;i++){snprintf(other,sizeof(other),"https://example.invalid/%d",i);response_cache_path(other,other_path,sizeof(other_path));if(!strcmp(path,other_path)){collision=1;break;}}
    assert(collision);assert(!response_cache_load(other));response_cache_store(other,"different");assert(!response_cache_load(url));
    char *huge=malloc(RESPONSE_CACHE_LIMIT+2);assert(huge);memset(huge,'x',RESPONSE_CACHE_LIMIT+1);huge[RESPONSE_CACHE_LIMIT+1]=0;response_cache_store(url,huge);free(huge);assert(!response_cache_load(url));
    DIR *d=opendir("catalog-cache");assert(d);struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.'){char file[512];snprintf(file,sizeof(file),"catalog-cache/%s",e->d_name);unlink(file);}closedir(d);rmdir("catalog-cache");assert(!chdir("/tmp"));rmdir(dir);
    puts("Public response cache: reuse, expiry, corruption, collision and size bounds passed");return 0;
}
