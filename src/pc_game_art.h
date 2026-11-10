/* Steam supplies public title search and portrait library artwork; no API key. */
static int pc_game_art_id(json_object *root,const char *title){
    json_object *items=NULL;
    if(!root||!json_object_object_get_ex(root,"items",&items)||!json_object_is_type(items,json_type_array))return 0;
    char wanted[256];size_t length=art_normalize(title,wanted,sizeof(wanted));
    int chosen=0,best=10000;
    size_t count=json_object_array_length(items);
    for(size_t i=0;i<count;i++){
        json_object *item=json_object_array_get_idx(items,i);
        const char *name=string(item,"name");
        if(strcmp(string(item,"type"),"app")||strcasestr(name,"soundtrack")||strcasestr(name,"arrangement")||strcasestr(name,"demo"))continue;
        char found[256];size_t count=art_normalize(name,found,sizeof(found));
        int score=!strcmp(wanted,found)?0:(length>=6&&count>length&&count-length<=24&&!strncmp(wanted,found,length)?(int)(count-length):10000);
        int id=number(item,"id");
        if(id>0&&score<best){chosen=id;best=score;}
    }
    return chosen;
}
static void pc_game_art(Entry *entry,int slot,const char *title){
    char *term=curl_easy_escape(NULL,title,0);if(!term)return;
    char url[1024];snprintf(url,sizeof(url),"https://store.steampowered.com/api/storesearch/?term=%s&l=english&cc=NO",term);curl_free(term);
    Buffer search={.limit=512*1024};int id=0;
    if(!fetch(url,&search)){
        json_object *root=json_tokener_parse(search.data);id=pc_game_art_id(root,title);
        if(root)json_object_put(root);
    }
    free(search.data);if(!id)return;
    snprintf(url,sizeof(url),"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/%d/library_600x900.jpg",id);
    Buffer cover={.limit=4*1024*1024};
    if(!fetch(url,&cover)){
        SDL_RWops *stream=SDL_RWFromConstMem(cover.data,(int)cover.length);
        SDL_Surface *art=stream?IMG_Load_RW(stream,1):NULL;
        if(art&&art->w>0&&art->h>0&&art->w<=2048&&art->h<=2048){
            float scale=154.0f/art->w;if(art->h*scale>231)scale=231.0f/art->h;
            int width=(int)(art->w*scale),height=(int)(art->h*scale);
            SDL_Surface *small=width>0&&height>0?SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888):NULL;
            if(small&&!SDL_BlitScaled(art,NULL,small,NULL)){
                char path[80];snprintf(path,sizeof(path),"catalog-cache/art-%08lx-%d.bmp",cache_poster_hash,slot);
                if(!SDL_SaveBMP(small,path))snprintf(entry->poster,sizeof(entry->poster),"%s",path);
            }
            SDL_FreeSurface(small);
        }
        SDL_FreeSurface(art);
    }
    free(cover.data);
}
