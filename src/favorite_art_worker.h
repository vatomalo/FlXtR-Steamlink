#include "favorite_art.h"
static void favorite_poster_fetch(Entry *entry){
    if((!strcmp(entry->kind,"movie")||!strcmp(entry->kind,"tv")||!strcmp(entry->kind,"episode")||!strcmp(entry->kind,"season"))&&entry->id>0){
        char query[96];snprintf(query,sizeof(query),"/%s/%d",!strcmp(entry->kind,"movie")?"movie":"tv",entry->id);
        json_object *data=get_json(query);
        if(data){snprintf(entry->poster,sizeof(entry->poster),"%s",string(data,"poster_path"));json_object_put(data);}
    }else if((!strcmp(entry->kind,"kiss")||!strcmp(entry->kind,"kiss-episode"))&&entry->id>0){
        char url[128];snprintf(url,sizeof(url),KISS"/?post_type=anime&p=%d",entry->id);
        char *html=kiss_fetch(url);
        if(html){
            for(char *p=html;(p=strstr(p,"<meta"));p++){
                char property[80],content[512];
                if(kiss_attr(p,"property",property,sizeof(property))&&!strcmp(property,"og:image")&&kiss_attr(p,"content",content,sizeof(content))){
                    snprintf(entry->poster,sizeof(entry->poster),"%s",content);break;
                }
            }free(html);
        }
    }else if(!strcmp(entry->kind,"archive")||!strcmp(entry->kind,"archive-file")){
        const char *prefix=ARCHIVE"/details/";
        if(!strncmp(entry->url,prefix,strlen(prefix))&&archive_identifier(entry->url+strlen(prefix)))
            snprintf(entry->poster,sizeof(entry->poster),ARCHIVE"/services/img/%.128s",entry->url+strlen(prefix));
    }
    if(entry->poster[0])poster(entry,0);
}
static int favorite_repair_entry(Entry *entry,void (*fetch_art)(Entry*)){
    char target[96],temp[112];
    favorite_art_path(target,sizeof(target),entry->kind,entry->title,entry->url,entry->id,entry->season,entry->episode);
    if(favorite_art_valid(target)){snprintf(entry->poster,sizeof(entry->poster),"%s",target);return 0;}
    entry->poster[0]=0;fetch_art(entry);
    if(!favorite_art_valid(entry->poster))return -1;
    mkdir("favorites-art",0700);snprintf(temp,sizeof(temp),"%s.next",target);
    FILE *src=fopen(entry->poster,"rb"),*dst=src?fopen(temp,"wb"):NULL;
    int good=src&&dst;unsigned char data[4096];size_t n;
    if(good){while((n=fread(data,1,sizeof(data),src)))if(fwrite(data,1,n,dst)!=n){good=0;break;}if(ferror(src))good=0;}
    if(src)fclose(src);
    if(dst&&fclose(dst))good=0;
    if(!good||rename(temp,target)){unlink(temp);return -1;}
    snprintf(entry->poster,sizeof(entry->poster),"%s",target);return 1;
}
static void favorite_repair_index(const Entry *entry){
    char row[768],line[768];
    snprintf(row,sizeof(row),"%s\t%s\t%d\t%d\t%d\t%s\n",entry->kind,entry->title,entry->id,entry->season,entry->episode,entry->poster);
    FILE *index=fopen("favorites-art-index.tsv","r");
    if(index){while(fgets(line,sizeof(line),index))if(!strcmp(line,row)){fclose(index);return;}fclose(index);}
    index=fopen("favorites-art-index.tsv","a");if(index){fputs(row,index);fclose(index);}
}
static int favorite_repair_all(void){
    if(getenv("FLXTR_NO_NETWORK"))return 1;
    FILE *favorites=fopen("favorites.tsv","r");if(!favorites)return 0;
    mkdir("catalog-cache",0700);mkdir("favorites-art",0700);
    char line[6000];int attempted=0,repaired=0;
    while(attempted<32&&fgets(line,sizeof(line),favorites)){
        if(!strchr(line,'\n')&&!feof(favorites)){int c;while((c=fgetc(favorites))!=EOF&&c!='\n'){}continue;}
        char *part[12];int fields=split(line,part,12);if(fields<8)continue;
        Entry entry={0};clean(entry.title,sizeof(entry.title),part[0]);clean(entry.kind,sizeof(entry.kind),part[4]);
        if(strlen(part[3])>=sizeof(entry.url))continue;
        strcpy(entry.url,part[3]);entry.id=positive(part[5],1);entry.season=positive(part[6],1);entry.episode=positive(part[7],1);
        if(strcmp(entry.kind,"movie")&&strcmp(entry.kind,"tv")&&strcmp(entry.kind,"episode")&&strcmp(entry.kind,"season")&&strcmp(entry.kind,"kiss")&&strcmp(entry.kind,"kiss-episode")&&strcmp(entry.kind,"archive")&&strcmp(entry.kind,"archive-file"))continue;
        favorite_art_path(entry.poster,sizeof(entry.poster),entry.kind,entry.title,entry.url,entry.id,entry.season,entry.episode);
        if(favorite_art_valid(entry.poster)){favorite_repair_index(&entry);continue;}
        attempted++;cache_poster_hash=0xface0000UL+(unsigned)attempted;
        int result=favorite_repair_entry(&entry,favorite_poster_fetch);
        if(result<0){fprintf(stderr,"Favorite cover unavailable: %s %d\n",entry.kind,entry.id);continue;}
        favorite_repair_index(&entry);
        repaired++;
    }
    fclose(favorites);fprintf(stderr,"Favorite covers repaired: %d of %d missing\n",repaired,attempted);
    return attempted&&!repaired?1:0;
}
