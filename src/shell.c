#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <curl/curl.h>
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
#include "tv_schedule.h"
#include "tv_progress.h"
#include "tv_history.h"
#include "browse_filters.h"

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
static SDL_Texture *posters[VISIBLE],*background;
static SDL_GameController *pads[4];
static pid_t player_pid, update_pid;
static int restart_shell,game_exit, coverflow=1;
static int player_control=-1,play_after_load,player_menu;
static Title playing_title,bios_archive_title;
static double restart_position,auto_resume;
static TvBlock tv_blocks[TV_BLOCKS];
static int tv_active,tv_attempts,tv_schedule_on,tv_row,tv_column;
static int tv_breaks=1,tv_break_due,tv_in_break,filters_on,filter_row,filter_original_genre,filter_original_order;
static Uint32 tv_next_at;
static void tv_candidate(void);
static void tv_tick(void);
static int bios_archive_confirm,bios_selected,bios_confirm,bios_on,monochrome_menu,settings_on,settings_row,quality_setting=1,buffer_setting=0,disk_setting=1,subtitle_setting,subtitle_scale=2,subtitle_delay;
static const int qualities[]={480,720,1080},buffer_seconds[]={5,15,30},disk_megabytes[]={64,128,256};
static const char *const subtitle_languages[]={"off","auto","eng","nor"};
static void save_settings(void){
    FILE *f=fopen("settings.cfg.next","w");if(!f)return;
    fprintf(f,"quality=%d\nbuffer=%d\ndisk=%d\ncoverflow=%d\nsubtitles=%d\nscale=%d\ndelay=%d\n",quality_setting,buffer_setting,disk_setting,coverflow,subtitle_setting,subtitle_scale,subtitle_delay);
    fprintf(f,"tv_breaks=%d\nmenu_monochrome=%d\n",tv_breaks,monochrome_menu);
    for(int i=0;i<TV_BLOCKS;i++)fprintf(f,"tv_hour_%d=%d\ntv_genre_%d=%d\n",i,tv_blocks[i].hour,i,tv_blocks[i].genre);
    if(fclose(f)==0)rename("settings.cfg.next","settings.cfg");
}
static void load_settings(void){
    memcpy(tv_blocks,tv_defaults,sizeof(tv_blocks));
    FILE *f=fopen("settings.cfg","r");if(!f)return;char line[80],key[32];int value;
    while(fgets(line,sizeof(line),f))if(sscanf(line,"%31[^=]=%d",key,&value)==2){
        if(!strcmp(key,"tv_breaks")&&(value==0||value==1))tv_breaks=value;
        else if(!strcmp(key,"menu_monochrome")&&(value==0||value==1))monochrome_menu=value;
        else if(!strcmp(key,"quality")&&value>=0&&value<3)quality_setting=value;
        else if(!strcmp(key,"buffer")&&value>=0&&value<3)buffer_setting=value;
        else if(!strcmp(key,"disk")&&value>=0&&value<3)disk_setting=value;
        else if(!strcmp(key,"coverflow")&&(value==0||value==1))coverflow=value;
        else if(!strcmp(key,"subtitles")&&value>=0&&value<4)subtitle_setting=value;
        else if(!strcmp(key,"scale")&&value>=2&&value<=3)subtitle_scale=value;
        else if(!strcmp(key,"delay")&&value>=-5&&value<=5)subtitle_delay=value;
        else if(!strncmp(key,"tv_hour_",8)&&strlen(key)==9&&key[8]>='0'&&key[8]<'0'+TV_BLOCKS)tv_blocks[key[8]-'0'].hour=value;
        else if(!strncmp(key,"tv_genre_",9)&&strlen(key)==10&&key[9]>='0'&&key[9]<'0'+TV_BLOCKS)tv_blocks[key[9]-'0'].genre=value;
    }fclose(f);if(!tv_valid(tv_blocks))memcpy(tv_blocks,tv_defaults,sizeof(tv_blocks));
}
static float flow_position;
static int stopping_player;
static int viewing=VIEW_FIT;
static Uint32 stop_time;
typedef struct {int mode,page,id,season,episode,selected;char query[129],name[80];} Browse;
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
static const char *const browse_labels[]={"LOCAL","MOVIES","SERIES","SEASONS","EPISODES","SERVERS","QUALITY","KISSANIME","ANIME EPISODES","INTERNET ARCHIVE","VIDEO FILES","TV MODE","TV PICK","COMMERCIAL BREAK","GAMES","ARCHIVE PLATFORMS","IMPORTING GAME","ARCHIVE TITLES","ROM FILES","DOWNLOADING ROM","VERIFIED HOMEBREW","ROM FOLDERS","CONSOLE GAMES","BIOS ARCHIVE SEARCH","BIOS FILES"};
static const char *const browse_kinds[]={"","movie","tv","season","episode","source","quality","kiss","kiss-episodes","archive","archive-files","","tv-pick","tv-break","games","games-platforms","games-download","games-archive-search","games-archive-files","games-file-download","games-archive","games-folders","games-system","games-bios-search","games-bios-files"};
static int search_on,search_key;
static char search_text[65];
static const char search_keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -<>";
static char status[100]="SELECT A TITLE / Y GENRE AND MOST POPULAR";
static unsigned char star_x[STARS], star_y[STARS], star_speed[STARS];
static SDL_Color green={116,255,132,255}, dim={66,126,77,255}, white={212,226,214,255};

