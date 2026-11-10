#include "gamez_id.h"

/* Read the private bridge settings as data, never as a shell command. */
static void gamez_setting(char *out,size_t capacity,const char *value){
    size_t n=strlen(value);
    while(n&&(value[n-1]=='\r'||value[n-1]=='\n'||value[n-1]==' '||value[n-1]=='\t'))n--;
    if(n>=2&&((value[0]=='\''&&value[n-1]=='\'')||(value[0]=='"'&&value[n-1]=='"'))){value++;n-=2;}
    if(n>=capacity)return;
    memcpy(out,value,n);out[n]=0;
}
static int gamez_list(int page){
    total=0;
    char host[256]="",token_path[4096]="",line[4608];
    FILE *file=fopen("gamez-bridge.conf","r");if(!file)return 0;
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
    snprintf(url,sizeof(url),"http://%s/v1/games",host);
    struct curl_slist *headers=curl_slist_append(NULL,authorization);
    CURL *client=curl_easy_init();if(!client||!headers){if(client)curl_easy_cleanup(client);curl_slist_free_all(headers);return -1;}
    Buffer response={.limit=512*1024};
    curl_easy_setopt(client,CURLOPT_URL,url);
    curl_easy_setopt(client,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(client,CURLOPT_CONNECTTIMEOUT,3L);
    curl_easy_setopt(client,CURLOPT_TIMEOUT,6L);
    curl_easy_setopt(client,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(client,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(client,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(client,CURLOPT_WRITEDATA,&response);
    CURLcode result=curl_easy_perform(client);long status=0;
    curl_easy_getinfo(client,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(client);curl_slist_free_all(headers);
    if(result!=CURLE_OK||status!=200){free(response.data);return -1;}
    json_object *root=response.data?json_tokener_parse(response.data):NULL,*games=NULL;
    free(response.data);
    if(!root||!json_object_object_get_ex(root,"games",&games)||!json_object_is_type(games,json_type_array)){
        if(root)json_object_put(root);
        return -1;
    }
    size_t count=json_object_array_length(games);
    if(count>5000){json_object_put(root);return -1;}
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
        int index=total++;
        if(index<(page-1)*PAGE_SIZE||used>=PAGE_SIZE)continue;
        Entry *entry=&entries[used++];
        snprintf(entry->title,sizeof(entry->title),"%s",title);
        for(size_t j=0;entry->title[j];j++)if((unsigned char)entry->title[j]<32)entry->title[j]=' ';
        strcpy(entry->meta,"APPROVED PC GAME / A STREAM");
        strcpy(entry->kind,"moonlight-game");
        snprintf(entry->url,sizeof(entry->url),"gamez:%s",id);
        snprintf(entry->poster,sizeof(entry->poster),"gameart:PC|%s",entry->title);
    }
    json_object_put(root);return 0;
}
