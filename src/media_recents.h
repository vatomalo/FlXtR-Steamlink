/* Independent, bounded on-device history for movies, series, anime and games.
 * Music has its own music-cache/recents.tsv and is intentionally not included. */
#ifndef FLXTR_MEDIA_RECENTS_H
#define FLXTR_MEDIA_RECENTS_H
#define MEDIA_RECENTS_MODE 38
#define MEDIA_RECENTS_LIMIT 24
static Title media_recent_entries[MEDIA_RECENTS_LIMIT];
static int media_recent_count;
static void media_recents_clean(char *value){
    for(char *p=value;*p;p++)if(*p=='\t'||*p=='\r'||*p=='\n')*p=' ';
}
static int media_recents_eligible(const Title *t){
    static const char *const kinds[]={
        "movie","tv","episode","kiss","kiss-episode",
        "game","moonlight-game","offline","deceptive-dice","scummvm-launch"
    };
    for(size_t i=0;i<sizeof(kinds)/sizeof(kinds[0]);i++)
        if(!strcmp(t->kind,kinds[i]))return 1;
    return 0;
}
static int media_recents_same(const Title *a,const Title *b){
    if(strcmp(a->kind,b->kind))return 0;
    if(!strcmp(a->kind,"moonlight-game")||!strcmp(a->kind,"game")||
       !strcmp(a->kind,"offline"))
        return !strcmp(a->url,b->url);
    return a->id==b->id&&a->season==b->season&&a->episode==b->episode;
}
static void media_recents_load(void){
    media_recent_count=0;
    FILE *file=fopen("media-recents.tsv","r");
    if(!file)return;
    char line[6000];
    while(media_recent_count<MEDIA_RECENTS_LIMIT&&fgets(line,sizeof(line),file)){
        if(!strchr(line,'\n')&&!feof(file)){
            int c;while((c=fgetc(file))!=EOF&&c!='\n'){}
            continue;
        }
        line[strcspn(line,"\r\n")]=0;
        char *fields[8],*cursor=line;
        int valid=1;
        for(int j=0;j<7;j++){
            fields[j]=cursor;
            char *tab=strchr(cursor,'\t');
            if(!tab){valid=0;break;}
            *tab=0;cursor=tab+1;
        }
        if(!valid||strchr(cursor,'\t'))continue;
        fields[7]=cursor;
        Title item={0};
        if(!fields[0][0]||strlen(fields[0])>=sizeof(item.title)||
           strlen(fields[1])>=sizeof(item.meta)||
           strlen(fields[2])>=sizeof(item.poster)||
           strlen(fields[3])>=sizeof(item.url)||
           strlen(fields[4])>=sizeof(item.kind))continue;
        strcpy(item.title,fields[0]);strcpy(item.meta,fields[1]);
        strcpy(item.poster,fields[2]);strcpy(item.url,fields[3]);
        strcpy(item.kind,fields[4]);
        char tail;
        if(sscanf(fields[5],"%d%c",&item.id,&tail)!=1||
           sscanf(fields[6],"%d%c",&item.season,&tail)!=1||
           sscanf(fields[7],"%d%c",&item.episode,&tail)!=1)continue;
        if(!media_recents_eligible(&item))continue;
        media_recent_entries[media_recent_count++]=item;
    }
    fclose(file);
}
static void media_recents_record(const Title *title){
    if(!title||!media_recents_eligible(title))return;
    Title copy=*title;
    media_recents_clean(copy.title);media_recents_clean(copy.meta);
    media_recents_clean(copy.poster);media_recents_clean(copy.url);
    if(!copy.title[0])return;
    media_recents_load();
    int found=-1;
    for(int i=0;i<media_recent_count;i++)
        if(media_recents_same(&copy,&media_recent_entries[i])){found=i;break;}
    if(found>=0){
        memmove(media_recent_entries+found,media_recent_entries+found+1,
                (size_t)(media_recent_count-found-1)*sizeof(Title));
        media_recent_count--;
    }
    if(media_recent_count==MEDIA_RECENTS_LIMIT)media_recent_count--;
    memmove(media_recent_entries+1,media_recent_entries,
            (size_t)media_recent_count*sizeof(Title));
    media_recent_entries[0]=copy;media_recent_count++;
    FILE *file=fopen("media-recents.tsv.next","w");
    if(!file)return;
    int failed=0;
    for(int i=0;i<media_recent_count;i++){
        const Title *t=&media_recent_entries[i];
        if(fprintf(file,"%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n",
            t->title,t->meta,t->poster,t->url,t->kind,t->id,t->season,t->episode)<0)
            failed=1;
    }
    if(fclose(file))failed=1;
    if(failed||rename("media-recents.tsv.next","media-recents.tsv"))
        unlink("media-recents.tsv.next");
}
static void media_recents_open(Browse next){
    browse=next;history_size=0;ready_only=0;about=0;
    count=total=selection=0;listing_pages=1;listing_total=0;
    memset(titles,0,sizeof(titles));
    if(!access("media-recents.tsv",R_OK))read_catalog("media-recents.tsv");
    listing_total=total;
    snprintf(status,sizeof(status),total?
        "RECENTLY OPENED MEDIA / A RESUME / X CHANGE LIBRARY":
        "NO RECENTS YET / VISIT MOVIES, SERIES, ANIME OR GAMES WITH X");
}
#endif