static void apply_menu_theme(void) {
    if(background&&!monochrome_menu){green=(SDL_Color){18,91,56,255};dim=(SDL_Color){62,74,72,255};white=(SDL_Color){24,32,30,255};}
    else {green=(SDL_Color){116,255,132,255};dim=(SDL_Color){66,126,77,255};white=(SDL_Color){212,226,214,255};}
}
static int bios_present(const char *filename) {
    char path[256];
    const char *dirs[]={"roms/NeoGeo","roms/neogeo","roms","system",
        "retroarch/system","../retroarch/system","../retroarch/roms/NeoGeo",
        "../retroarch/roms/neogeo","/home/apps/retroarch/system"};
    for(size_t i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        if(snprintf(path,sizeof(path),"%s/%s",dirs[i],filename)>=(int)sizeof(path))continue;
        if(access(path,R_OK)==0)return 1;
    }
    return 0;
}
static const char *const bios_files[]={"neogeo.zip","scph5500.bin","scph5501.bin","scph5502.bin","gba_bios.bin"};
static int bios_download(void){
    FILE *f=fopen("bios-sources.tsv","r");if(!f)return 2;
    char line[1024],name[64],url[768]={0},sha[65]={0},digest[65];
    while(fgets(line,sizeof(line),f)){
        char address[768];
        if(sscanf(line,"%63s %767s %64s",name,address,digest)!=3)continue;
        if(strcmp(name,bios_files[bios_selected])||strncmp(address,"https://",8)||strlen(digest)!=64)continue;
        int valid=1;for(int i=0;i<64;i++)if(!isxdigit((unsigned char)digest[i]))valid=0;
        if(valid){strcpy(url,address);strcpy(sha,digest);break;}
    }
    fclose(f);if(!*url)return 2;
    if(mkdir("system",0700)<0&&errno!=EEXIST)return 3;
    char target[128],tmp[128];snprintf(target,sizeof(target),"system/%s",bios_files[bios_selected]);
    snprintf(tmp,sizeof(tmp),"system/.%s.download",bios_files[bios_selected]);
    if(access(target,F_OK)==0)return 4;
    unlink(tmp);
    pid_t child=fork();if(child<0)return 3;
    if(!child){execlp("curl","curl","-fLsS","--proto","=https","--proto-redir","=https","--connect-timeout","10","--max-time","90","--max-filesize","16777216","--cacert","certs/cacert.pem","-o",tmp,url,(char*)NULL);_exit(127);}
    int code=0;if(waitpid(child,&code,0)!=child||!WIFEXITED(code)||WEXITSTATUS(code)){unlink(tmp);return 5;}
    f=fopen("bios-check.txt","w");if(!f){unlink(tmp);return 3;}
    fprintf(f,"%s  %s\n",sha,tmp);fclose(f);
    child=fork();if(child<0){unlink(tmp);unlink("bios-check.txt");return 3;}
    if(!child){int devnull=open("/dev/null",O_WRONLY);if(devnull>=0){dup2(devnull,STDOUT_FILENO);close(devnull);}execlp("sha256sum","sha256sum","-c","bios-check.txt",(char*)NULL);_exit(127);}
    code=0;int ok=waitpid(child,&code,0)==child&&WIFEXITED(code)&&WEXITSTATUS(code)==0;
    unlink("bios-check.txt");if(!ok){unlink(tmp);return 6;}
    if(link(tmp,target)<0){unlink(tmp);return 4;}
    unlink(tmp);return 0;
}
static int bios_archive_download(const Title *item){
    if(item->id<1||item->id>5)return 2;
    if(strncmp(item->meta,"SHA1:",5)||strlen(item->meta+5)!=40)return 2;
    for(int i=5;i<45;i++)if(!isxdigit((unsigned char)item->meta[i]))return 2;
    const char *prefix="https://archive.org/download/";
    if(strncmp(item->url,prefix,strlen(prefix)))return 2;
    const char *names[]={"neogeo.zip","scph5500.bin","scph5501.bin","scph5502.bin","gba_bios.bin"};
    const char *name=names[item->id-1];
    const char *slash=strrchr(item->url,'/');
    int is_zip=!strcmp(item->kind,"bios-zip");
    if(!slash||(!is_zip&&strcmp(slash+1,name)))return 2;
    if(is_zip&&(item->id==1||!strstr(slash+1,".zip")))return 2;
    if(mkdir("system",0700)<0&&errno!=EEXIST)return 3;
    char dest[96],temp[128];snprintf(dest,sizeof(dest),"system/%s",name);snprintf(temp,sizeof(temp),"system/.%s.partial",name);
    if(access(dest,F_OK)==0)return 4;
    unlink(temp);
    FILE *output=fopen(temp,"wb");if(!output)return 3;
    CURL *curl=curl_easy_init();if(!curl){fclose(output);unlink(temp);return 3;}
    curl_easy_setopt(curl,CURLOPT_URL,item->url);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,4L);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,output);
    curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,120L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,45L);
    curl_easy_setopt(curl,CURLOPT_CAINFO,access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    CURLcode result=curl_easy_perform(curl);long http=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);
    curl_easy_cleanup(curl);
    int output_error=fclose(output)!=0;
    struct stat downloaded;
    if(result!=CURLE_OK||http!=200||output_error||stat(temp,&downloaded)||
       downloaded.st_size<=0||downloaded.st_size>16777216){
        fprintf(stderr,"BIOS Archive transfer failed: HTTP %ld / curl %d\\n",http,(int)result);
        unlink(temp);return 5;
    }
    pid_t pid;int code=0;
    FILE *f=fopen("bios-archive-check.txt","w");if(!f){unlink(temp);return 3;}
    fprintf(f,"%s  %s\n",item->meta+5,temp);fclose(f);
    pid=fork();if(pid<0){unlink(temp);unlink("bios-archive-check.txt");return 3;}
    if(!pid){
        int fd=open("/dev/null",O_WRONLY);if(fd>=0){dup2(fd,STDOUT_FILENO);close(fd);}
        execlp("sha1sum","sha1sum","-c","bios-archive-check.txt",(char*)NULL);_exit(127);
    }
    code=0;int verified=waitpid(pid,&code,0)==pid&&WIFEXITED(code)&&WEXITSTATUS(code)==0;
    unlink("bios-archive-check.txt");
    if(!verified){unlink(temp);return 6;}
    if(is_zip){
        /* Archive checksum has already been checked. Inspect members without extracting paths. */
        int pipes[2];if(pipe(pipes)<0){unlink(temp);return 3;}
        pid=fork();if(pid<0){close(pipes[0]);close(pipes[1]);unlink(temp);return 3;}
        if(!pid){
            dup2(pipes[1],STDOUT_FILENO);close(pipes[0]);close(pipes[1]);
            execlp("unzip","unzip","-Z","-1",temp,(char*)NULL);_exit(127);
        }
        close(pipes[1]);
        FILE *listing=fdopen(pipes[0],"r");char line[512],member[512]="";
        if(listing){
            while(fgets(line,sizeof(line),listing)){
                size_t n=strcspn(line,"\r\n");if(line[n]!='\n'&&line[n]!='\r'&&!feof(listing))continue;
                line[n]=0;const char *base=strrchr(line,'/');base=base?base+1:line;
                if(!strcmp(base,name)&&!strstr(line,"..")&&!strchr(line,'\\')){
                    snprintf(member,sizeof(member),"%s",line);
                }
            }
            fclose(listing);
        }else close(pipes[0]);
        int listcode=0;waitpid(pid,&listcode,0);
        if(!member[0]||!WIFEXITED(listcode)||WEXITSTATUS(listcode)!=0){unlink(temp);return 7;}
        char extracted[128];snprintf(extracted,sizeof(extracted),"system/.%s.extracted",name);
        unlink(extracted);
        int fd=open(extracted,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0){unlink(temp);return 3;}
        pid=fork();if(pid<0){close(fd);unlink(extracted);unlink(temp);return 3;}
        if(!pid){
            dup2(fd,STDOUT_FILENO);close(fd);
            execlp("unzip","unzip","-p",temp,member,(char*)NULL);_exit(127);
        }
        close(fd);int extractcode=0;waitpid(pid,&extractcode,0);
        struct stat contents;
        int good=WIFEXITED(extractcode)&&WEXITSTATUS(extractcode)==0&&
            !stat(extracted,&contents)&&contents.st_size==((item->id==5)?16384:524288);
        unlink(temp);
        if(!good){unlink(extracted);return 7;}
        if(link(extracted,dest)<0){unlink(extracted);return 4;}
        unlink(extracted);return 0;
    }
    if(link(temp,dest)<0){unlink(temp);return 4;}
    unlink(temp);return 0;
}
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
    SDL_DestroyTexture(background);background=NULL;
    SDL_DestroyRenderer(renderer);renderer=NULL;
    SDL_DestroyWindow(window);window=NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
