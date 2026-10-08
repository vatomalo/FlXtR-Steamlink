#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "font.h"
#include "video_layout.h"

#ifndef FLXTR_VERSION
#define FLXTR_VERSION "development"
#endif
#define W 960
#define H 540
#define MAX_TITLES 128
#define VISIBLE 6
#define STARS 56
typedef struct { char title[80],meta[96],poster[192],url[2048],kind[16];int id,season,episode,height; } Title;
static Title titles[MAX_TITLES];
static int count, selection, ready_only, about, stars_on=1, running=1;
static int visible[MAX_TITLES], total, cached_page=-1;
static SDL_Renderer *renderer;
static SDL_Window *window;
static SDL_Texture *posters[VISIBLE];
static SDL_GameController *pads[4];
static pid_t player_pid, update_pid;
static int restart_shell, coverflow=1;
static int player_control=-1,play_after_load,player_menu;
static Title playing_title;
static double restart_position,auto_resume;
static int settings_on,settings_row,quality_setting=1,buffer_setting=1,disk_setting=1,subtitle_setting,subtitle_scale=2,subtitle_delay;
static const int qualities[]={480,720,1080},buffer_seconds[]={5,15,30},disk_megabytes[]={64,128,256};
static const char *const subtitle_languages[]={"off","auto","eng","nor"};
static void save_settings(void){
    FILE *f=fopen("settings.cfg.next","w");if(!f)return;
    fprintf(f,"quality=%d\nbuffer=%d\ndisk=%d\ncoverflow=%d\nsubtitles=%d\nscale=%d\ndelay=%d\n",quality_setting,buffer_setting,disk_setting,coverflow,subtitle_setting,subtitle_scale,subtitle_delay);
    if(fclose(f)==0)rename("settings.cfg.next","settings.cfg");
}
static void load_settings(void){
    FILE *f=fopen("settings.cfg","r");if(!f)return;char line[80],key[32];int value;
    while(fgets(line,sizeof(line),f))if(sscanf(line,"%31[^=]=%d",key,&value)==2){
        if(!strcmp(key,"quality")&&value>=0&&value<3)quality_setting=value;
        else if(!strcmp(key,"buffer")&&value>=0&&value<3)buffer_setting=value;
        else if(!strcmp(key,"disk")&&value>=0&&value<3)disk_setting=value;
        else if(!strcmp(key,"coverflow")&&(value==0||value==1))coverflow=value;
        else if(!strcmp(key,"subtitles")&&value>=0&&value<4)subtitle_setting=value;
        else if(!strcmp(key,"scale")&&value>=2&&value<=3)subtitle_scale=value;
        else if(!strcmp(key,"delay")&&value>=-5&&value<=5)subtitle_delay=value;
    }fclose(f);
}
static float flow_position;
static int stopping_player;
static int viewing=VIEW_FIT;
static Uint32 stop_time;
typedef struct {int mode,page,id,season,episode,selected;char query[65],name[80];} Browse;
static Browse browse={0,1,0,0,0,0,"",""},pending,history[4];
static int history_size,pending_push,pending_pop,listing_pages=1,listing_total;
static pid_t catalog_pid,prefetch_pid,idle_pid;
static Browse prefetch_browse;
static int prefetch_left;
static Uint32 controller_activity;
static int controller_slept;
static int auto_active,auto_count,auto_index;
static char auto_servers[40][32];
static Title auto_title;
static Uint32 catalog_started;
static void launch_player(const Title *t);
static void play(void);
static void auto_next(void);
static const char *local_catalog="catalog.tsv";
static const char *const browse_labels[]={"LOCAL","MOVIES","SERIES","SEASONS","EPISODES","SERVERS","QUALITY","KISSANIME","ANIME EPISODES"};
static const char *const browse_kinds[]={"","movie","tv","season","episode","source","quality","kiss","kiss-episodes"};
static int search_on,search_key;
static char search_text[65];
static const char search_keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -<>";
static char status[100]="SELECT A TITLE TO BEGIN";
static unsigned char star_x[STARS], star_y[STARS], star_speed[STARS];
static const SDL_Color green={116,255,132,255}, dim={66,126,77,255}, white={212,226,214,255};

