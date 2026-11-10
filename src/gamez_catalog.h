#include "gamez_id.h"
#include "gamez_filters.h"

static void gamez_info_path(const char *title,char *out,size_t size){
    unsigned long hash=2166136261UL;
    for(const unsigned char *p=(const unsigned char *)title;*p;p++)hash=((hash^(unsigned char)toupper(*p))*16777619UL)&0xffffffffUL;
    snprintf(out,size,"gamez-metadata/%08lx.json",hash);
}
static int gamez_info_stale(const char *title){
    char path[80];gamez_info_path(title,path,sizeof(path));struct stat st;
    if(stat(path,&st)||time(NULL)-st.st_mtime>86400)return 1;
    json_object *info=json_object_from_file(path);int stale=!info||number(info,"search_version")<2;
    if(info)json_object_put(info);
    return stale;
}
static void gamez_queue_info(const char *title){
    if(!gamez_info_stale(title))return;
    mkdir("game-art-cache",0700);
    int fd=open("game-art-cache/.requests",O_CREAT|O_WRONLY|O_APPEND,0600);
    if(fd<0)return;
    if(!flock(fd,LOCK_EX)){
        char line[160];int n=snprintf(line,sizeof(line),"gameart:PC|%.79s\n",title);
        if(n>0&&(size_t)n<sizeof(line)){ssize_t ignored=write(fd,line,(size_t)n);(void)ignored;}
        flock(fd,LOCK_UN);
    }
    close(fd);
}
static int gamez_record_play(const char *id){
    if(!gamez_valid_id(id))return 2;
    json_object *history=json_object_from_file("gamez-history.json");
    if(!history||!json_object_is_type(history,json_type_object)){if(history)json_object_put(history);history=json_object_new_object();}
    json_object_object_add(history,id,json_object_new_int64((int64_t)time(NULL)));
    int ok=!json_object_to_file("gamez-history.json.next",history)&&!rename("gamez-history.json.next","gamez-history.json");
    json_object_put(history);return ok?0:1;
}
typedef struct {const char *id,*title;long long played,popularity;int release;char genres[160];} GamezItem;
static int gamez_sort_order;
static int gamez_compare(const void *a,const void *b){
    const GamezItem *left=a,*right=b;
    long long x=gamez_sort_order==1?left->release:gamez_sort_order==2?left->popularity:left->played;
    long long y=gamez_sort_order==1?right->release:gamez_sort_order==2?right->popularity:right->played;
    if(gamez_sort_order&&x!=y)return x>y?-1:1;
    return strcasecmp(left->title,right->title);
}

