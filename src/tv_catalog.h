#include "tv_progress.h"
#include "tv_history.h"
static int tv_pick(int genre,int seed){
    if(genre<0||genre>=TV_GENRES)return -1;
    /* Alternate sci-fi with animated comedy from Adult Swim's network. */
    const char *filters[]={"","&with_genres=35","&with_genres=80%7C9648%7C18","&with_genres=10765","&with_genres=16","&with_genres=10759","&with_genres=9648","&with_genres=16&with_networks=80","&with_genres=99","&with_genres=10751","&with_genres=10764","&with_genres=10762","&with_genres=10766","&with_genres=10768","&with_genres=37","&with_genres=10767"};
    int effective=genre==7&&seed%2==0?3:genre;
    char path[512];snprintf(path,sizeof(path),"/discover/tv?language=en-US&sort_by=popularity.desc&vote_count.gte=50&include_adult=false&page=%d%s",effective==7?seed/2%2+1:seed%10+1,filters[effective]);
    json_object *root=get_json(path);if(!root)return -1;
    json_object *shows=field(root,"results");int n=shows&&json_object_is_type(shows,json_type_array)?(int)json_object_array_length(shows):0;
    if(!n){json_object_put(root);return -1;}
    /* User-adjustable TV gambits. Rating is always part of the score. */
    int wr=5,wp=2,wv=4;
    FILE *gambits=fopen("tv-gambits.cfg","r");
    if(gambits){
        char line[80],key[32];int value;
        while(fgets(line,sizeof(line),gambits))
            if(sscanf(line,"%31[^=]=%d",key,&value)==2&&value>=0&&value<=5){
                if(!strcmp(key,"rating"))wr=value;
                else if(!strcmp(key,"popularity"))wp=value;
                else if(!strcmp(key,"variety"))wv=value;
            }
        fclose(gambits);
    }
    if(wr<1)wr=1; /* Never ignore ratings entirely. */
    int pick=-1;double best=-1e30;
    for(int i=0;i<n;i++){
        json_object *candidate=json_object_array_get_idx(shows,i);
        int candidate_id=number(candidate,"id");
        if(candidate_id<1||tv_recent_has(candidate_id,0,0,1))continue;
        double rating=0,popularity=0;
        json_object *value=field(candidate,"vote_average");
        if(value)rating=json_object_get_double(value);
        value=field(candidate,"popularity");
        if(value)popularity=json_object_get_double(value);
        int votes=number(candidate,"vote_count");
        /* Bayesian rating avoids treating a handful of perfect votes as a hit. */
        double reliable=(rating*votes+6.5*100.0)/(votes+100.0);
        double pop=popularity/(popularity+50.0)*10.0;
        unsigned hash=(unsigned)candidate_id*2654435761u^(unsigned)seed*2246822519u;
        hash^=hash>>16;hash*=2246822519u;hash^=hash>>13;
        double novelty=(hash%10000)/1000.0;
        double score=wr*reliable+wp*pop+wv*novelty;
        if(score>best){best=score;pick=i;}
    }
    if(pick<0){json_object_put(root);return -1;}
    json_object *show=json_object_array_get_idx(shows,pick);int id=number(show,"id");char title[80],art[512];
    clean(title,sizeof(title),string(show,"name"));clean(art,sizeof(art),string(show,"poster_path"));json_object_put(root);if(id<1)return -1;
    TvProgress progress[256];n=tv_progress_read(progress);int season=1,episode=1;
    for(int i=0;i<n;i++)if(progress[i].id==id){season=progress[i].season;episode=progress[i].episode+1;}
    for(int attempt=0;attempt<2;attempt++){
        snprintf(path,sizeof(path),"/tv/%d/season/%d",id,season);root=get_json(path);if(!root)return -1;
        json_object *episodes=field(root,"episodes");n=episodes&&json_object_is_type(episodes,json_type_array)?(int)json_object_array_length(episodes):0;
        for(int i=0;i<n;i++){
            json_object *ep=json_object_array_get_idx(episodes,i);if(number(ep,"episode_number")!=episode)continue;
            if(tv_recent_has(id,season,episode,0)){json_object_put(root);return -1;}
            Entry *e=&entries[used++];clean(e->title,sizeof(e->title),title);strcpy(e->poster,art);strcpy(e->kind,"episode");
            e->id=id;e->season=season;e->episode=episode;
            int runtime=number(ep,"runtime");
            if(runtime<1||runtime>240)runtime=genre==11||genre==4?24:genre==8?50:44;
            snprintf(e->meta,sizeof(e->meta),"TV MODE / %d MIN / S%02d E%02d / %.35s",runtime,season,episode,string(ep,"name"));
            total=1;json_object_put(root);return 0;
        }json_object_put(root);season++;episode=1;
    }return -1;
}