static void color(SDL_Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
static void rect(int x,int y,int w,int h,SDL_Color c,int fill) {
    SDL_Rect r={x,y,w,h}; color(c);
    if(fill) SDL_RenderFillRect(renderer,&r); else SDL_RenderDrawRect(renderer,&r);
}
static void text(int x,int y,const char *str,int scale,SDL_Color c,int limit) {
    int n=0; color(c);
    for(;*str && n<limit; str++,n++,x+=6*scale) {
        unsigned char ch=(unsigned char)toupper((unsigned char)*str);
        size_t i;
        if(ch==' ') continue;
        for(i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==ch) break;
        if(i==sizeof(glyphs)/sizeof(glyphs[0])) i=sizeof(glyphs)/sizeof(glyphs[0])-5;
        for(int y1=0;y1<7;y1++) for(int x1=0;x1<5;x1++) if(glyphs[i].row[y1]&(16>>x1)) {
            SDL_Rect r={x+x1*scale,y+y1*scale,scale,scale}; SDL_RenderFillRect(renderer,&r);
        }
    }
}
static void wrap(int x,int y,const char *str,int cols,int lines,SDL_Color c) {
    for(int row=0;*str && row<lines;row++) {
        char line[81]; size_t len=strlen(str), n=len<(size_t)cols?len:(size_t)cols;
        if(len>n) { size_t k=n; while(k>0 && str[k]!=' ') k--; if(k>0)n=k; }
        memcpy(line,str,n); line[n]=0; text(x,y+row*20,line,2,c,cols);
        str+=n; while(*str==' ')str++;
    }
}
static void clear_posters(void) {
    for(int i=0;i<VISIBLE;i++) { if(posters[i])SDL_DestroyTexture(posters[i]); posters[i]=NULL; }
    cached_page=-1;
}
/* SDL_HideWindow leaves the last graphics frame over the hardware video plane
 * on Steam Link. Release the graphics backend before handing over to SLVideo. */
static void close_ui(void) {
    clear_posters();
    SDL_DestroyRenderer(renderer);renderer=NULL;
    SDL_DestroyWindow(window);window=NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
static int open_ui(void) {
    if(SDL_InitSubSystem(SDL_INIT_VIDEO))return -1;
    window=SDL_CreateWindow("FlXtR Steamlink",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,SDL_WINDOW_SHOWN);
    if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){close_ui();return -1;}
    SDL_RenderSetLogicalSize(renderer,W,H);SDL_ShowCursor(SDL_DISABLE);
    return 0;
}
static void filter(void) {
    total=0; for(int i=0;i<count;i++)if(!ready_only||titles[i].url[0])visible[total++]=i;
    selection=0; clear_posters();
}
static int read_catalog(const char *path) {
    FILE *f=fopen(path,"r"); char line[2600];
    if(!f) { snprintf(status,sizeof(status),"CATALOG NOT FOUND - ADD CATALOG.TSV");return 0; }
    while(fgets(line,sizeof(line),f) && count<MAX_TITLES) {
        char *fields[9],*p=line; int nf=0;
        if(line[0]=='#'&&!strchr(line,'\t')){int pages,items;if(sscanf(line,"# pages=%d total=%d",&pages,&items)==2&&pages>0&&pages<=2000&&items>=0){listing_pages=pages;listing_total=items;}continue;}
        if(line[0]=='\n')continue;
        if(!strchr(line,'\n')&&!feof(f)) { int ch; while((ch=fgetc(f))!=EOF&&ch!='\n'){} continue; }
        line[strcspn(line,"\r\n")]=0;
        while(nf<9){fields[nf++]=p;char *tab=strchr(p,'\t');if(!tab||nf==9)break;*tab=0;p=tab+1;}
        if((nf!=4&&nf!=8&&nf!=9)||!fields[0][0]||strchr(fields[nf-1],'\t'))continue;
        Title *t=&titles[count];
        if(strlen(fields[0])>=sizeof(t->title)||strlen(fields[1])>=sizeof(t->meta)||strlen(fields[2])>=sizeof(t->poster)||strlen(fields[3])>=sizeof(t->url))continue;
        if(fields[3][0] && strncmp(fields[3],"https://",8) && strncmp(fields[3],"http://",7))continue;
        strcpy(t->title,fields[0]);strcpy(t->meta,fields[1]);strcpy(t->poster,fields[2]);strcpy(t->url,fields[3]);count++;
        if(nf>=8){snprintf(t->kind,sizeof(t->kind),"%s",fields[4]);t->id=atoi(fields[5]);t->season=atoi(fields[6]);t->episode=atoi(fields[7]);}
        if(nf==9){t->height=atoi(fields[8]);if(t->height<0||t->height>1080)t->height=0;}
    }
    fclose(f); filter();return count;
}
static void request_catalog(Browse next,int push,int pop){
    if(catalog_pid)return;
    if(prefetch_pid){kill(prefetch_pid,SIGKILL);waitpid(prefetch_pid,NULL,0);prefetch_pid=0;}prefetch_left=0;
    if(!next.mode){
        browse=next;history_size=0;count=0;ready_only=0;listing_pages=1;memset(titles,0,sizeof(titles));read_catalog(local_catalog);return;
    }
    if(access("./greenlink-catalog",X_OK)){snprintf(status,sizeof(status),"CATALOG WORKER NOT INSTALLED");return;}
    mkdir("catalog-cache",0700);
    for(int i=0;i<VISIBLE;i++){char path[80];snprintf(path,sizeof(path),"catalog-cache/poster-%d.next.bmp",i);unlink(path);}
    browse.selected=selection;pending=next;pending_push=push;pending_pop=pop;
    catalog_started=SDL_GetTicks();
    catalog_pid=fork();
    if(!catalog_pid){
        int out=open("catalog-cache/result.tsv",O_WRONLY|O_CREAT|O_TRUNC,0600),log=open("catalog.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(out<0||log<0)_exit(126);
        dup2(out,STDOUT_FILENO);dup2(log,STDERR_FILENO);close(out);close(log);
        char pg[16],id[16],sn[16],ep[16];snprintf(pg,sizeof(pg),"%d",next.page);snprintf(id,sizeof(id),"%d",next.id);snprintf(sn,sizeof(sn),"%d",next.season);snprintf(ep,sizeof(ep),"%d",next.episode);
        execl("./greenlink-catalog","greenlink-catalog",auto_active&&!strcmp(auto_title.kind,"kiss-episode")?(next.mode==5?"kiss-source":"kiss-quality"):browse_kinds[next.mode],pg,next.query,id,sn,ep,next.mode==6?next.query:"",(char*)NULL);_exit(127);
    }
    if(catalog_pid<0){catalog_pid=0;snprintf(status,sizeof(status),"COULD NOT LOAD CATALOG");}
    else snprintf(status,sizeof(status),"LOADING %s - B TO CANCEL",browse_labels[next.mode]);
}
/* Automatic lookup uses its own small queue and leaves the visible library intact. */
static void auto_request(Browse next){
    request_catalog(next,0,0);
    if(!catalog_pid)auto_active=0;
}
static void auto_next(void){
    if(auto_index>=auto_count){auto_active=0;snprintf(status,sizeof(status),"NO WORKING SERVER FOUND - A TO RETRY / B BACK");return;}
    Browse next={6,1,auto_title.id,auto_title.season,auto_title.episode,0,"",""};
    strcpy(next.query,auto_servers[auto_index]);auto_request(next);
    if(auto_active)snprintf(status,sizeof(status),"TRYING SERVER %d OF %d - B TO CANCEL",auto_index+1,auto_count);
}
static void auto_result(int success){
    if(!success){
        if(pending.mode==6){auto_index++;auto_next();}
        else {auto_active=0;snprintf(status,sizeof(status),"SERVER LOOKUP FAILED - A TO RETRY");}
        return;
    }
    FILE *f=fopen("catalog-cache/result.tsv","r");char line[2600];int pages=1;
    Title source=auto_title;source.url[0]=0;source.height=0;
    if(f){while(fgets(line,sizeof(line),f)){
        if(line[0]=='#'&&!strchr(line,'\t')){int n;if(sscanf(line,"# pages=%d",&n)==1&&n>=1&&n<=7)pages=n;continue;}
        if(!strchr(line,'\n')&&!feof(f)){int ch;while((ch=fgetc(f))!=EOF&&ch!='\n'){}continue;}
        line[strcspn(line,"\r\n")]=0;char *field[9],*part=line;int nf=0;
        while(nf<9){field[nf++]=part;char *tab=strchr(part,'\t');if(!tab)break;*tab=0;part=tab+1;}
        if(nf<8||atoi(field[5])!=auto_title.id||atoi(field[6])!=auto_title.season||atoi(field[7])!=auto_title.episode)continue;
        if(pending.mode==5&&!strcmp(field[4],"server")){
            size_t len=strlen(field[0]);int valid=len>0&&len<32;
            for(size_t i=0;i<len;i++)if(!isalnum((unsigned char)field[0][i])&&field[0][i]!='_'&&field[0][i]!='-')valid=0;
            for(int i=0;i<auto_count;i++)if(!strcmp(auto_servers[i],field[0]))valid=0;
            if(valid&&auto_count<40)strcpy(auto_servers[auto_count++],field[0]);
        }else if(!strcmp(field[4],"source")&&!source.url[0]&&strlen(field[3])<sizeof(source.url)&&
                 (!strncmp(field[3],"https://",8)||!strncmp(field[3],"http://",7))){
            /* First row is AUTO: player chooses the best compatible H.264 stream. */
            strcpy(source.url,field[3]);
        }
    }fclose(f);}
    if(source.url[0]){launch_player(&source);if(!player_pid)auto_active=0;return;}
    if(pending.mode==5){
        if(pending.page<pages&&auto_count<40){Browse next=pending;next.page++;auto_request(next);}
        else {auto_index=0;auto_next();}
    }else {auto_index++;auto_next();}
}
static void finish_catalog(void){
    if(!catalog_pid)return;
    if(auto_active&&SDL_GetTicks()-catalog_started>70000)kill(catalog_pid,SIGKILL);
    int code;pid_t p=waitpid(catalog_pid,&code,WNOHANG);if(p!=catalog_pid)return;
    catalog_pid=0;
    if(auto_active){auto_result(WIFEXITED(code)&&!WEXITSTATUS(code));return;}
    if(!WIFEXITED(code)||WEXITSTATUS(code)){play_after_load=0;snprintf(status,sizeof(status),pending.mode==6?"SERVER UNAVAILABLE - SELECT ANOTHER SERVER":"COULD NOT LOAD - CHECK CONNECTION / CATALOG.LOG");return;}
    for(int i=0;i<VISIBLE;i++){char src[80],dst[80];snprintf(src,sizeof(src),"catalog-cache/poster-%d.next.bmp",i);snprintf(dst,sizeof(dst),"catalog-cache/poster-%d.bmp",i);rename(src,dst);}
    if(pending_push&&history_size<4)history[history_size++]=browse;
    if(pending_pop&&history_size)history_size--;
    browse=pending;count=0;ready_only=0;about=0;listing_pages=1;listing_total=0;memset(titles,0,sizeof(titles));read_catalog("catalog-cache/result.tsv");
    if(browse.selected>=0&&browse.selected<total)selection=browse.selected;
    if((browse.mode<5||browse.mode>=7)){prefetch_browse=browse;prefetch_left=browse.page==1?2:1;}
    if((browse.mode==5||browse.mode==6)&&!total)snprintf(status,sizeof(status),"NO SOURCES RETURNED FOR THIS TITLE - B TO RETURN");
    else snprintf(status,sizeof(status),"%s / PAGE %d OF %d / X CHANGE LIBRARY",browse_labels[browse.mode],browse.page,listing_pages);
    if(play_after_load){play_after_load=0;if(total)play();}
}
static void prefetch_pages(void){
    if(prefetch_pid){if(waitpid(prefetch_pid,NULL,WNOHANG)==prefetch_pid)prefetch_pid=0;else return;}
    if(!prefetch_left||catalog_pid||player_pid||auto_active)return;
    prefetch_browse.page++;prefetch_left--;
    if(prefetch_browse.page>listing_pages){prefetch_left=0;return;}
    prefetch_pid=fork();
    if(!prefetch_pid){
        int out=open("/dev/null",O_WRONLY);if(out>=0){dup2(out,1);dup2(out,2);close(out);}
        char pg[16],id[16],sn[16],ep[16];snprintf(pg,sizeof(pg),"%d",prefetch_browse.page);snprintf(id,sizeof(id),"%d",browse.id);
        snprintf(sn,sizeof(sn),"%d",browse.season);snprintf(ep,sizeof(ep),"%d",browse.episode);
        execl("./greenlink-catalog","greenlink-catalog",browse_kinds[browse.mode],pg,browse.query,id,sn,ep,"",(char*)NULL);_exit(127);
    }
    if(prefetch_pid<0){prefetch_pid=0;prefetch_left=0;}
}
static void controller_used(void){controller_activity=SDL_GetTicks();controller_slept=0;}
static void controller_idle(void){
    if(idle_pid){if(waitpid(idle_pid,NULL,WNOHANG)==idle_pid)idle_pid=0;else return;}
    if(controller_slept||SDL_GetTicks()-controller_activity<300000)return;
    controller_slept=1;
    if(access("./controller-idle.sh",R_OK))return;
    idle_pid=fork();
    if(!idle_pid){int log=open("controller.log",O_WRONLY|O_CREAT|O_TRUNC,0600);if(log>=0){dup2(log,1);dup2(log,2);close(log);}execl("/bin/sh","sh","./controller-idle.sh",(char*)NULL);_exit(127);}
    if(idle_pid<0)idle_pid=0;
}
static void next_catalog_page(int direction){
    if(!browse.mode){int page=selection/VISIBLE+direction;if(page>=0&&page*VISIBLE<total)selection=page*VISIBLE;return;}
    Browse next=browse;next.page+=direction;next.selected=direction<0?5:0;
    if(next.mode&&next.page>=1&&next.page<=listing_pages)request_catalog(next,0,0);
}
/* Inspect BMP header before decoding: disk and decoded sizes both stay bounded. */
static SDL_Texture *load_poster(const char *path) {
    struct stat s; unsigned char header[26]; FILE *f; SDL_Surface *image; SDL_Texture *texture;
    if(!path[0]||stat(path,&s)||s.st_size<54||s.st_size>192*288*4+4096)return NULL;
    f=fopen(path,"rb");if(!f)return NULL;
    size_t n=fread(header,1,sizeof(header),f);fclose(f);
    if(n!=sizeof(header)||header[0]!='B'||header[1]!='M'||header[14]<40)return NULL;
    uint32_t width=(uint32_t)header[18]|(uint32_t)header[19]<<8|(uint32_t)header[20]<<16|(uint32_t)header[21]<<24;
    int32_t height=(int32_t)((uint32_t)header[22]|(uint32_t)header[23]<<8|(uint32_t)header[24]<<16|(uint32_t)header[25]<<24);
    if(width<1||width>192||height==0||height>288||height< -288)return NULL;
    image=SDL_LoadBMP(path);if(!image)return NULL;
    texture=SDL_CreateTextureFromSurface(renderer,image);SDL_FreeSurface(image);return texture;
}
static void cache_page(int page) {
    if(cached_page==page)return;
    clear_posters();cached_page=page;flow_position=(float)selection;
    for(int i=0;i<VISIBLE && page*VISIBLE+i<total;i++)posters[i]=load_poster(titles[visible[page*VISIBLE+i]].poster);
}
static void placeholder(int x,int y,int w,int h,int seed) {
    SDL_Color a={10,23,16,255}, b={23,57,35,255};rect(x,y,w,h,a,1);
    for(int i=0;i<6;i++)rect(x+8+i*9,y+h/2-i*7,w-16-i*18,3,b,1);
    text(x+10,y+12,"NO ART",1,dim,12);
    char code[16];snprintf(code,sizeof(code),"%03d",seed+1);text(x+10,y+h-28,code,2,green,5);
}
/* Six textures, depth ordering and mirrored reflections; no 3D context. */
static void draw_coverflow(void) {
    int page=selection/VISIBLE;cache_page(page);
    float delta=(float)selection-flow_position;
    flow_position+=delta*0.24f;
    if(delta<0.02f&&delta> -0.02f)flow_position=(float)selection;
    SDL_Rect clip={184,118,746,296};SDL_RenderSetClipRect(renderer,&clip);
    /* Farthest cards first so the centered cover stays in front. */
    for(int depth=6;depth>=0;depth--)for(int slot=0;slot<VISIBLE&&page*VISIBLE+slot<total;slot++) {
        int index=page*VISIBLE+slot;float d=(float)index-flow_position,a=d<0?-d:d;
        if((int)(a+0.5f)!=depth)continue;
        float scale=1.0f/(1.0f+a*0.27f);
        int h=(int)(236*scale),w=(int)(158*scale/(1+a*0.28f));
        int center=554+(int)(d*132);
        SDL_Rect dst={center-w/2,151+(236-h)/2,w,h};
        Uint8 light=(Uint8)(255/(1+a*0.45f));
        rect(dst.x-3,dst.y-3,w+6,h+6,index==selection?green:dim,0);
        if(posters[slot]) {
            SDL_SetTextureColorMod(posters[slot],light,light,light);
            SDL_SetTextureBlendMode(posters[slot],SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(posters[slot],255);
            SDL_RenderCopy(renderer,posters[slot],NULL,&dst);
            SDL_Rect reflection={dst.x,dst.y+h+8,w,h};
            SDL_SetTextureAlphaMod(posters[slot],40);
            SDL_RenderCopyEx(renderer,posters[slot],NULL,&reflection,0,NULL,SDL_FLIP_VERTICAL);
            SDL_SetTextureAlphaMod(posters[slot],255);SDL_SetTextureColorMod(posters[slot],255,255,255);
        }else {rect(dst.x,dst.y,w,h,(SDL_Color){8,23,14,255},1);text(dst.x+6,dst.y+h/2,"FLXTR",1,dim,w/6-1);}
    }
    SDL_RenderSetClipRect(renderer,NULL);
    Title *t=&titles[visible[selection]];
    text(194,95,browse.name[0]?browse.name:browse_labels[browse.mode],2,dim,60);
    text(194,427,t->title,2,green,60);text(194,454,t->meta,1,white,78);
    char line[90];snprintf(line,sizeof(line),"PAGE %d / %d   A OPEN   UP/DOWN PAGE   VIEW: %s",browse.mode?browse.page:page+1,browse.mode?listing_pages:(total+5)/6,view_names[viewing]);
    text(194,475,line,1,dim,120);
}
static int flow_animating(void){return coverflow&&!settings_on&&!about&&!search_on&&(browse.mode<5||browse.mode>=7)&&total&&flow_position!=(float)selection;}
static void start_update(void) {
    if(access("./update.sh",R_OK)){snprintf(status,sizeof(status),"UPDATER NOT INSTALLED");return;}
    update_pid=fork();
    if(!update_pid){int log=open("update.log",O_WRONLY|O_CREAT|O_TRUNC,0600);if(log>=0){dup2(log,1);dup2(log,2);close(log);}execl("/bin/sh","sh","./update.sh",(char*)NULL);_exit(127);}
    if(update_pid<0){update_pid=0;snprintf(status,sizeof(status),"COULD NOT START UPDATER");return;}
    snprintf(status,sizeof(status),"CHECKING GITHUB FOR A SHELL UPDATE...");
}
static void draw(Uint32 tick) {
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    if(stars_on)for(int i=0;i<STARS;i++) {
        int x=star_x[i]*W/256, y=(star_y[i]*H/256+(tick/100)*star_speed[i]/8)%H;
        SDL_Color c={145+(i%3)*40,145+(i%3)*40,145+(i%3)*40,255};rect(x,y,1+(i%9==0),1+(i%9==0),c,1);
    }
    text(30,24,"FLXTR",4,green,12);text(180,38,"CINEMA / STEAM LINK",1,dim,24);
    text(737,32,"NATIVE / MINIMAL",2,dim,17);
    rect(30,65,900,1,dim,1);
    text(30,92,"LIBRARY",2,green,12);
    text(30,124,browse.mode==0?"> LOCAL":"  LOCAL",1,browse.mode==0?green:dim,20);
    int root_mode=history_size?history[0].mode:browse.mode;
    text(30,145,root_mode==1?"> MOVIES":"  MOVIES",1,root_mode==1?green:dim,20);
    text(30,166,root_mode==2?"> SERIES":"  SERIES",1,root_mode==2?green:dim,20);
    text(30,187,root_mode==7?"> KISSANIME":"  KISSANIME",1,root_mode==7?green:dim,20);
    char num[64];snprintf(num,sizeof(num),browse.mode==7?"%d+ TITLES":"%d TITLES",browse.mode?listing_total:total);text(30,222,num,1,white,20);
    text(30,379,"[SELECT] SETTINGS",1,dim,22);text(30,402,"[X] LIBRARY",1,dim,22);text(30,421,"[Y] STARS",1,dim,22);text(30,440,browse.mode==1||browse.mode==2||browse.mode==7?"[START] SEARCH":"[START] ABOUT",1,dim,22);text(30,459,"B BACK",1,dim,22);
    if(settings_on){
        const char *labels[]={"QUALITY","PREBUFFER","DISK LIMIT","LIBRARY VIEW","SUBTITLES","SUBTITLE SIZE","SUBTITLE DELAY"};
        char values[7][40];snprintf(values[0],40,"%dP",qualities[quality_setting]);snprintf(values[1],40,"%d SECONDS",buffer_seconds[buffer_setting]);
        snprintf(values[2],40,"%d MB",disk_megabytes[disk_setting]);snprintf(values[3],40,"%s",coverflow?"COVERFLOW":"SIX-COVER WALL");
        const char *sub_names[]={"OFF","AUTOMATIC","ENGLISH","NORWEGIAN"};snprintf(values[4],40,"%s",sub_names[subtitle_setting]);snprintf(values[5],40,"%s",subtitle_scale==2?"NORMAL":"LARGE");snprintf(values[6],40,"%+d SECONDS",subtitle_delay);
        text(194,94,"SETTINGS",3,green,40);
        for(int i=0;i<7;i++){int y=151+i*41;rect(190,y-9,724,34,i==settings_row?green:dim,0);text(204,y,labels[i],2,white,24);text(566,y,values[i],2,i==settings_row?green:dim,28);}
        text(194,452,"LEFT/RIGHT CHANGE / B SAVE AND RETURN",1,green,90);
        text(194,474,"TEXT SUBTITLES WHEN INCLUDED IN THE STREAM",1,dim,90);
    }else if(search_on){
        text(194,100,"SEARCH",3,green,30);text(194,150,search_text,2,white,60);
        for(int i=0;i<40;i++){
            int x=194+(i%10)*64,y=202+(i/10)*48;char ch[2]={search_keys[i],0};
            rect(x,y,48,38,i==search_key?green:dim,0);text(x+15,y+10,ch,2,i==search_key?green:white,1);
        }
        text(194,424,"A TYPE / B DELETE / START SEARCH",2,green,60);
        text(194,460,"< DELETE    > SEARCH    ESC CANCEL",1,dim,60);
    }else if(about) {
        text(194,100,"SMALL BY DESIGN",3,green,35);
        wrap(194,155,"A QUIET LIBRARY FOR YOUR STEAM LINK. BLACK SPACE, GREEN PIXELS, AND A FEW DISTANT STARS.",53,4,white);
        wrap(194,260,"C + SDL2. SIX POSTERS IN MEMORY. NO BROWSER. NO WEB UI. DIRECT VIDEO PLAYBACK USES A SEPARATE PLAYER.",53,5,dim);
        text(194,390,"A CHECK FOR UPDATE / B RETURN",2,green,50);
        text(194,429,"BUILD " FLXTR_VERSION,1,dim,70);
    } else if(!total) {
        text(194,143,"NOTHING HERE YET",3,green,40);
        wrap(194,195,(browse.mode==5||browse.mode==6)?"THE PROVIDER RETURNED NO SOURCES. B RETURNS TO THE PREVIOUS SCREEN.":"NO MATCHING TITLES. X CHANGES LIBRARY. START OPENS SEARCH.",48,5,white);
    } else if((browse.mode==5||browse.mode==6)){
        text(194,96,browse_labels[browse.mode],3,green,35);text(194,136,browse.name,1,white,78);
        for(int i=0;i<total;i++){
            int y=180+i*43;Title *t=&titles[visible[i]];
            rect(190,y-8,726,37,i==selection?green:dim,0);
            text(204,y,t->title,2,i==selection?green:white,35);text(652,y,t->meta,1,dim,39);
        }
        text(194,460,browse.mode==6?"A PLAY / B CHANGE SERVER":"A SELECT SERVER / B RETURN",1,green,70);
    } else if(coverflow) {
        draw_coverflow();
    } else {
        int page=selection/VISIBLE;cache_page(page);
        for(int slot=0;slot<VISIBLE && page*VISIBLE+slot<total;slot++) {
            int index=page*VISIBLE+slot,x=194+(slot%3)*150,y=94+(slot/3)*194;
            Title *t=&titles[visible[index]]; SDL_Rect dst={x+5,y+5,124,142};
            rect(x,y,134,152,index==selection?green:dim,0);
            if(posters[slot]) { int iw,ih;SDL_QueryTexture(posters[slot],NULL,NULL,&iw,&ih);
                double scale=124.0/iw;if(ih*scale>142)scale=142.0/ih;
                dst.w=(int)(iw*scale);dst.h=(int)(ih*scale);dst.x=x+5+(124-dst.w)/2;dst.y=y+5+(142-dst.h)/2;
                SDL_RenderCopy(renderer,posters[slot],NULL,&dst);
            } else placeholder(dst.x,dst.y,dst.w,dst.h,visible[index]);
            text(x,y+162,t->title,1,index==selection?green:white,22);
            const char *label=!strcmp(t->kind,"kiss")?"BROWSE EPISODES":!strcmp(t->kind,"tv")?"BROWSE SEASONS":!strcmp(t->kind,"season")?"BROWSE EPISODES":t->url[0]?"PLAYABLE SOURCE":"A PLAY / AUTO SERVER";
            text(x,y+177,label,1,dim,22);
        }
        Title *t=&titles[visible[selection]];
        rect(665,94,1,378,dim,1);text(690,97,"NOW SELECTED",1,dim,30);
        wrap(690,126,t->title,19,4,green);wrap(690,228,t->meta,19,5,white);
        text(690,354,t->url[0]?"[A] PLAY":t->kind[0]?"[A] OPEN":"NO STREAM YET",2,t->kind[0]||t->url[0]?green:dim,20);
        text(690,395,"[LB/RB] VIEW",1,dim,25);
        text(690,416,view_names[viewing],1,green,25);
        snprintf(num,sizeof(num),"PAGE %d / %d",browse.mode?browse.page:page+1,browse.mode?listing_pages:(total+5)/6);text(690,455,num,1,dim,25);
    }
    if(catalog_pid){rect(190,227,680,70,(SDL_Color){0,0,0,255},1);rect(190,227,680,70,green,0);text(212,250,auto_active?"FINDING A WORKING SERVER... B CANCEL":"LOADING... B TO CANCEL",2,green,50);}
    rect(30,489,900,1,dim,1);text(30,509,status,1,white,92);text(713,509,"DPAD MOVE / B BACK",1,green,30);
    SDL_RenderPresent(renderer);
}
static void play(void) {
    if(settings_on||about||!total||player_pid)return;
    restart_position=auto_resume=0;
    Title *t=&titles[visible[selection]];
    if(!t->url[0]&&t->kind[0]){
        Browse next={0,1,t->id,t->season,t->episode,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);
        next.mode=!strcmp(t->kind,"kiss")?8:!strcmp(t->kind,"tv")?3:!strcmp(t->kind,"season")?4:!strcmp(t->kind,"server")?6:5;
        if(next.mode==6){snprintf(next.query,sizeof(next.query),"%.31s",t->title);snprintf(next.name,sizeof(next.name),"%s",browse.name);}
        if(next.mode==5){
            if(access("./greenlink-player",X_OK)){snprintf(status,sizeof(status),"PLAYER NOT INSTALLED");return;}
            auto_title=*t;auto_active=1;auto_count=auto_index=0;auto_request(next);
        }else request_catalog(next,1,0);
        return;
    }
    if(!t->url[0]) { snprintf(status,sizeof(status),"NO DIRECT STREAM - FLIXER RESOLUTION IS NOT CONNECTED YET");return; }
    launch_player(t);
}
static void launch_player(const Title *t) {
    if(prefetch_pid){kill(prefetch_pid,SIGKILL);waitpid(prefetch_pid,NULL,0);prefetch_pid=0;}prefetch_left=0;
    if(access("./greenlink-player",X_OK)) { snprintf(status,sizeof(status),"PLAYER NOT BUILT - RUN SCRIPTS/BUILD-PLAYER.SH");return; }
    int controls[2];if(pipe(controls)){snprintf(status,sizeof(status),"PLAYER CONTROL PIPE FAILED");return;}
    fcntl(controls[1],F_SETFL,O_NONBLOCK);fcntl(controls[1],F_SETFD,FD_CLOEXEC);
    signal(SIGPIPE,SIG_IGN);unlink("playback-request");
    if(auto_active)restart_position=auto_resume;
    playing_title=*t;player_menu=access("player-menu-v1",F_OK)==0;
    close_ui();
    player_pid=fork();
    if(player_pid==0) {
        close(controls[1]);
        signal(SIGUSR1,SIG_IGN); /* Ignore a controller press before exec is ready. */
        int log=open("player.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log>=0){dup2(log,STDERR_FILENO);dup2(log,STDOUT_FILENO);close(log);}
        const char *modes[]={"fit","stretch","pixel"};
        char height[16],buffer[16],disk[16],scale[16],delay[16],control[16],start[32];
        snprintf(control,sizeof(control),"%d",controls[0]);snprintf(start,sizeof(start),"%.3f",restart_position);snprintf(height,sizeof(height),"%d",t->height?t->height:qualities[quality_setting]);
        snprintf(buffer,sizeof(buffer),"%d",buffer_seconds[buffer_setting]);snprintf(disk,sizeof(disk),"%d",disk_megabytes[disk_setting]);
        snprintf(scale,sizeof(scale),"%d",subtitle_scale);snprintf(delay,sizeof(delay),"%d",subtitle_delay);
        if(!player_menu)execl("./greenlink-player","greenlink-player",t->url,"--view",modes[viewing],"--height",height,"--buffer-seconds",buffer,"--buffer-mb",disk,"--subtitles",subtitle_languages[subtitle_setting],"--subtitle-size",scale,"--subtitle-delay",delay,(char*)NULL);
        char *args[]={"greenlink-player",(char*)t->url,"--view",(char*)modes[viewing],"--height",height,"--buffer-seconds",buffer,"--buffer-mb",disk,"--subtitles",(char*)subtitle_languages[subtitle_setting],"--subtitle-size",scale,"--subtitle-delay",delay,"--control-fd",control,"--start",start,NULL,NULL,NULL};
        int n=20;if(browse.mode==4||browse.mode==8)args[n++]="--episodes";if(auto_active)args[n++]="--servers";args[n]=NULL;
        execv("./greenlink-player",args);_exit(127);
    }
    close(controls[0]);player_control=controls[1];restart_position=0;
    if(player_pid<0){close(player_control);player_control=-1;player_pid=0;if(open_ui())running=0;snprintf(status,sizeof(status),"COULD NOT START PLAYER");}
    else snprintf(status,sizeof(status),"PLAYING - B TO STOP");
}
static void finish_player(int code){
    int cancelled=stopping_player;
    player_pid=0;stopping_player=0;if(player_control>=0)close(player_control);player_control=-1;
    if(open_ui()){fprintf(stderr,"Restore UI: %s\n",SDL_GetError());running=0;auto_active=0;return;}
    if(!cancelled&&WIFEXITED(code)&&WEXITSTATUS(code)==40){
        int command,view,sub,scale,delay;double position;FILE *f=fopen("playback-request","r");
        int valid=f&&fscanf(f,"%d %lf %d %d %d %d",&command,&position,&view,&sub,&scale,&delay)==6;
        if(f)fclose(f);
        unlink("playback-request");
        if(!valid||command<1||command>7||!(position>=0&&position<=86400)||view<0||view>=VIEW_COUNT||sub<0||sub>3||scale<2||scale>3||delay< -5||delay>5){auto_active=0;snprintf(status,sizeof(status),"INVALID PLAYBACK REQUEST");return;}
        viewing=view;subtitle_setting=sub;subtitle_scale=scale;subtitle_delay=delay;save_settings();
        if(command==7){auto_active=0;snprintf(status,sizeof(status),"PLAYBACK STOPPED");return;}
        if(command<=3){
            if(command>1){quality_setting=(quality_setting+(command==2?1:2))%3;playing_title.height=0;save_settings();}
            restart_position=auto_resume=position;launch_player(&playing_title);return;
        }
        if(command==4){if(auto_active){restart_position=auto_resume=position;auto_index++;auto_next();}else snprintf(status,sizeof(status),"NO ALTERNATE SERVER FOR LOCAL VIDEO");return;}
        auto_active=0;
        if(browse.mode!=4&&browse.mode!=8){snprintf(status,sizeof(status),"EPISODE NAVIGATION ONLY AVAILABLE IN SERIES");return;}
        int direction=command==5?-1:1,next=selection+direction;
        if(next>=0&&next<total){selection=next;play();return;}
        int page=browse.page+direction;
        if(page>=1&&page<=listing_pages){play_after_load=1;next_catalog_page(direction);return;}
        snprintf(status,sizeof(status),"NO MORE EPISODES IN THIS SEASON");return;
    }
    if(WIFEXITED(code)&&WEXITSTATUS(code)==44){auto_active=0;snprintf(status,sizeof(status),"SERVER CANNOT SEEK - SELECT TITLE TO RESTART");return;}
    if(auto_active&&!cancelled&&(!WIFEXITED(code)||WEXITSTATUS(code))){auto_index++;auto_next();return;}
    auto_active=0;
    snprintf(status,sizeof(status),cancelled?"PLAYBACK STOPPED":WIFEXITED(code)&&!WEXITSTATUS(code)?"PLAYBACK FINISHED":"PLAYER STOPPED - SEE PLAYER.LOG");
}
static void action(SDL_Keycode key) {
    if(update_pid)return;
    if(player_pid) {
        if(!player_menu){
            if(key==SDLK_i||key==SDLK_y||key==SDLK_v)kill(player_pid,SIGUSR1);
            if((key==SDLK_ESCAPE||key==SDLK_BACKSPACE)&&!stopping_player){auto_active=0;kill(player_pid,SIGTERM);stopping_player=1;stop_time=SDL_GetTicks();}
            return;
        }
        char command=key==SDLK_i?'m':key==SDLK_ESCAPE||key==SDLK_BACKSPACE?'b':key==SDLK_UP?'u':key==SDLK_DOWN?'d':key==SDLK_LEFT?'l':key==SDLK_RIGHT?'r':key==SDLK_RETURN?'a':key==SDLK_y||key==SDLK_v?'v':0;
        if(command&&player_control>=0){ssize_t sent=write(player_control,&command,1);(void)sent;}
        return;
    }
    if(catalog_pid){if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){kill(catalog_pid,SIGKILL);waitpid(catalog_pid,NULL,0);catalog_pid=0;auto_active=0;snprintf(status,sizeof(status),"LOAD CANCELLED");}return;}
    if(search_on){
        size_t n=strlen(search_text);
        if(key==SDLK_ESCAPE){search_on=0;SDL_StopTextInput();}
        else if(key==SDLK_BACKSPACE){if(n)search_text[n-1]=0;else {search_on=0;SDL_StopTextInput();}}
        else if(key==SDLK_RIGHT&&search_key<39)search_key++;
        else if(key==SDLK_LEFT&&search_key>0)search_key--;
        else if(key==SDLK_DOWN&&search_key<30)search_key+=10;
        else if(key==SDLK_UP&&search_key>=10)search_key-=10;
        else if(key==SDLK_F4){char c=search_keys[search_key];if(c=='<'){if(n)search_text[n-1]=0;}else if(c=='>')action(SDLK_RETURN);else if(n<64){search_text[n]=c;search_text[n+1]=0;}}
        else if(key==SDLK_RETURN||key==SDLK_F3){Browse next=browse;next.page=1;next.selected=0;strcpy(next.query,search_text);search_on=0;SDL_StopTextInput();request_catalog(next,0,0);}
        return;
    }
    if(settings_on){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5){settings_on=0;save_settings();return;}
        if(key==SDLK_UP&&settings_row>0)settings_row--;
        if(key==SDLK_DOWN&&settings_row<6)settings_row++;
        int step=key==SDLK_LEFT?-1:1;
        if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN){
            switch(settings_row){
                case 0:quality_setting=(quality_setting+step+3)%3;break;
                case 1:buffer_setting=(buffer_setting+step+3)%3;break;
                case 2:disk_setting=(disk_setting+step+3)%3;break;
                case 3:coverflow=!coverflow;break;
                case 4:subtitle_setting=(subtitle_setting+step+4)%4;break;
                case 5:subtitle_scale=subtitle_scale==2?3:2;break;
                case 6:subtitle_delay+=step;if(subtitle_delay>5)subtitle_delay=-5;if(subtitle_delay< -5)subtitle_delay=5;break;
            }save_settings();
        }return;
    }
    if(about&&(key==SDLK_RETURN||key==SDLK_SPACE||key==SDLK_u)){start_update();return;}
    if(key==SDLK_u){start_update();return;}
    if(key==SDLK_F5){settings_on=1;about=0;return;}
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE) { if(about)about=0;else if(history_size)request_catalog(history[history_size-1],0,1);else if(browse.mode){Browse next={0,1,0,0,0,0,"",""};request_catalog(next,0,0);}else running=0; }
    else if(key==SDLK_F2){Browse next={browse.mode==0?1:browse.mode==1?2:browse.mode==2?7:0,1,0,0,0,0,"",""};history_size=0;request_catalog(next,0,0);}
    else if((key==SDLK_F3||key==SDLK_SLASH)&&(browse.mode==1||browse.mode==2||browse.mode==7)){search_on=1;search_key=0;strcpy(search_text,browse.query);SDL_StartTextInput();}
    else if(key==SDLK_F3)about=!about;
    else if(key==SDLK_i)about=!about;
    else if(key==SDLK_y)stars_on=!stars_on;
    else if(key==SDLK_v){viewing=(viewing+1)%VIEW_COUNT;snprintf(status,sizeof(status),"VIEW: %s / DURING PLAYBACK: Y CHANGE VIEW, B STOP",view_names[viewing]);}
    else if(key==SDLK_TAB&&!browse.mode){ready_only=!ready_only;filter();}
    else if(key==SDLK_RETURN||key==SDLK_SPACE)play();
    else if(!about&&total){
        if(key==SDLK_RIGHT){if(selection+1<total)selection++;else next_catalog_page(1);}
        if(key==SDLK_LEFT){if(selection>0)selection--;else next_catalog_page(-1);}
        if(coverflow&&(browse.mode<5||browse.mode>=7)&&(key==SDLK_UP||key==SDLK_DOWN)){next_catalog_page(key==SDLK_UP?-1:1);return;}
        int step=(browse.mode==5||browse.mode==6)?1:3;
        if(key==SDLK_DOWN && selection+step<total)selection+=step;
        else if(key==SDLK_DOWN)next_catalog_page(1);
        if(key==SDLK_UP && selection>=step)selection-=step;
        else if(key==SDLK_UP)next_catalog_page(-1);
        snprintf(status,sizeof(status),"%s",titles[visible[selection]].title);
    }
    if(key==SDLK_PAGEDOWN)next_catalog_page(1);
    if(key==SDLK_PAGEUP)next_catalog_page(-1);
}
static void add_pad(int device) {
    if(!SDL_IsGameController(device))return;
    controller_used();
    SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(device);
    for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==id)return;
    for(int i=0;i<4;i++)if(!pads[i]){pads[i]=SDL_GameControllerOpen(device);break;}
}
int main(int argc,char **argv) {
    const char *catalog="catalog.tsv",*shot=NULL;int frames=0,seen=0,initial_library=0;Uint32 start,last_draw=0;int dirty=1;
    load_settings();
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--version")){puts(FLXTR_VERSION);return 0;}
        else if(!strcmp(argv[i],"--grid"))coverflow=0;
        else if(!strcmp(argv[i],"--settings"))settings_on=1;
        else if(!strcmp(argv[i],"--catalog")&&i+1<argc)catalog=argv[++i];
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)shot=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--no-stars"))stars_on=0;
        else if(!strcmp(argv[i],"--library")&&i+1<argc){const char *v=argv[++i];initial_library=!strcmp(v,"movies")?1:!strcmp(v,"series")?2:!strcmp(v,"anime")?7:0;if(!initial_library)return 2;}
        else {fprintf(stderr,"Usage: %s [--catalog file] [--screenshot file.bmp] [--frames N] [--no-stars]\n",argv[0]);return 2;}
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if(SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)) {fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    if(open_ui()){fprintf(stderr,"Renderer: %s\n",SDL_GetError());SDL_Quit();return 1;}
    for(int i=0;i<SDL_NumJoysticks();i++)add_pad(i);
    uint32_t seed=0x31415926;
    for(int i=0;i<STARS;i++){seed=seed*1664525u+1013904223u;star_x[i]=seed>>24;star_y[i]=seed>>16;star_speed[i]=1+(seed%3);}
    local_catalog=catalog;read_catalog(catalog);start=SDL_GetTicks();controller_used();
    if(initial_library){Browse next={initial_library,1,0,0,0,0,"",""};request_catalog(next,0,0);}
    while(running) {
        SDL_Event event;
        if(SDL_WaitEventTimeout(&event,(stars_on||flow_animating())?33:250)) do {
            dirty=1;
            if(event.type==SDL_QUIT)running=0;
            else if(event.type==SDL_KEYDOWN)action(event.key.keysym.sym);
            else if(event.type==SDL_TEXTINPUT&&search_on){for(const char *p=event.text.text;*p;p++){size_t n=strlen(search_text);if(n<64&&(isalnum((unsigned char)*p)||*p==' '||*p=='-')){search_text[n]=(char)toupper((unsigned char)*p);search_text[n+1]=0;}}}
            else if(event.type==SDL_CONTROLLERDEVICEADDED)add_pad(event.cdevice.which);
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERAXISMOTION){int v=event.caxis.value;if((event.caxis.axis<4&&(v>8000||v< -8000))||(event.caxis.axis>=4&&v>8000))controller_used();}
            else if(event.type==SDL_CONTROLLERBUTTONUP)controller_used();
            else if(event.type==SDL_CONTROLLERBUTTONDOWN) {
                controller_used();
                switch(event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:action(search_on?SDLK_F4:SDLK_RETURN);break;
                    case SDL_CONTROLLER_BUTTON_B:action(search_on?SDLK_BACKSPACE:SDLK_ESCAPE);break;
                    case SDL_CONTROLLER_BUTTON_X:action(SDLK_F2);break;
                    case SDL_CONTROLLER_BUTTON_Y:action(SDLK_y);break;
                    case SDL_CONTROLLER_BUTTON_BACK:action(SDLK_F5);break;
                    case SDL_CONTROLLER_BUTTON_START:action(player_pid?SDLK_i:SDLK_F3);break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:action(player_pid?SDLK_LEFT:SDLK_v);break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:action(player_pid?SDLK_RIGHT:SDLK_v);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:action(SDLK_UP);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:action(SDLK_DOWN);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:action(SDLK_LEFT);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:action(SDLK_RIGHT);break;
                }
            }
        } while(SDL_PollEvent(&event));
        if(update_pid){int code;pid_t done=waitpid(update_pid,&code,WNOHANG);
            if(done==update_pid){update_pid=0;dirty=1;if(WIFEXITED(code)&&!WEXITSTATUS(code)){restart_shell=1;running=0;}else snprintf(status,sizeof(status),"UPDATE UNAVAILABLE - INSTALLED BUILD KEPT / SEE UPDATE.LOG");}}
        if(catalog_pid){finish_catalog();dirty=1;}
        prefetch_pages();controller_idle();
        if(player_pid){int code;pid_t p=waitpid(player_pid,&code,WNOHANG);
            if(p==player_pid){finish_player(code);dirty=1;}
            else if(stopping_player&&SDL_GetTicks()-stop_time>3000)kill(player_pid,SIGKILL);
            SDL_Delay(30);continue;
        }
        Uint32 now=SDL_GetTicks();
        if(dirty||((stars_on||flow_animating())&&now-last_draw>=33)||shot||frames){draw(now-start);last_draw=now;dirty=0;seen++;
            if(shot&&!catalog_pid){int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
                if(!s||SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch)||SDL_SaveBMP(s,shot)){fprintf(stderr,"Screenshot: %s\n",SDL_GetError());SDL_FreeSurface(s);return 1;}SDL_FreeSurface(s);running=0;}
            if(frames>0&&seen>=frames)running=0;
        }
    }
    if(player_pid){kill(player_pid,SIGKILL);waitpid(player_pid,NULL,0);}
    if(catalog_pid){kill(catalog_pid,SIGKILL);waitpid(catalog_pid,NULL,0);}
    if(prefetch_pid){kill(prefetch_pid,SIGKILL);waitpid(prefetch_pid,NULL,0);}
    if(idle_pid)waitpid(idle_pid,NULL,0);
    close_ui();for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerClose(pads[i]);
    if(update_pid)waitpid(update_pid,NULL,0);
    SDL_Quit();return restart_shell?42:0;
}
