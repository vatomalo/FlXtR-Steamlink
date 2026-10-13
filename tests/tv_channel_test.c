#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <json-c/json.h>
typedef struct {char title[80],meta[96],poster[512],kind[16];int id,season,episode;} Entry;
static Entry entries[6];static int used,total;
static json_object *field(json_object *o,const char *k){json_object *v=NULL;json_object_object_get_ex(o,k,&v);return v;}
static int number(json_object *o,const char *k){return json_object_get_int(field(o,k));}
static const char *string(json_object *o,const char *k){const char *s=json_object_get_string(field(o,k));return s?s:"";}
static void clean(char *out,size_t n,const char *in){snprintf(out,n,"%s",in);}
static json_object *get_json(const char *path){
    if(strstr(path,"/discover/"))return json_tokener_parse("{\"results\":[{\"id\":42,\"name\":\"Show\",\"vote_average\":8,\"vote_count\":1000}]}");
    if(strstr(path,"/season/2"))return json_tokener_parse("{\"episodes\":[{\"episode_number\":1,\"runtime\":50,\"name\":\"Season two\"}]}");
    if(strstr(path,"/season/"))return json_tokener_parse("{\"episodes\":[{\"episode_number\":1,\"runtime\":44},{\"episode_number\":2,\"runtime\":24},{\"episode_number\":3,\"runtime\":30}]}");
    return json_tokener_parse("{\"id\":42,\"name\":\"Show\"}");
}
#include "../src/tv_catalog.h"
int main(void){
    char dir[]="/tmp/flxtr-channel-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    TvAiring a;
    assert(tv_pick("2:100000:101800:1:1:1",1,0)==0); /* 15:00 -> 15:30 */
    assert(tv_airing_read(&a)&&a.start==100000&&a.episode==1);
    assert(101800-a.start==1800);
    tv_recent_add(42,1,1);
    assert(tv_pick("2:100000:101900:1:1:1",1,0)==0&&entries[0].episode==1); /* Rejoin, not new show. */
    assert(tv_pick("2:100000:103000:1:1:1",1,0)==0);
    assert(tv_airing_read(&a)&&a.episode==2&&a.start==102760); /* 44m + 2m ad. */
    assert(tv_pick("2:100000:106400:1:1:1",1,0)==0);
    assert(tv_airing_read(&a)&&a.season==2&&a.episode==1); /* Marathon crosses seasons. */
    assert(tv_pick("3:106300:106500:1:1:1",1,0)==0&&entries[0].season==2); /* Overtime is allowed. */
    tv_progress_save(42,1,1);
    assert(tv_pick("2:100000:101800:0:1:1",2,42)==0);
    assert(tv_airing_read(&a)&&a.episode==2&&a.start==101800); /* Continuous playback starts at zero. */
    assert(tv_pick("2:100000:99999:1:1:1",1,0)<0);
    FILE *f=fopen("tv-airing.tsv","w");assert(f);fputs("broken",f);fclose(f);assert(!tv_airing_read(&a));
    unlink("tv-airing.tsv");unlink("tv-progress.tsv");unlink("tv-recent.tsv");chdir("/tmp");rmdir(dir);
    puts("PASS: wall-clock join, stable rejoin, ad spacing, marathon season rollover, overtime, continuous playback, invalid guide");
}
