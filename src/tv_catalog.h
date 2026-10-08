#include "tv_progress.h"
static int tv_pick(int genre,int seed){
    if(genre<0||genre>7)return -1;
    /* Alternate sci-fi with animated comedy from Adult Swim's network. */
    const char *filters[]={"","&with_genres=35","&with_genres=80%7C9648%7C18","&with_genres=10765","&with_genres=16","&with_genres=10759","&with_genres=9648","&with_genres=16&with_networks=80"};
    int effective=genre==7&&seed%2==0?3:genre;
    char path[512];snprintf(path,sizeof(path),"/discover/tv?language=en-US&sort_by=popularity.desc&vote_count.gte=50&include_adult=false&page=%d%s",effective==7?1:seed/20%3+1,filters[effective]);
    json_object *root=get_json(path);if(!root)return -1;
    json_object *shows=field(root,"results");int n=shows&&json_object_is_type(shows,json_type_array)?(int)json_object_array_length(shows):0;
    if(!n){json_object_put(root);return -1;}
    json_object *show=json_object_array_get_idx(shows,seed%n);int id=number(show,"id");char title[80],art[512];
    clean(title,sizeof(title),string(show,"name"));clean(art,sizeof(art),string(show,"poster_path"));json_object_put(root);if(id<1)return -1;
    TvProgress progress[256];n=tv_progress_read(progress);int season=1,episode=1;
    for(int i=0;i<n;i++)if(progress[i].id==id){season=progress[i].season;episode=progress[i].episode+1;}
    for(int attempt=0;attempt<2;attempt++){
        snprintf(path,sizeof(path),"/tv/%d/season/%d",id,season);root=get_json(path);if(!root)return -1;
        json_object *episodes=field(root,"episodes");n=episodes&&json_object_is_type(episodes,json_type_array)?(int)json_object_array_length(episodes):0;
        for(int i=0;i<n;i++){
            json_object *ep=json_object_array_get_idx(episodes,i);if(number(ep,"episode_number")!=episode)continue;
            Entry *e=&entries[used++];clean(e->title,sizeof(e->title),title);strcpy(e->poster,art);strcpy(e->kind,"episode");
            e->id=id;e->season=season;e->episode=episode;snprintf(e->meta,sizeof(e->meta),"TV MODE / S%02d E%02d / %.50s",season,episode,string(ep,"name"));
            total=1;json_object_put(root);return 0;
        }json_object_put(root);season++;episode=1;
    }return -1;
}