/* Read the private bridge settings as data, never as a shell command. */
static void gamez_setting(char *out,size_t capacity,const char *value){
    size_t n=strlen(value);
    while(n&&(value[n-1]=='\r'||value[n-1]=='\n'||value[n-1]==' '||value[n-1]=='\t'))n--;
    if(n>=2&&((value[0]=='\''&&value[n-1]=='\'')||(value[0]=='"'&&value[n-1]=='"'))){value++;n-=2;}
    if(n>=capacity)return;
    memcpy(out,value,n);out[n]=0;
}
static int gamez_request(const char *path,const char *body,Buffer *response,long *status){
    char host[256]="",token_path[4096]="",line[4608];
    FILE *file=fopen("gamez-bridge.conf","r");if(!file)return -1;
    while(fgets(line,sizeof(line),file)){
        if(!strncmp(line,"GAMEZ_HOST=",11))gamez_setting(host,sizeof(host),line+11);
        if(!strncmp(line,"GAMEZ_TOKEN_FILE=",17))gamez_setting(token_path,sizeof(token_path),line+17);
    }
    fclose(file);
    if(!host[0]||!token_path[0])return -1;
    for(size_t i=0;host[i];i++)if(!((host[i]>='a'&&host[i]<='z')||(host[i]>='A'&&host[i]<='Z')||(host[i]>='0'&&host[i]<='9')||strchr(".:-",host[i])))return -1;
    char token[258]="";file=fopen(token_path,"r");if(!file)return -1;
    int read_ok=fgets(token,sizeof(token),file)!=NULL;fclose(file);if(!read_ok)return -1;
    token[strcspn(token,"\r\n")]=0;
    size_t length=strlen(token);if(length<32||length>256)return -1;
    for(size_t i=0;i<length;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='z')||(token[i]>='A'&&token[i]<='Z')))return -1;
    char authorization[288],url[320];
    snprintf(authorization,sizeof(authorization),"Authorization: Bearer %s",token);
    snprintf(url,sizeof(url),"http://%s%s",host,path);
    struct curl_slist *headers=curl_slist_append(NULL,authorization);
    CURL *client=curl_easy_init();if(!client||!headers){if(client)curl_easy_cleanup(client);curl_slist_free_all(headers);return -1;}
    if(body){headers=curl_slist_append(headers,"Content-Type: application/json");curl_easy_setopt(client,CURLOPT_POSTFIELDS,body);}
    curl_easy_setopt(client,CURLOPT_URL,url);
    curl_easy_setopt(client,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(client,CURLOPT_CONNECTTIMEOUT,3L);
    curl_easy_setopt(client,CURLOPT_TIMEOUT,6L);
    curl_easy_setopt(client,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(client,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(client,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(client,CURLOPT_WRITEDATA,response);
    CURLcode result=curl_easy_perform(client);*status=0;
    curl_easy_getinfo(client,CURLINFO_RESPONSE_CODE,status);
    curl_easy_cleanup(client);curl_slist_free_all(headers);
    return result==CURLE_OK?0:-1;
}
static int gamez_rename(const char *id,const char *title){
    if(!gamez_valid_id(id)||!title[0]||strlen(title)>79)return 2;
    for(const unsigned char *p=(const unsigned char *)title;*p;p++)if(*p<32||*p==127)return 2;
    char name[80];snprintf(name,sizeof(name),"%s",title);
    size_t length=strlen(name);while(length&&name[length-1]==' ')name[--length]=0;
    char *first=name;while(*first==' ')first++;
    if(!*first)return 2;
    json_object *names=json_object_from_file("gamez-names.json");
    if(!names||!json_object_is_type(names,json_type_object)){if(names)json_object_put(names);names=json_object_new_object();}
    json_object_object_add(names,id,json_object_new_string(first));
    int ok=!json_object_to_file("gamez-names.json.next",names)&&!rename("gamez-names.json.next","gamez-names.json");
    json_object_put(names);
    if(!ok)puts("COULD NOT SAVE GAME NAME");
    return ok?0:1;
}
static int gamez_unapprove(const char *id){
    if(!gamez_valid_id(id))return 2;
    json_object *request=json_object_new_object();json_object_object_add(request,"id",json_object_new_string(id));
    Buffer response={.limit=4096};long status=0;
    int result=gamez_request("/v1/unapprove",json_object_to_json_string(request),&response,&status);
    json_object_put(request);free(response.data);
    if(!result&&status==200)return 0;
    puts(status==404?"UPDATE PC GAMEZ TO UNAPPROVE":"COULD NOT UNAPPROVE / CHECK PC LAUNCHER");return 1;
}
static int gamez_list(int page,int genre,int order){
    if(genre<0||genre>=GAMEZ_GENRES||order<0||order>=GAMEZ_ORDERS)return -1;
    total=0;
    if(access("gamez-bridge.conf",R_OK))return 0;
    Buffer response={.limit=512*1024};long status=0;
    if(gamez_request("/v1/games",NULL,&response,&status)||status!=200){free(response.data);return -1;}
    json_object *root=response.data?json_tokener_parse(response.data):NULL,*games=NULL;
    free(response.data);
    if(!root||!json_object_object_get_ex(root,"games",&games)||!json_object_is_type(games,json_type_array)){
        if(root)json_object_put(root);
        return -1;
    }
    size_t count=json_object_array_length(games);
    if(count>5000){json_object_put(root);return -1;}
    GamezItem *items=calloc(count?count:1,sizeof(*items));
    if(!items){json_object_put(root);return -1;}
    json_object *history=json_object_from_file("gamez-history.json"),*names=json_object_from_file("gamez-names.json");size_t kept=0;
    for(size_t i=0;i<count;i++){
        json_object *game=json_object_array_get_idx(games,i),*id_value=NULL,*title_value=NULL;
        if(!json_object_object_get_ex(game,"id",&id_value)||!json_object_object_get_ex(game,"title",&title_value)||
           !json_object_is_type(id_value,json_type_string)||!json_object_is_type(title_value,json_type_string))continue;
        const char *id=json_object_get_string(id_value),*title=json_object_get_string(title_value);
        if(!gamez_valid_id(id)||!title[0]||json_object_get_string_len(id_value)!=(int)strlen(id))continue;
        int duplicate=0;
        for(size_t j=0;j<i;j++){
            json_object *other=json_object_array_get_idx(games,j),*other_id=NULL;
            if(json_object_object_get_ex(other,"id",&other_id)&&json_object_is_type(other_id,json_type_string)&&!strcmp(json_object_get_string(other_id),id)){duplicate=1;break;}
        }
        if(duplicate)continue;
        json_object *alias=NULL;
        if(names&&json_object_object_get_ex(names,id,&alias)&&json_object_is_type(alias,json_type_string)){
            const char *name=json_object_get_string(alias);
            if(name[0]&&strlen(name)<=79)title=name;
        }
        gamez_queue_info(title);
        char path[80];gamez_info_path(title,path,sizeof(path));
        json_object *info=json_object_from_file(path),*tags=NULL,*value=NULL;
        GamezItem item={.id=id,.title=title};
        if(info){item.release=number(info,"release");item.popularity=number(info,"popularity");json_object_object_get_ex(info,"genres",&tags);}
        int matches=genre==0,known=tags&&json_object_is_type(tags,json_type_array)&&json_object_array_length(tags)>0;
        if(genre==GAMEZ_GENRES-1)matches=!known;
        if(known){
            size_t tag_count=json_object_array_length(tags);
            for(size_t j=0;j<tag_count;j++){
                const char *tag=json_object_get_string(json_object_array_get_idx(tags,j));
                if(!tag)continue;
                if(genre>0&&genre<GAMEZ_GENRES-1&&!strcmp(tag,gamez_genre_ids[genre]))matches=1;
                for(int k=1;k<GAMEZ_GENRES-1;k++)if(!strcmp(tag,gamez_genre_ids[k])&&strlen(item.genres)+strlen(gamez_genres[k])+4<sizeof(item.genres)){
                    if(item.genres[0])strcat(item.genres," / ");
                    strcat(item.genres,gamez_genres[k]);
                }
            }
        }
        if(history&&json_object_object_get_ex(history,id,&value))item.played=json_object_get_int64(value);
        if(info)json_object_put(info);
        if(matches&&(order!=3||item.played>0))items[kept++]=item;
    }
    gamez_sort_order=order;qsort(items,kept,sizeof(*items),gamez_compare);total=(int)kept;
    for(int i=(page-1)*PAGE_SIZE;i<total&&used<PAGE_SIZE;i++){
        const GamezItem *item=&items[i];const char *id=item->id,*title=item->title;
        Entry *entry=&entries[used++];
        snprintf(entry->title,sizeof(entry->title),"%s",title);
        for(size_t j=0;entry->title[j];j++)if((unsigned char)entry->title[j]<32)entry->title[j]=' ';
        snprintf(entry->meta,sizeof(entry->meta),"%.60s / %s",item->genres[0]?item->genres:"APPROVED PC GAME",gamez_orders[order]);
        strcpy(entry->kind,"moonlight-game");
        snprintf(entry->url,sizeof(entry->url),"gamez:%s",id);
        char poster_title[sizeof(entry->title)];memcpy(poster_title,entry->title,sizeof(poster_title));
        snprintf(entry->poster,sizeof(entry->poster),"gameart:PC|%s",poster_title);
    }
    if(history)json_object_put(history);
    if(names)json_object_put(names);
    free(items);json_object_put(root);return 0;
}
