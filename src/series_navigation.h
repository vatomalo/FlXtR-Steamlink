/* Find the adjacent aired episode, including across season boundaries. */
static int series_adjacent(int id,int season,int episode,int direction){
    char path[128];snprintf(path,sizeof(path),"/tv/%d",id);
    json_object *show=get_json(path);if(!show)return -1;
    json_object *seasons=field(show,"seasons");
    if(!json_object_is_type(seasons,json_type_array)){json_object_put(show);return -1;}
    char today[11];time_t now=time(NULL);struct tm tm;gmtime_r(&now,&tm);strftime(today,sizeof(today),"%Y-%m-%d",&tm);
    int current=season;
    for(int pass=0;pass<100;pass++){
        snprintf(path,sizeof(path),"/tv/%d/season/%d",id,current);
        json_object *root=get_json(path);if(!root){json_object_put(show);return -1;}
        json_object *eps=field(root,"episodes"),*best=NULL;int best_number=direction>0?2147483647:-1;
        if(json_object_is_type(eps,json_type_array))for(size_t i=0;i<(size_t)json_object_array_length(eps);i++){
            json_object *ep=json_object_array_get_idx(eps,i);int num=number(ep,"episode_number");const char *date=string(ep,"air_date");
            if(num<1||strlen(date)!=10||strcmp(date,today)>0)continue;
            if(current==season&&(direction>0?num<=episode:num>=episode))continue;
            if(direction>0?num<best_number:num>best_number){best=ep;best_number=num;}
        }
        if(best){from_json(&entries[used++],best,"episode",id,current);total=1;json_object_put(root);json_object_put(show);return 0;}
        json_object_put(root);int next=direction>0?2147483647:-1;
        for(size_t i=0;i<(size_t)json_object_array_length(seasons);i++){
            int n=number(json_object_array_get_idx(seasons,i),"season_number");
            if(n<1)continue;
            if(direction>0?(n>current&&n<next):(n<current&&n>next))next=n;
        }
        if(next==-1||next==2147483647)break;
        current=next;
    }
    json_object_put(show);total=0;return 0;
}
