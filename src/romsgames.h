/* Public catalogue only. Downloads stay in the user's phone browser. */
#define ROMSGAMES "https://www.romsgames.net"
static const char *rg_names[]={"NES","SNES","GAME BOY","GAME BOY COLOR","GAME BOY ADVANCE","PLAYSTATION","PSP","SEGA GENESIS","NEO GEO","MAME"};
static const char *rg_slugs[]={"nintendo","super-nintendo","gameboy","gameboy-color","gameboy-advance","playstation","playstation-portable","sega-genesis","snk-neo-geo","mame-037b11"};
static int rg_platforms(int page){
    total=10;
    for(int i=(page-1)*PAGE_SIZE;i<total&&used<PAGE_SIZE;i++){
        Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"%s",rg_names[i]);
        strcpy(e->kind,"rg-platform");e->id=i+1;strcpy(e->meta,"ROMSGAMES / PHONE DOWNLOAD + UPLOAD");
    }return 0;
}
static int rg_path(const char *s){
    size_t n=strlen(s);if(n<8||n>240||s[0]!='/'||s[n-1]!='/'||!strstr(s,"-rom-"))return 0;
    for(size_t i=1;i<n-1;i++)if(!(s[i]>='a'&&s[i]<='z')&&!(s[i]>='0'&&s[i]<='9')&&s[i]!='-')return 0;
    return 1;
}
static int rg_cards(const char *html,int first){
    int count=0;const char *p=html;
    while((p=strstr(p,"<a "))){
        const char *end=strstr(p,"</a>");if(!end)break;
        char path[512],title[160],picture[512];const char *img=strstr(p,"<img ");
        if(kiss_attr(p,"href",path,sizeof(path))&&rg_path(path)&&img&&img<end&&kiss_attr(img,"alt",title,sizeof(title))){
            if(count++>=first&&used<PAGE_SIZE){
                Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"%.79s",title);
                snprintf(e->url,sizeof(e->url),ROMSGAMES "%s",path);strcpy(e->kind,"rg-game");
                strcpy(e->meta,"A PHONE QR / DOWNLOAD, THEN UPLOAD ROM");
                if(kiss_attr(img,"src",picture,sizeof(picture))){
                    if(!strncmp(picture,"//cache.downloadroms.io/",23))snprintf(e->poster,sizeof(e->poster),"https:%.505s",picture);
                    else if(!strncmp(picture,"https://cache.downloadroms.io/",29))snprintf(e->poster,sizeof(e->poster),"%s",picture);
                }
            }
        }p=end+4;
    }return count;
}
static int rg_list(int page,int id){
    if(id<1||id>10)return -1;
    int web_page=(page-1)/6+1;char url[256];
    snprintf(url,sizeof(url),ROMSGAMES "/roms/%s/?page=%d&sort=popularity",rg_slugs[id-1],web_page);
    char *html=response_cache_load(url);int hit=html!=NULL;
    if(!html)html=kiss_fetch(url);
    if(!html)return -1;
    int count=rg_cards(html,((page-1)%6)*PAGE_SIZE),last=web_page;
    if(!count&&hit){free(html);html=kiss_fetch(url);if(!html)return -1;hit=0;count=rg_cards(html,((page-1)%6)*PAGE_SIZE);}
    if(count&&!hit)response_cache_store(url,html);
    const char *p=html;while((p=strstr(p,"?page="))){int n=atoi(p+6);if(n>last&&n<=333)last=n;p+=6;}
    total=last>web_page?last*36:(web_page-1)*36+count;
    free(html);return count?0:-1;
}