static int open_ui(void) {
    if(SDL_InitSubSystem(SDL_INIT_VIDEO))return -1;
    /* Vivante cannot resize its native window after creation. Fullscreen flags
     * alone update SDL's size while leaving a smaller EGL surface underneath. */
    const char *driver=SDL_GetCurrentVideoDriver();int dummy=driver&&!strcmp(driver,"dummy");
    SDL_DisplayMode display;int width=W,height=H;
    if(!dummy&&!SDL_GetCurrentDisplayMode(0,&display)&&display.w>0&&display.h>0){width=display.w;height=display.h;}
    window=SDL_CreateWindow("FlXtR Steamlink",dummy?SDL_WINDOWPOS_CENTERED:0,dummy?SDL_WINDOWPOS_CENTERED:0,width,height,SDL_WINDOW_SHOWN | (dummy?0:SDL_WINDOW_FULLSCREEN_DESKTOP));
    if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){close_ui();return -1;}
    SDL_RenderSetLogicalSize(renderer,W,H);SDL_ShowCursor(SDL_DISABLE);
    int output_w=0,output_h=0;SDL_GetRendererOutputSize(renderer,&output_w,&output_h);
    fprintf(stderr,"Shell display: %s window=%dx%d output=%dx%d logical=%dx%d\n",driver?driver:"unknown",width,height,output_w,output_h,W,H);
    green=(SDL_Color){116,255,132,255};dim=(SDL_Color){66,126,77,255};white=(SDL_Color){212,226,214,255};
    struct stat bg_info;
    if(!stat("assets/white-metal-droplets.bmp",&bg_info)&&bg_info.st_size==1555254){
        SDL_Surface *surface=SDL_LoadBMP("assets/white-metal-droplets.bmp");
        if(surface){if(surface->w==W&&surface->h==H)background=SDL_CreateTextureFromSurface(renderer,surface);SDL_FreeSurface(surface);}
    }
    apply_menu_theme();
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
/* A small RAM-only LRU catalog. Reuse loaded pages until process restart.
 * We never cache playback sources or actions that mutate downloaded files. */
#define LIBRARY_RAM_CACHE 16
typedef struct {
    int valid,count,pages,items;
    Browse key;
    Title rows[VISIBLE];
} LibraryPage;
static LibraryPage library_pages[LIBRARY_RAM_CACHE];
static unsigned library_cache_cursor;
static int library_cacheable(int mode){
    return mode==1||mode==2||mode==3||mode==4||mode==7||mode==8||
        mode==9||mode==10||mode==14||mode==15||mode==17||mode==18||
        mode==20||mode==21||mode==22||mode==23||mode==24;
}
static int library_page_equals(const Browse *a,const Browse *b){
    return a->mode==b->mode&&a->page==b->page&&a->id==b->id&&
        a->season==b->season&&a->episode==b->episode&&
        !strcmp(a->query,b->query);
}
static void library_cache_store(const Browse *key){
    if(!library_cacheable(key->mode)||count>VISIBLE)return;
    LibraryPage *p=NULL;
    for(int i=0;i<LIBRARY_RAM_CACHE;i++)
        if(library_pages[i].valid&&library_page_equals(&library_pages[i].key,key)){
            p=&library_pages[i];break;
        }
    if(!p)p=&library_pages[library_cache_cursor++%LIBRARY_RAM_CACHE];
    p->valid=1;p->key=*key;p->count=count;
    p->pages=listing_pages;p->items=listing_total;
    memcpy(p->rows,titles,(size_t)count*sizeof(Title));
}
static int library_cache_restore(Browse next,int push,int pop){
    if(!library_cacheable(next.mode))return 0;
    for(int i=0;i<LIBRARY_RAM_CACHE;i++){
        LibraryPage *p=&library_pages[i];
        if(!p->valid||!library_page_equals(&p->key,&next))continue;
        if(push&&history_size<4)history[history_size++]=browse;
        if(pop&&history_size)history_size--;
        browse=next;count=p->count;listing_pages=p->pages;listing_total=p->items;
        ready_only=0;about=0;memset(titles,0,sizeof(titles));
        memcpy(titles,p->rows,(size_t)count*sizeof(Title));
        filter();
        if(next.selected>=0&&next.selected<total)selection=next.selected;
        snprintf(status,sizeof(status),"%s / CACHED PAGE %d OF %d",
            browse_labels[next.mode],next.page,listing_pages);
        return 1;
    }
    return 0;
}
static void request_catalog(Browse next,int push,int pop){
    if(next.mode!=5&&next.mode!=6&&next.mode!=12&&next.mode!=13)tv_active=0;
    if(next.mode==11){browse=next;history_size=0;about=0;total=count=0;prefetch_left=0;snprintf(status,sizeof(status),"A START TV / SELECT SETTINGS / OSLO TIME");return;}
    if(catalog_pid)return;
    if(prefetch_pid){kill(prefetch_pid,SIGKILL);waitpid(prefetch_pid,NULL,0);prefetch_pid=0;}prefetch_left=0;
    if(!next.mode){
        browse=next;history_size=0;count=0;ready_only=0;listing_pages=1;memset(titles,0,sizeof(titles));read_catalog(local_catalog);return;
    }
    if(!auto_active&&library_cache_restore(next,push,pop))return;
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
    else snprintf(status,sizeof(status),"LOADING %s - X SKIP / B CANCEL",browse_labels[next.mode]);
}
/* Automatic lookup uses its own small queue and leaves the visible library intact. */
static void auto_request(Browse next){
    request_catalog(next,0,0);
    if(!catalog_pid)auto_active=0;
}
static void auto_next(void){
    if(auto_index>=auto_count){auto_active=0;snprintf(status,sizeof(status),auto_count?"ALL SOURCES FAILED - SEE CATALOG.LOG / PLAYER.LOG":"NO SERVER OPTIONS - SEE CATALOG.LOG");return;}
    Browse next={6,1,auto_title.id,auto_title.season,auto_title.episode,0,"",""};
    strcpy(next.query,auto_servers[auto_index]);auto_request(next);
    if(auto_active)snprintf(status,sizeof(status),"TRYING SERVER %d OF %d - B TO CANCEL",auto_index+1,auto_count);
}
static void auto_result(int success){
    if(!success){
        if(pending.mode==6){auto_index++;auto_next();}
        else {auto_active=0;snprintf(status,sizeof(status),"SERVER DISCOVERY FAILED - SEE CATALOG.LOG");}
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
    if(pending.mode==6)fprintf(stderr,"Source lookup returned no usable URL for server %s\n",pending.query);
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
    if(pending.mode==13){
        if(tv_active&&WIFEXITED(code)&&!WEXITSTATUS(code)){
            count=total=0;selection=0;read_catalog("catalog-cache/result.tsv");
            if(total&&!strcmp(titles[visible[0]].kind,"tv-break")){tv_in_break=1;launch_player(&titles[visible[0]]);if(!player_pid)tv_in_break=0;}
        }
        tv_next_at=0;return;
    }
    if(pending.mode==12){
        if(WIFEXITED(code)&&!WEXITSTATUS(code)&&tv_active)tv_candidate();
        else snprintf(status,sizeof(status),"TV: NO EPISODE FOUND / TRYING ANOTHER SHOW");
        tv_next_at=SDL_GetTicks()+10000;return;
    }
    if(!WIFEXITED(code)||WEXITSTATUS(code)){play_after_load=0;snprintf(status,sizeof(status),(pending.mode==16||pending.mode==19)?"ROM DOWNLOAD FAILED - SEE CATALOG.LOG":pending.mode==6?"SERVER UNAVAILABLE - SELECT ANOTHER SERVER":"COULD NOT LOAD - CHECK CONNECTION / CATALOG.LOG");return;}
    for(int i=0;i<VISIBLE;i++){char src[80],dst[80];snprintf(src,sizeof(src),"catalog-cache/poster-%d.next.bmp",i);snprintf(dst,sizeof(dst),"catalog-cache/poster-%d.bmp",i);rename(src,dst);}
    if(pending_push&&history_size<4)history[history_size++]=browse;
    if(pending_pop&&history_size)history_size--;
    if(pending.mode==16||pending.mode==19)pending.mode=14;
    if(pending.mode==14)pending.id=0;
    browse=pending;count=0;ready_only=0;about=0;listing_pages=1;listing_total=0;memset(titles,0,sizeof(titles));read_catalog("catalog-cache/result.tsv");
    if(browse.selected>=0&&browse.selected<total)selection=browse.selected;
    library_cache_store(&browse);
    if((browse.mode<5||browse.mode>=7)&&browse.mode!=23&&browse.mode!=24){prefetch_browse=browse;prefetch_left=browse.page==1?2:1;}
    if((browse.mode==5||browse.mode==6)&&!total)snprintf(status,sizeof(status),"NO SOURCES RETURNED FOR THIS TITLE - B TO RETURN");
    else snprintf(status,sizeof(status),"%s / PAGE %d OF %d / X CHANGE LIBRARY",browse_labels[browse.mode],browse.page,listing_pages);
    if(browse.mode==7||browse.mode==8)snprintf(status,sizeof(status),"KISSANIME: BROWSING WORKS / SOME VIDEO HOSTS ARE NOT SUPPORTED");
    if(browse.mode==14)snprintf(status,sizeof(status),"Y IMPORT / START LICENSED DOWNLOADS / SHARE+OPTIONS EXIT GAME");
    if(browse.mode==17&&!total)snprintf(status,sizeof(status),"NO ARCHIVE TITLES FOUND - TRY ANOTHER PLATFORM");
    if(browse.mode==18&&!total)snprintf(status,sizeof(status),"NO SUPPORTED ROM FILES IN THIS ARCHIVE ITEM");
    if(browse.mode==14){FILE *f=fopen("game-exit-status","r");int code=0;if(f){int got=fscanf(f,"%d",&code);fclose(f);unlink("game-exit-status");if(got==1&&code)snprintf(status,sizeof(status),"GAME COULD NOT START / SEE GAME.LOG / Y REIMPORT");}}
    if(browse.mode==10&&!total)snprintf(status,sizeof(status),"NO PUBLIC H.264 / MPEG4 FILES IN THIS ITEM");
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
    if(browse.mode==21){
        SDL_Color ink={116,255,132,255},fill={24,62,39,255};
        rect(x,y,w,h,fill,1);
        int bx=x+w/7,by=y+h/3,bw=w*5/7,bh=h/3;
        rect(bx,by,bw/3,7,ink,1);
        rect(bx,by+7,bw,bh,ink,0);
        rect(bx+3,by+10,bw-6,bh-6,fill,1);
        if(seed>=0&&seed<MAX_TITLES){
            const char *name=titles[seed].title;
            text(x+8,y+h*3/4,name,1,ink,18);
        }
        return;
    }
    SDL_Color a={10,23,16,255}, b={23,57,35,255};rect(x,y,w,h,a,1);
    for(int i=0;i<6;i++)rect(x+8+i*9,y+h/2-i*7,w-16-i*18,3,b,1);
    text(x+10,y+12,"NO ART",1,(SDL_Color){150,190,160,255},12);
    char code[16];snprintf(code,sizeof(code),"%03d",seed+1);text(x+10,y+h-28,code,2,(SDL_Color){116,255,132,255},5);
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
        }else {rect(dst.x,dst.y,w,h,(SDL_Color){8,23,14,255},1);text(dst.x+6,dst.y+h/2,"FLXTR",1,(SDL_Color){150,190,160,255},w/6-1);}
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
    if(background&&!monochrome_menu){
        SDL_RenderCopy(renderer,background,NULL,NULL);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        rect(20,16,920,55,(SDL_Color){255,255,255,180},1);
        rect(20,80,155,400,(SDL_Color){255,255,255,208},1);
        rect(20,483,920,43,(SDL_Color){255,255,255,224},1);
        rect(184,419,746,63,(SDL_Color){255,255,255,208},1);
        if(settings_on||bios_on||filters_on||search_on||about||tv_schedule_on||browse.mode==11)rect(184,80,746,339,(SDL_Color){255,255,255,200},1);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
    }
    if(stars_on&&(!background||monochrome_menu))for(int i=0;i<STARS;i++) {
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
    text(30,208,root_mode==9?"> ARCHIVE":"  ARCHIVE",1,root_mode==9?green:dim,20);
    text(30,229,root_mode==11?"> TV MODE":"  TV MODE",1,root_mode==11?green:dim,20);
    text(30,250,root_mode==14?"> GAMES":"  GAMES",1,root_mode==14?green:dim,20);
    char num[64];snprintf(num,sizeof(num),browse.mode==7?"%d+ TITLES":"%d TITLES",browse.mode?listing_total:total);text(30,287,num,1,white,20);
    text(30,379,"[SELECT] SETTINGS",1,dim,22);text(30,402,"[X] LIBRARY",1,dim,22);text(30,421,browse.mode==1||browse.mode==2||browse.mode==7?"[Y] GENRE / SECTION":browse.mode==14?"[Y] ROM FOLDERS":browse.mode==21?"[A] SELECT SYSTEM":background?"WHITE METAL":"[Y] STARS",1,dim,22);text(30,440,browse.mode==1||browse.mode==2||browse.mode==7||browse.mode==9||browse.mode==17?"[START] SEARCH":browse.mode==14?"[START] GET GAMES":browse.mode==21?"[A] OPEN FOLDER":"[START] ABOUT",1,dim,22);text(30,459,"B BACK",1,dim,22);
    if(filters_on){
        text(194,100,"BROWSE FILTERS",3,green,40);
        const char *genre=browse.mode==7&&browse.id==11?"KIDS":browse_genres[browse.id];
        text(204,180,filter_row==0?"> GENRE":"  GENRE",2,green,30);text(500,180,genre,2,white,30);
        text(204,240,filter_row==1?"> SECTION":"  SECTION",2,green,30);text(500,240,browse_orders[browse.season],2,white,30);
        text(194,350,"UP/DOWN ROW / LEFT/RIGHT CHANGE",2,green,60);
        text(194,400,"A APPLY / B OR X SKIP",2,green,60);
    }else if(tv_schedule_on||(browse.mode==11&&!settings_on&&!about)){
        text(194,94,tv_schedule_on?"EDIT TV SCHEDULE":"TV MODE / OSLO TIME",2,green,55);
        int active=tv_block_at(tv_blocks,tv_hour());
        for(int i=0;i<TV_BLOCKS;i++){
            char line[80];snprintf(line,sizeof(line),"%c %02d-%02d  %s",i==active?'>':' ',tv_blocks[i].hour,i+1<TV_BLOCKS?tv_blocks[i+1].hour:24,tv_genres[tv_blocks[i].genre]);
            int y=143+i*37;if(tv_schedule_on&&i==tv_row)rect(190,y-8,724,30,green,0);
            text(202,y,line,2,i==active||i==tv_row?green:white,58);
        }
        text(194,430,tv_schedule_on?(tv_column?"EDIT GENRE: LEFT/RIGHT / A EDIT HOUR":"EDIT START HOUR: LEFT/RIGHT / A EDIT GENRE"):"A START TV / SELECT EDIT SCHEDULE",1,green,90);
        text(194,455,tv_schedule_on?"UP/DOWN ROW / B SAVE":"EPISODES FINISH BEFORE THE NEXT GENRE BLOCK",1,dim,90);
    }else if(bios_on){
        text(194,94,"GAME BIOS STATUS",3,green,40);
        const char *const *files=bios_files;
        const char *labels[]={"NEO GEO","PLAYSTATION JP","PLAYSTATION US","PLAYSTATION EU","GAME BOY ADVANCE"};
        for(int i=0;i<5;i++){
            int y=155+i*43;
            text(205,y,labels[i],2,i==bios_selected?green:white,24);
            text(545,y,bios_present(files[i])?"FOUND":"NOT FOUND",2,bios_present(files[i])?green:dim,12);
            text(205,y+19,files[i],1,dim,40);
        }
        text(194,389,bios_confirm?"WARNING: ONLY DOWNLOAD LICENSED BIOS":"Y SOURCE DOWNLOAD / START ARCHIVE SEARCH",1,green,80);
        text(194,412,bios_confirm?"A CONFIRM / B CANCEL":"/HOME/APPS/GREENLINK/SYSTEM/",1,white,80);
        text(194,452,"CONFIGURE HTTPS URL AND SHA256 IN BIOS-SOURCES.TSV",1,dim,80);
        text(194,474,status,1,green,86);
    }else if(settings_on){
#ifdef FLXTR_DESKTOP
        const char *labels[]={"QUALITY TARGET","PREBUFFER","RAM LIMIT","LIBRARY VIEW","SUBTITLES","SUBTITLE SIZE","SUBTITLE DELAY","TV SCHEDULE","TV COMMERCIALS","MENU THEME","GAME BIOS"};
#else
        const char *labels[]={"QUALITY","PREBUFFER","DISK LIMIT","LIBRARY VIEW","SUBTITLES","SUBTITLE SIZE","SUBTITLE DELAY","TV SCHEDULE","TV COMMERCIALS","MENU THEME","GAME BIOS"};
#endif
        char values[11][40];snprintf(values[0],40,"%dP",qualities[quality_setting]);snprintf(values[1],40,"%d SECONDS",buffer_seconds[buffer_setting]);
        snprintf(values[2],40,"%d MB",disk_megabytes[disk_setting]);snprintf(values[3],40,"%s",coverflow?"COVERFLOW":"SIX-COVER WALL");
        const char *sub_names[]={"OFF","AUTOMATIC","ENGLISH","NORWEGIAN"};snprintf(values[4],40,"%s",sub_names[subtitle_setting]);snprintf(values[5],40,"%s",subtitle_scale==2?"NORMAL":"LARGE");snprintf(values[6],40,"%+d SECONDS",subtitle_delay);
        snprintf(values[7],40,"EDIT HOURS / GENRES");snprintf(values[8],40,"%s",tv_breaks?"BETWEEN PROGRAMS":"OFF");snprintf(values[9],40,"%s",monochrome_menu?"CLASSIC MONOCHROME":"WHITE METAL");snprintf(values[10],40,"VIEW BIOS STATUS");
        text(194,94,"SETTINGS",3,green,40);
        for(int i=0;i<11;i++){int y=134+i*27;rect(190,y-9,724,34,i==settings_row?green:dim,0);text(204,y,labels[i],2,white,24);text(566,y,values[i],2,i==settings_row?green:dim,28);}
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
        wrap(194,155,"A QUIET LIBRARY FOR YOUR STEAM LINK. WHITE METAL, WATER DROPS, AND GREEN PIXELS.",53,4,white);
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
            const char *label=!strcmp(t->kind,"archive")?"BROWSE VIDEO FILES":!strcmp(t->kind,"kiss")?"BROWSE EPISODES":!strcmp(t->kind,"tv")?"BROWSE SEASONS":!strcmp(t->kind,"season")?"BROWSE EPISODES":!strcmp(t->kind,"archive-game")?"A DOWNLOAD GAME":!strcmp(t->kind,"game")?"A PLAY GAME":t->url[0]?"PLAYABLE SOURCE":"A PLAY / AUTO SERVER";
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
    if(bios_archive_confirm){rect(190,210,680,110,green,0);text(207,225,"ARCHIVE BIOS / COPYRIGHT WARNING",2,green,45);text(207,259,"CONFIRM RIGHTS AND SOURCE BEFORE INSTALL",1,white,75);text(207,289,"A DOWNLOAD / B OR X CANCEL",2,green,42);}
    if(catalog_pid){rect(190,227,680,70,background?(SDL_Color){248,250,249,255}:(SDL_Color){0,0,0,255},1);rect(190,227,680,70,green,0);text(212,250,auto_active?"FINDING A WORKING SERVER... B CANCEL":"LOADING... B TO CANCEL",2,green,50);}
    rect(30,489,900,1,dim,1);text(30,509,status,1,white,92);text(713,509,"DPAD MOVE / B BACK",1,green,30);
    SDL_RenderPresent(renderer);
}
static void play(void) {
    if(settings_on||about||!total||player_pid)return;
    restart_position=auto_resume=0;
    Title *t=&titles[visible[selection]];
    if(!strcmp(t->kind,"bios-item")){
        const char *prefix="https://archive.org/details/";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)||strlen(t->url+n)>=129){snprintf(status,sizeof(status),"INVALID BIOS ARCHIVE ITEM");return;}
        Browse next={24,1,t->id,0,0,0,"",""};
        snprintf(next.query,sizeof(next.query),"%s",t->url+n);
        snprintf(next.name,sizeof(next.name),"%s",t->title);
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"bios-file")||!strcmp(t->kind,"bios-zip")){
        bios_archive_title=*t;bios_archive_confirm=1;
        snprintf(status,sizeof(status),"ARCHIVE BIOS: VERIFY RIGHTS / A CONFIRM / B CANCEL");return;
    }
    if(!strcmp(t->kind,"game-folder")){
        Browse next={22,1,t->id,0,0,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"game-curated")){Browse next={20,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"game-platform")){
        Browse next={17,1,t->id,0,0,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"game-item")){
        const char *prefix="https://archive.org/details/";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)||strlen(t->url+n)>=129){snprintf(status,sizeof(status),"INVALID ARCHIVE ITEM");return;}
        Browse next={18,1,t->id,0,0,0,"",""};snprintf(next.query,sizeof(next.query),"%s",t->url+n);
        snprintf(next.name,sizeof(next.name),"%s",t->title);request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"game-rom")){
        const char *prefix="https://archive.org/download/";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)){snprintf(status,sizeof(status),"INVALID ROM LINK");return;}
        const char *slash=strchr(t->url+n,'/');if(!slash){snprintf(status,sizeof(status),"INVALID ROM LINK");return;}
        char item[129];size_t ilen=(size_t)(slash-(t->url+n));
        if(ilen<1||ilen>=sizeof(item)){snprintf(status,sizeof(status),"INVALID ARCHIVE ID");return;}
        memcpy(item,t->url+n,ilen);item[ilen]=0;
        char name[161];int ni=0;const char *p=slash+1;
        while(*p&&ni<160){
            if(*p=='%'&&isxdigit((unsigned char)p[1])&&isxdigit((unsigned char)p[2])){
                char hex[3]={p[1],p[2],0};name[ni++]=(char)strtol(hex,NULL,16);p+=3;
            }else name[ni++]=*p++;
        }name[ni]=0;
        Browse next={19,1,0,0,0,0,"",""};
        int written=snprintf(next.query,sizeof(next.query),"%s|%s",item,name);
        if(written<0||written>=(int)sizeof(next.query)){snprintf(status,sizeof(status),"ROM FILENAME TOO LONG");return;}
        request_catalog(next,0,0);return;
    }
    if(!strcmp(t->kind,"archive-game")){Browse next={16,1,t->id,0,0,0,"",""};request_catalog(next,0,0);return;}
    if(!strcmp(t->kind,"game")){
        int back=-1,start=-1;
        for(int i=0;i<4;i++)if(pads[i]){SDL_GameControllerButtonBind b=SDL_GameControllerGetBindForButton(pads[i],SDL_CONTROLLER_BUTTON_BACK),a=SDL_GameControllerGetBindForButton(pads[i],SDL_CONTROLLER_BUTTON_START);if(b.bindType==SDL_CONTROLLER_BINDTYPE_BUTTON&&a.bindType==SDL_CONTROLLER_BINDTYPE_BUTTON){back=b.value.button;start=a.value.button;break;}}
        FILE *f=fopen("game-request","w");if(!f){snprintf(status,sizeof(status),"COULD NOT START GAME");return;}
        fprintf(f,"%d %d %d\n",t->id,back,start);if(fclose(f)){snprintf(status,sizeof(status),"COULD NOT START GAME");return;}game_exit=1;running=0;return;
    }
    if(!strcmp(t->kind,"archive")){
        const char *prefix="https://archive.org/details/";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)||strlen(t->url+n)>128){snprintf(status,sizeof(status),"INVALID ARCHIVE ITEM");return;}
        Browse next={10,1,0,0,0,0,"",""};strcpy(next.query,t->url+n);snprintf(next.name,sizeof(next.name),"%s",t->title);request_catalog(next,1,0);return;
    }
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
static void tv_candidate(void){
    count=total=0;selection=0;memset(titles,0,sizeof(titles));read_catalog("catalog-cache/result.tsv");
    if(!total)return;
    Title *t=&titles[visible[0]];if(strcmp(t->kind,"episode")||t->id<1)return;
    tv_recent_add(t->id,t->season,t->episode);
    fprintf(stderr,"TV selected %d S%d E%d: %s\n",t->id,t->season,t->episode,t->title);
    auto_title=*t;auto_active=1;auto_count=auto_index=0;auto_resume=restart_position=0;
    Browse next={5,1,t->id,t->season,t->episode,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);auto_request(next);
}
static void tv_tick(void){
    if(!tv_active||catalog_pid||player_pid||auto_active||settings_on||tv_schedule_on||(Sint32)(SDL_GetTicks()-tv_next_at)<0)return;
    if(tv_break_due&&tv_breaks){tv_break_due=0;Browse ad={13,1,tv_sequence("tv-break-sequence.txt"),0,0,0,"",""};request_catalog(ad,0,0);return;}
    tv_break_due=0;
    if(tv_attempts++>=12){tv_active=0;snprintf(status,sizeof(status),"TV: NO WORKING SOURCES / A RETRY");return;}
    int block=tv_block_at(tv_blocks,tv_hour());
    Browse next={12,1,tv_sequence("tv-sequence.txt"),0,0,0,"",""};snprintf(next.query,sizeof(next.query),"%d",tv_blocks[block].genre);
    request_catalog(next,0,0);tv_next_at=SDL_GetTicks()+10000;
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
        if(!strcmp(t->kind,"kiss-episode"))setenv("FLXTR_MEDIA_PROVIDER","megaplay",1);
        else unsetenv("FLXTR_MEDIA_PROVIDER");
        char height[16],buffer[16],disk[16],scale[16],delay[16],control[16],start[32];
        snprintf(control,sizeof(control),"%d",controls[0]);snprintf(start,sizeof(start),"%.3f",restart_position);snprintf(height,sizeof(height),"%d",t->height?t->height:qualities[quality_setting]);
        snprintf(buffer,sizeof(buffer),"%d",buffer_seconds[buffer_setting]);snprintf(disk,sizeof(disk),"%d",disk_megabytes[disk_setting]);
        snprintf(scale,sizeof(scale),"%d",subtitle_scale);snprintf(delay,sizeof(delay),"%d",subtitle_delay);
        if(!player_menu)execl("./greenlink-player","greenlink-player",t->url,"--view",modes[viewing],"--height",height,"--buffer-seconds",buffer,"--buffer-mb",disk,"--subtitles",subtitle_languages[subtitle_setting],"--subtitle-size",scale,"--subtitle-delay",delay,(char*)NULL);
        char *args[26]={"greenlink-player",(char*)t->url,"--view",(char*)modes[viewing],"--height",height,"--buffer-seconds",buffer,"--buffer-mb",disk,"--subtitles",(char*)subtitle_languages[subtitle_setting],"--subtitle-size",scale,"--subtitle-delay",delay,"--control-fd",control,"--start",start,NULL,NULL,NULL};
        int n=20;if(tv_in_break)args[n++]="120";if(tv_active||browse.mode==4||browse.mode==8)args[n++]="--episodes";if(auto_active)args[n++]="--servers";args[n]=NULL;
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
        if(command==7){tv_active=0;auto_active=0;tv_in_break=tv_break_due=0;snprintf(status,sizeof(status),"PLAYBACK STOPPED");return;}
        if(command<=3){
            if(command>1){quality_setting=(quality_setting+(command==2?1:2))%3;playing_title.height=0;save_settings();}
            restart_position=auto_resume=position;
            if(auto_active&&!strcmp(playing_title.kind,"kiss-episode"))auto_next();
            else launch_player(&playing_title);
            return;
        }
        if(command==4){if(auto_active){restart_position=auto_resume=position;auto_index++;auto_next();}else snprintf(status,sizeof(status),"NO ALTERNATE SERVER FOR LOCAL VIDEO");return;}
        if(tv_active&&(command==5||command==6)){auto_active=0;tv_in_break=tv_break_due=0;tv_attempts=0;tv_next_at=0;return;}
        auto_active=0;
        if(browse.mode!=4&&browse.mode!=8){snprintf(status,sizeof(status),"EPISODE NAVIGATION ONLY AVAILABLE IN SERIES");return;}
        int direction=command==5?-1:1,next=selection+direction;
        if(next>=0&&next<total){selection=next;play();return;}
        int page=browse.page+direction;
        if(page>=1&&page<=listing_pages){play_after_load=1;next_catalog_page(direction);return;}
        snprintf(status,sizeof(status),"NO MORE EPISODES IN THIS SEASON");return;
    }
    if(tv_in_break){tv_in_break=0;tv_attempts=0;tv_next_at=0;if(cancelled)tv_active=0;snprintf(status,sizeof(status),"BACK TO TV / SELECTING NEXT PROGRAM");return;}
    if(WIFEXITED(code)&&WEXITSTATUS(code)==44){auto_active=0;snprintf(status,sizeof(status),"SERVER CANNOT SEEK - SELECT TITLE TO RESTART");return;}
    if(auto_active&&!cancelled&&(!WIFEXITED(code)||WEXITSTATUS(code))){auto_index++;auto_next();return;}
    auto_active=0;
    if(tv_active&&!cancelled&&WIFEXITED(code)&&!WEXITSTATUS(code)){
        tv_progress_save(auto_title.id,auto_title.season,auto_title.episode);tv_attempts=0;tv_next_at=0;tv_break_due=tv_breaks;
        fprintf(stderr,"TV completed %d S%d E%d\n",auto_title.id,auto_title.season,auto_title.episode);
    }
    snprintf(status,sizeof(status),cancelled?"PLAYBACK STOPPED":WIFEXITED(code)&&!WEXITSTATUS(code)?"PLAYBACK FINISHED":"PLAYER STOPPED - SEE PLAYER.LOG");
}
static void action(SDL_Keycode key) {
    if(bios_archive_confirm){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F2){bios_archive_confirm=0;return;}
        if(key==SDLK_RETURN){
            bios_archive_confirm=0;int result=bios_archive_download(&bios_archive_title);
            snprintf(status,sizeof(status),"%s",result==0?"ARCHIVE BIOS INSTALLED AND VERIFIED":
                result==2?"INVALID BIOS SOURCE":result==4?"BIOS EXISTS: NOT REPLACED":
                result==5?"ARCHIVE DOWNLOAD FAILED":result==6?"SHA1 MISMATCH: DOWNLOAD DISCARDED":result==7?"ZIP HAS NO VALID MATCHING BIOS":"BIOS INSTALL ERROR");
        }
        return;
    }
    if(update_pid)return;
    if(player_pid) {
        if(!player_menu){
            if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE)tv_active=0;
            if(key==SDLK_i||key==SDLK_y||key==SDLK_v)kill(player_pid,SIGUSR1);
            if((key==SDLK_ESCAPE||key==SDLK_BACKSPACE)&&!stopping_player){auto_active=0;kill(player_pid,SIGTERM);stopping_player=1;stop_time=SDL_GetTicks();}
            return;
        }
        char command=key==SDLK_i?'m':key==SDLK_ESCAPE||key==SDLK_BACKSPACE?'b':key==SDLK_UP?'u':key==SDLK_DOWN?'d':key==SDLK_LEFT?'l':key==SDLK_RIGHT?'r':key==SDLK_RETURN?'a':key==SDLK_y||key==SDLK_v?'v':0;
        if(command&&player_control>=0){ssize_t sent=write(player_control,&command,1);(void)sent;}
        return;
    }
    if(catalog_pid){
        if(key==SDLK_F2&&auto_active){
            kill(catalog_pid,SIGKILL);waitpid(catalog_pid,NULL,0);catalog_pid=0;
            if(pending.mode==6){auto_index++;auto_next();}
            else {auto_active=0;snprintf(status,sizeof(status),"SERVER DISCOVERY SKIPPED");}
            return;
        }
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F2){
            kill(catalog_pid,SIGKILL);waitpid(catalog_pid,NULL,0);catalog_pid=0;
            auto_active=0;tv_active=0;
            snprintf(status,sizeof(status),key==SDLK_F2?"LOAD SKIPPED":"LOAD CANCELLED");
        }
        return;
    }
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
    if(tv_schedule_on){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){tv_schedule_on=0;save_settings();return;}
        if(key==SDLK_UP&&tv_row>0)tv_row--;
        if(key==SDLK_DOWN&&tv_row<TV_BLOCKS-1)tv_row++;
        if(key==SDLK_RETURN)tv_column=!tv_column;
        int step=key==SDLK_LEFT?-1:1;
        if(key==SDLK_LEFT||key==SDLK_RIGHT){
            if(tv_column)tv_blocks[tv_row].genre=(tv_blocks[tv_row].genre+step+TV_GENRES)%TV_GENRES;
            else if(tv_row){int h=tv_blocks[tv_row].hour+step,maximum=tv_row+1<TV_BLOCKS?tv_blocks[tv_row+1].hour:24;if(h>tv_blocks[tv_row-1].hour&&h<maximum)tv_blocks[tv_row].hour=h;}
            save_settings();
        }return;
    }
    if(filters_on){
        if(key==SDLK_UP||key==SDLK_DOWN)filter_row=!filter_row;
        if(key==SDLK_LEFT||key==SDLK_RIGHT){int step=key==SDLK_LEFT?-1:1;
            if(filter_row)browse.season=(browse.season+step+BROWSE_ORDERS)%BROWSE_ORDERS;
            else do {browse.id=(browse.id+step+BROWSE_GENRES)%BROWSE_GENRES;}while(browse.mode==2&&series_genres[browse.id]<0);
        }
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F2){filters_on=0;browse.id=filter_original_genre;browse.season=filter_original_order;return;}
        if(key==SDLK_RETURN){filters_on=0;Browse next=browse;next.page=1;next.query[0]=0;request_catalog(next,0,0);}
        return;
    }
    if(!settings_on&&!about&&browse.mode==14&&(key==SDLK_y||key==SDLK_SLASH)){Browse next={21,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!settings_on&&!about&&browse.mode==14&&key==SDLK_F3){Browse next={15,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!settings_on&&!about&&browse.mode==14&&key==SDLK_F6){Browse next=browse;next.page=1;next.id=1;request_catalog(next,0,0);return;}
    if(!settings_on&&!about&&(key==SDLK_F6||key==SDLK_y)&&(browse.mode==1||browse.mode==2||browse.mode==7)){filters_on=1;filter_row=0;filter_original_genre=browse.id;filter_original_order=browse.season;return;}
    if(bios_on){
        if(bios_confirm){
            if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F2){bios_confirm=0;return;}
            if(key==SDLK_RETURN){
                bios_confirm=0;
                int result=bios_download();
                snprintf(status,sizeof(status),"%s",result==0?"BIOS INSTALLED AND VERIFIED":result==2?"NO VERIFIED SOURCE CONFIGURED":result==4?"BIOS PRESENT - NOT OVERWRITTEN":result==5?"BIOS DOWNLOAD FAILED":result==6?"SHA256 FAILED - FILE DISCARDED":"BIOS INSTALL FAILED");
            }
            return;
        }
        if(key==SDLK_UP&&bios_selected>0)bios_selected--;
        if(key==SDLK_DOWN&&bios_selected<4)bios_selected++;
        if(key==SDLK_F3){
            bios_on=0;Browse next={23,1,bios_selected+1,0,0,0,"",""};
            request_catalog(next,1,0);return;
        }
        if(key==SDLK_y){
            if(bios_present(bios_files[bios_selected]))snprintf(status,sizeof(status),"BIOS ALREADY PRESENT");
            else if(access("bios-sources.tsv",R_OK))snprintf(status,sizeof(status),"NO SOURCES: ADD BIOS-SOURCES.TSV");
            else {bios_confirm=1;snprintf(status,sizeof(status),"ONLY CONFIRM IF YOU HAVE DOWNLOAD RIGHTS");}
        }
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5||key==SDLK_F2)bios_on=0;
        return;
    }
    if(settings_on){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5){settings_on=0;save_settings();return;}
        if(key==SDLK_UP&&settings_row>0)settings_row--;
        if(key==SDLK_DOWN&&settings_row<10)settings_row++;
        int step=key==SDLK_LEFT?-1:1;
        if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN){
            switch(settings_row){
                case 0:quality_setting=(quality_setting+step+3)%3;break;
                case 1:buffer_setting=(buffer_setting+step+3)%3;break;
                case 2:disk_setting=(disk_setting+step+3)%3;break;
                case 3:coverflow=!coverflow;break;
                case 4:subtitle_setting=(subtitle_setting+step+4)%4;break;
                case 5:subtitle_scale=subtitle_scale==2?3:2;break;
                case 8:tv_breaks=!tv_breaks;break;
                case 9:monochrome_menu=!monochrome_menu;apply_menu_theme();break;
                case 10:bios_on=1;break;
                case 7:tv_schedule_on=1;tv_row=0;tv_column=1;break;
                case 6:subtitle_delay+=step;if(subtitle_delay>5)subtitle_delay=-5;if(subtitle_delay< -5)subtitle_delay=5;break;
            }save_settings();
        }return;
    }
    if(browse.mode==11&&!about&&(key==SDLK_RETURN||key==SDLK_SPACE)){tv_active=1;tv_attempts=0;tv_next_at=0;tv_break_due=tv_in_break=0;tv_tick();return;}
    if(about&&(key==SDLK_RETURN||key==SDLK_SPACE||key==SDLK_u)){start_update();return;}
    if(key==SDLK_u){start_update();return;}
    if(key==SDLK_F5){settings_on=1;about=0;return;}
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE) { if(about)about=0;else if(history_size)request_catalog(history[history_size-1],0,1);else if(browse.mode){Browse next={0,1,0,0,0,0,"",""};request_catalog(next,0,0);}else running=0; }
    else if(key==SDLK_F2){Browse next={browse.mode==0?1:browse.mode==1?2:browse.mode==2?7:browse.mode==7?9:browse.mode==9?11:browse.mode==11?14:0,1,0,0,0,0,"",""};history_size=0;request_catalog(next,0,0);}
    else if((key==SDLK_F3||key==SDLK_SLASH)&&(browse.mode==1||browse.mode==2||browse.mode==7||browse.mode==9||browse.mode==17||browse.mode==23)){search_on=1;search_key=0;strcpy(search_text,browse.query);SDL_StartTextInput();}
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
        else if(!strcmp(argv[i],"--library")&&i+1<argc){const char *v=argv[++i];initial_library=!strcmp(v,"movies")?1:!strcmp(v,"series")?2:!strcmp(v,"anime")?7:!strcmp(v,"archive")?9:!strcmp(v,"tv")?11:!strcmp(v,"games")?14:0;if(!initial_library)return 2;}
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
        if(SDL_WaitEventTimeout(&event,((stars_on&&!background)||flow_animating())?33:250)) do {
            dirty=1;
            if(event.type==SDL_QUIT)running=0;
            else if(event.type==SDL_KEYDOWN)action(event.key.keysym.sym);
            else if(event.type==SDL_TEXTINPUT&&search_on){for(const char *p=event.text.text;*p;p++){size_t n=strlen(search_text);if(n<64&&(isalnum((unsigned char)*p)||*p==' '||*p=='-')){search_text[n]=(char)toupper((unsigned char)*p);search_text[n+1]=0;}}}
            else if(event.type==SDL_CONTROLLERDEVICEADDED)add_pad(event.cdevice.which);
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERAXISMOTION){int v=event.caxis.value;if((event.caxis.axis<4&&(v>8000||v< -8000))||(event.caxis.axis>=4&&v>8000))controller_used();}
            else if(event.type==SDL_CONTROLLERBUTTONUP)controller_used();
            else if(event.type==SDL_CONTROLLERBUTTONDOWN) {
                /* Suppress duplicate controller button-down bursts during playback.
                 * Distinct buttons remain independent for normal navigation. */
                static Uint32 last_play_button[32];
                unsigned button=(unsigned)event.cbutton.button;
                Uint32 pressed_at=SDL_GetTicks();
                if(player_pid&&button<32&&last_play_button[button]&&
                   pressed_at-last_play_button[button]<120)continue;
                if(player_pid&&button<32)last_play_button[button]=pressed_at;
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
        prefetch_pages();controller_idle();tv_tick();
        if(player_pid){int code;pid_t p=waitpid(player_pid,&code,WNOHANG);
            if(p==player_pid){finish_player(code);dirty=1;}
            else if(stopping_player&&SDL_GetTicks()-stop_time>3000)kill(player_pid,SIGKILL);
            SDL_Delay(30);continue;
        }
        Uint32 now=SDL_GetTicks();
        if(dirty||(browse.mode==11&&now-last_draw>=1000)||(((stars_on&&!background)||flow_animating())&&now-last_draw>=33)||shot||frames){draw(now-start);last_draw=now;dirty=0;seen++;
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
    SDL_Quit();return game_exit?43:restart_shell?42:0;
}
