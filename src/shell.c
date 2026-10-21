#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <SDL_image.h>
#include <curl/curl.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <unistd.h>
#include <time.h>
#include "windows_runtime.h"
#include "windows_archive.h"
#include "font.h"
#include "video_layout.h"
#include "tv_schedule.h"
#include "tv_airing.h"
#include "tv_progress.h"
#include "tv_history.h"
#include "tv_play_history.h"
#include "browse_filters.h"
#include "game_metadata.h"
#include "child_process.h"
static int phone_dispatch(char *reply,size_t size);
#define FLXTR_PHONE_DISPATCH phone_dispatch
#include "api_setup.h"
#include "vendor/qrcodegen.c"
#include "wifi_pairing.h"
#ifdef _WIN32
#define IMG_Load(path) IMG_Load(flxtr_win_resource(path))
#define SDL_RWFromFile(path,mode) SDL_RWFromFile(flxtr_win_resource(path),mode)
#endif

static int accounts_on,party_entry,about_row;
static char rom_phone_path[241];
static uint8_t accounts_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(16)];
static uint8_t wifi_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
static int wifi_qr_valid;
static void accounts_open(int party){
    party_entry=party;
    if(party!=2)rom_phone_path[0]=0;
    char ip[INET_ADDRSTRLEN];uint8_t temp[sizeof(accounts_qr)];accounts_on=1;
    memset(accounts_qr,0,sizeof(accounts_qr));
    if(api_lan_ip(ip,sizeof(ip))||api_setup_start(ip,SDL_GetTicks())){
        api_setup_close();strcpy(api_setup.message,"NO LAN ADDRESS - CONNECT TO YOUR HOME NETWORK");return;
    }
    char qr_url[512];snprintf(qr_url,sizeof(qr_url),"%s%s%s",api_setup.url,party_entry==2?"phone#rom=":party_entry?"phone#party":"",party_entry==2?rom_phone_path:"");
    if(!qrcodegen_encodeText(qr_url,temp,accounts_qr,qrcodegen_Ecc_MEDIUM,1,16,qrcodegen_Mask_AUTO,true)){
        api_setup_close();strcpy(api_setup.message,"COULD NOT CREATE QR CODE - PRESS A TO RETRY");
    }
    char payload[800];uint8_t wifi_temp[sizeof(wifi_qr)];
    wifi_qr_valid=!wifi_payload(payload,sizeof(payload))&&qrcodegen_encodeText(payload,wifi_temp,wifi_qr,qrcodegen_Ecc_LOW,1,8,qrcodegen_Mask_AUTO,false);
    memset(payload,0,sizeof(payload));memset(wifi_temp,0,sizeof(wifi_temp));
}

#ifndef FLXTR_VERSION
#define FLXTR_VERSION "development"
#endif
#define W 960
#define H 540
#define MAX_TITLES 128
#define FAVORITES_MODE 25
#define VISIBLE 6
#define STARS 56
typedef struct { char title[80],meta[96],poster[192],url[2048],subs_eng[1024],subs_nor[1024],subs_spa[1024],kind[16];int id,season,episode,height; } Title;
/* Every ROM can select any locally installed Libretro core. */
#define ROM_CORE_LIMIT 64
static int rom_core_menu,rom_core_id,rom_core_selected,rom_core_count,rom_core_first;
static char rom_core_title[80],rom_core_names[ROM_CORE_LIMIT][96];
static void rom_core_scan(const char *dir){
    DIR *d=opendir(dir);
    if(!d)return;
    struct dirent *e;
    while((e=readdir(d))&&rom_core_count<ROM_CORE_LIMIT){
        size_t len=strlen(e->d_name);
        const char suffix[]="_libretro" FLXTR_CORE_EXT;
        if(len<=sizeof(suffix)-1||strcmp(e->d_name+len-(sizeof(suffix)-1),suffix))continue;
        len-=sizeof(suffix)-1;
        if(!len||len>=sizeof(rom_core_names[0]))continue;
        char name[96];memcpy(name,e->d_name,len);name[len]=0;
        int duplicate=0;
        for(int i=0;i<rom_core_count;i++)if(!strcmp(rom_core_names[i],name))duplicate=1;
        if(duplicate)continue;
        char path[256];
        if(snprintf(path,sizeof(path),"%s/%s",dir,e->d_name)>=(int)sizeof(path)||access(path,R_OK))continue;
        snprintf(rom_core_names[rom_core_count++],sizeof(rom_core_names[0]),"%s",name);
    }
    closedir(d);
}
static void rom_core_open(const Title *t){
    if(strcmp(t->kind,"game"))return;
    rom_core_id=t->id;
    snprintf(rom_core_title,sizeof(rom_core_title),"%s",t->title);
    rom_core_count=rom_core_selected=rom_core_first=0;
    rom_core_scan("cores");
    const char *retroarch_home=getenv("FLXTR_RETROARCH_HOME");
    if(retroarch_home&&*retroarch_home){char path[PATH_MAX];snprintf(path,sizeof(path),"%s/cores",retroarch_home);rom_core_scan(path);}
    rom_core_scan("/home/apps/retroarch/cores");
    rom_core_scan("/home/apps/retroarch/.home/.config/retroarch/cores");
    /* Open on the previously chosen core, if any. */
    FILE *f=fopen("game-cores.tsv","r");
    if(f){char line[256],title[80],core[96];int id;
        while(fgets(line,sizeof(line),f)){
            if(sscanf(line,"%d\t%79[^\t]\t%95[^\n]",&id,title,core)!=3)continue;
            if(id==rom_core_id)
                for(int i=0;i<rom_core_count;i++)
                    if(!strcmp(core,rom_core_names[i]))rom_core_selected=i;
        }
        fclose(f);
    }
    rom_core_first=rom_core_selected>3?rom_core_selected-3:0;
    if(rom_core_count)rom_core_menu=1;
}
static void rom_core_save(void){
    FILE *in=fopen("game-cores.tsv","r"),*out=fopen("game-cores.tsv.next","w");
    if(!out){if(in)fclose(in);return;}
    if(in){char line[256],title[80],core[96];int id;
        while(fgets(line,sizeof(line),in)){
            if(sscanf(line,"%d\t%79[^\t]\t%95[^\n]",&id,title,core)!=3)continue;
            if(id!=rom_core_id)
                fprintf(out,"%d\t%s\t%s\n",id,title,core);
        }fclose(in);
    }
    fprintf(out,"%d\t%s\t%s\n",rom_core_id,rom_core_title,rom_core_names[rom_core_selected]);
    if(fclose(out)==0)rename("game-cores.tsv.next","game-cores.tsv");
}
static Title titles[MAX_TITLES];
static int count, selection, ready_only, about, stars_on=1, running=1;
static int visible[MAX_TITLES], total, cached_page=-1;
static SDL_Renderer *renderer;
static SDL_Window *window;
static SDL_Texture *posters[VISIBLE],*background;
static SDL_GameController *pads[4];
static pid_t player_pid, update_pid, samba_pid;
static int restart_shell,game_exit, coverflow=1;
#ifdef _WIN32
typedef FlxtrPipeHandle PlayerPipe;
#define player_pipe_close flxtr_pipe_close
#define player_pipe_write flxtr_pipe_write
#else
typedef int PlayerPipe;
#define player_pipe_close close
#define player_pipe_write write
#endif
static PlayerPipe player_control=-1;
static int play_after_load,player_menu;
static Title playing_title,bios_archive_title;
static double restart_position,auto_resume;
static TvBlock tv_blocks[TV_BLOCKS];
static int tv_active,tv_attempts,tv_schedule_on,tv_row,tv_column,tv_quality_retry,kiss_1080_pass;
#include "game_output.h"
static int game_output=GAME_OUTPUT_DEFAULT;
static int game_autostate,tv_breaks=1,tv_break_due,tv_in_break,tv_prepared,filters_on,filter_row,filter_original_genre,filter_original_order;
static Uint32 tv_next_at;
static void tv_candidate(void);
static void tv_tick(void);
static void tv_begin_prepared(void);
static void bookmark_path(const Title *t,char *path,size_t capacity);
static void tv_history_prepare(const Title *t);
static void tv_cancel_prepared(void);
static void tv_stage_source(const Title *source);
static int tv_joining,tv_preloading;
static long long tv_air_start,tv_last_block;
static Title tv_next_title,tv_last_title;
static pid_t tv_buffer_pid;
static PlayerPipe tv_buffer_control=-1,tv_gate=-1;
static int tv_rating_weight=5,tv_popularity_weight=2,tv_variety_weight=4;
static int series_autoplay;
enum {MENU_THEMES=6};
#if !defined(FLXTR_DESKTOP)||defined(FLXTR_ADAPTIVE_TEST)
#define QUALITY_CHOICES 4
#define DEFAULT_QUALITY 3
#else
#define QUALITY_CHOICES 3
#define DEFAULT_QUALITY 1
#endif
static int adaptive_height=720,adaptive_blocked,adaptive_previous=720,adaptive_pending;
static double adaptive_position;
static char adaptive_title_key[16];
static int quality_height(void);
static int adaptive_lookup_fallback(void);
#include "settings_tabs.h"
static int music_autoplay=1;
static int bios_archive_confirm,bios_selected,bios_confirm,bios_on,monochrome_menu,menu_theme,settings_on,quality_setting=DEFAULT_QUALITY,buffer_setting=0,disk_setting=1,subtitle_setting=1,subtitle_scale=2,subtitle_delay;
static const int qualities[]={480,720,1080,720},buffer_seconds[]={15,15,20,25,30,45,60,80,128};
#ifdef FLXTR_DESKTOP
static const int disk_megabytes[]={64,128,256};
#else
static const int disk_megabytes[]={16,32,64};
#endif
static int quality_height(void){return quality_setting==3?adaptive_height:qualities[quality_setting];}
static const char *const subtitle_languages[]={"off","auto","eng","nor","spa"};
static void subtitle_route_log(const char *route,const Title *t){
    FILE *f=fopen("subtitle-debug.log","a");if(!f)return;
    fprintf(f,"%lu shell route=%s kind=%s video=%s english=%s norwegian=%s\n",
        (unsigned long)time(NULL),route,t->kind,t->url[0]?"yes":"no",
        t->subs_eng[0]?"yes":"no",t->subs_nor[0]?"yes":"no");
    fclose(f);
}

/* Launcher checks this marker before attempting release updates. */
static int auto_updates_enabled(void){return access(".no-auto-update",F_OK)!=0;}
static int set_auto_updates(int enabled){
    if(enabled)return unlink(".no-auto-update")==0||errno==ENOENT;
    int fd=open(".no-auto-update",O_WRONLY|O_CREAT,0600);
    if(fd<0)return 0;
    return close(fd)==0;
}
static void save_settings(void){
    FILE *f=fopen("settings.cfg.next","w");if(!f)return;
    fprintf(f,"series_autoplay=%d\n",series_autoplay);
    fprintf(f,"quality=%d\nbuffer=%d\ndisk=%d\ncoverflow=%d\nsubtitles=%d\nscale=%d\ndelay=%d\n",quality_setting,buffer_setting,disk_setting,coverflow,subtitle_setting,subtitle_scale,subtitle_delay);
    fprintf(f,"game_output=%d\nmusic_autoplay=%d\n",game_output,music_autoplay);
    fprintf(f,"tv_breaks=%d\nmenu_monochrome=%d\ngame_autostate=%d\n",tv_breaks,monochrome_menu,game_autostate);
    fprintf(f,"menu_theme=%d\n",menu_theme);
    fprintf(f,"tv_rating=%d\ntv_popularity=%d\ntv_variety=%d\n",tv_rating_weight,tv_popularity_weight,tv_variety_weight);
    FILE *g=fopen("tv-gambits.cfg.next","w");
    if(g){fprintf(g,"rating=%d\npopularity=%d\nvariety=%d\n",tv_rating_weight,tv_popularity_weight,tv_variety_weight);
        if(!fclose(g))rename("tv-gambits.cfg.next","tv-gambits.cfg");}
    fprintf(f,"tv_schedule_version=2\n");
    for(int i=0;i<TV_BLOCKS;i++)fprintf(f,"tv_hour_%d=%d\ntv_genre_%d=%d\n",i,tv_blocks[i].hour,i,tv_blocks[i].genre);
    for(int i=0;i<TV_BLOCKS;i++)fprintf(f,"tv_marathon_%d=%d\n",i,tv_blocks[i].marathon);
    if(fclose(f)==0)rename("settings.cfg.next","settings.cfg");
}
static void load_settings(void){
    memcpy(tv_blocks,tv_defaults,sizeof(tv_blocks));
    FILE *f=fopen("settings.cfg","r");if(!f)return;char line[80],key[32];int value;
    int theme_found=0,old_tv_schedule=0,schedule_version=0;
    while(fgets(line,sizeof(line),f))if(sscanf(line,"%31[^=]=%d",key,&value)==2){
        if(!strcmp(key,"tv_schedule_version"))schedule_version=value;
        else if(!strcmp(key,"tv_rating")&&value>=1&&value<=5)tv_rating_weight=value;
        else if(!strcmp(key,"tv_popularity")&&value>=0&&value<=5)tv_popularity_weight=value;
        else if(!strcmp(key,"tv_variety")&&value>=0&&value<=5)tv_variety_weight=value;
        else if(!strcmp(key,"game_output")&&game_output_valid(value))game_output=value;
        else if(!strcmp(key,"music_autoplay")&&(value==0||value==1))music_autoplay=value;
        else if(!strcmp(key,"game_autostate")&&(value==0||value==1))game_autostate=value;
        else if(!strcmp(key,"tv_breaks")&&(value==0||value==1))tv_breaks=value;
        else if(!strcmp(key,"menu_monochrome")&&(value==0||value==1))monochrome_menu=value;
        else if(!strcmp(key,"menu_theme")&&value>=0&&value<MENU_THEMES){menu_theme=value;theme_found=1;}
        else if(!strcmp(key,"quality")&&value>=0&&value<QUALITY_CHOICES)quality_setting=value;
        else if(!strcmp(key,"buffer")&&value>=0&&value<9)buffer_setting=value;
        else if(!strcmp(key,"disk")&&value>=0&&value<3)disk_setting=value;
        else if(!strcmp(key,"coverflow")&&(value==0||value==1))coverflow=value;
        else if(!strcmp(key,"series_autoplay")&&(value==0||value==1))series_autoplay=value;
        else if(!strcmp(key,"subtitles")&&value>=0&&value<5)subtitle_setting=value;
        else if(!strcmp(key,"scale")&&value>=2&&value<=3)subtitle_scale=value;
        else if(!strcmp(key,"delay")&&value>=-5&&value<=5)subtitle_delay=value;
        else if(!strncmp(key,"tv_marathon_",12)&&strlen(key)==13&&key[12]>='0'&&key[12]<'0'+TV_BLOCKS&&(value==0||value==1))tv_blocks[key[12]-'0'].marathon=value;
        else if(!strncmp(key,"tv_hour_",8)&&strlen(key)==9&&key[8]>='0'&&key[8]<'0'+TV_BLOCKS){tv_blocks[key[8]-'0'].hour=value;if(key[8]>='7')old_tv_schedule=1;}
        else if(!strncmp(key,"tv_genre_",9)&&strlen(key)==10&&key[9]>='0'&&key[9]<'0'+TV_BLOCKS)tv_blocks[key[9]-'0'].genre=value;
    }fclose(f);
    if(schedule_version<2){
        const int hours[]={0,6,9,11,13,15,17,19,21,23},genres[]={3,4,1,9,8,2,5,7,6,0};int stock=1;
        for(int i=0;i<TV_BLOCKS;i++)if(tv_blocks[i].hour!=hours[i]||tv_blocks[i].genre!=genres[i])stock=0;
        if(stock)memcpy(tv_blocks,tv_defaults,sizeof(tv_blocks));
    }
    if(!theme_found)menu_theme=monochrome_menu?1:0;
    monochrome_menu=menu_theme==1;
    if(!old_tv_schedule){
        /* Upgrade old seven-slot schedules without discarding custom genres.
         * Insert three new hourly boundaries into the widest existing gaps. */
        TvBlock upgraded[TV_BLOCKS];
        for(int i=0;i<7;i++)upgraded[i]=tv_blocks[i];
        int n=7;
        for(int extra=0;extra<3;extra++){
            int best=-1,width=1;
            for(int i=0;i<n;i++){
                int end=i+1<n?upgraded[i+1].hour:24;
                if(end-upgraded[i].hour>width){width=end-upgraded[i].hour;best=i;}
            }
            if(best<0)break;
            int hour=upgraded[best].hour+width/2;
            memmove(&upgraded[best+2],&upgraded[best+1],(size_t)(n-best-1)*sizeof(TvBlock));
            upgraded[best+1]=(TvBlock){hour,upgraded[best].genre,upgraded[best].marathon};
            n++;
        }
        if(n==TV_BLOCKS)memcpy(tv_blocks,upgraded,sizeof(tv_blocks));
    }
    if(!tv_valid(tv_blocks))memcpy(tv_blocks,tv_defaults,sizeof(tv_blocks));
}
static float flow_position;
static int stopping_player;
static int viewing=VIEW_FIT;
static Uint32 stop_time;
typedef struct {int mode,page,id,season,episode,selected;char query[2048],name[80];} Browse;
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
static int download_background;
static void music_video_pause(int resume);
static void launch_player(const Title *t);
static void play(void);
static void auto_next(void);
static const char *local_catalog="catalog.tsv";
static const char *const browse_labels[]={"LOCAL","MOVIES","SERIES","SEASONS","EPISODES","SERVERS","QUALITY","KISSANIME","ANIME EPISODES","INTERNET ARCHIVE","VIDEO FILES","TV MODE","TV PICK","COMMERCIAL BREAK","GAMES","GET GAMES","IMPORTING GAME","ARCHIVE TITLES","ROM FILES","DOWNLOADING ROM","VERIFIED HOMEBREW","ROM FOLDERS","CONSOLE GAMES","BIOS ARCHIVE SEARCH","BIOS FILES","FAVORITES","ZIP CONTENTS","EXTRACTING ROM","DOWNLOADS","DELETING ZIP","ARCHIVE FOLDERS","ARCHIVE COLLECTION","ROMSGAMES PLATFORMS","ROMSGAMES TITLES","NEXT EPISODE","LIBRETRO CORES","INSTALLING CORE"};
static const char *const browse_kinds[]={"","movie","tv","season","episode","source","quality","kiss","kiss-episodes","archive","archive-files","","tv-pick","tv-break","games","games-platforms","games-download","games-archive-search","games-archive-files","games-file-download","games-archive","games-folders","games-system","games-bios-search","games-bios-files","favorites","games-zip-list","games-zip-extract","games-downloads-list","games-downloads-delete","archive-folders","archive-category","romsgames-platforms","romsgames","series-adjacent","core-list","core-install"};
static int search_on,search_key;
static int favorite_remove_confirm;
static char search_text[65];
static const char search_keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -<>";
/* Persistent index of distinct titles encountered across every catalog page.
 * Append only new titles; no old entries are evicted when browsing. */
static char (*search_dict)[256];
static size_t search_dict_count,search_dict_capacity;
static int search_dict_loading;
static void search_dict_remember(const char *title){
    if(!title||!*title||strchr(title,'\n')||strchr(title,'\r')||strchr(title,'\t'))return;
    size_t len=strlen(title);if(!len||len>=256)return;
    for(size_t i=0;i<search_dict_count;i++)if(!strcasecmp(search_dict[i],title))return;
    if(search_dict_count==search_dict_capacity){
        size_t capacity=search_dict_capacity?search_dict_capacity*2:256;
        if(capacity>1000000)return;
        void *grown=realloc(search_dict,capacity*sizeof(search_dict[0]));
        if(!grown)return;
        search_dict=grown;search_dict_capacity=capacity;
    }
    snprintf(search_dict[search_dict_count++],256,"%s",title);
    if(!search_dict_loading){
        FILE *f=fopen("search-titles.txt","a");
        if(f){fprintf(f,"%s\n",title);fclose(f);}
    }
}
static void search_dict_load(void){
    FILE *f=fopen("search-titles.txt","r");if(!f)return;
    search_dict_loading=1;
    char line[512];
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')&&!feof(f)){int c;while((c=fgetc(f))!=EOF&&c!='\n'){}continue;}
        line[strcspn(line,"\r\n")]=0;search_dict_remember(line);
    }
    fclose(f);search_dict_loading=0;
}
static const char *search_completion(void){
    size_t n=strlen(search_text);
    if(n<4)return NULL;
    for(size_t i=search_dict_count;i>0;i--)
        if(!strncasecmp(search_dict[i-1],search_text,n)&&strlen(search_dict[i-1])>n)
            return search_dict[i-1];
    return NULL;
}


static char status[100]="SELECT A TITLE / Y GENRE AND MOST POPULAR";
static unsigned char star_x[STARS], star_y[STARS], star_speed[STARS];
static SDL_Color green={116,255,132,255}, dim={66,126,77,255}, white={212,226,214,255};

static int keyboard_used=
#ifdef FLXTR_DESKTOP
1;
#else
0;
#endif
static const char *ui_hint(const char *controller,const char *keyboard){return keyboard_used?keyboard:controller;}
#include "desktop_theme.h"
static void apply_menu_theme(void) {
    theme_canvas=(SDL_Color){0,0,0,255};theme_panel=(SDL_Color){255,255,255,255};
    if(menu_theme>=3){
        green=menu_theme==4?(SDL_Color){255,185,66,255}:(SDL_Color){101,201,255,255};
        white=menu_theme==4?(SDL_Color){255,226,173,255}:(SDL_Color){231,237,245,255};
        dim=menu_theme==4?(SDL_Color){173,129,64,255}:(SDL_Color){139,155,175,255};
        theme_canvas=menu_theme==4?(SDL_Color){16,10,2,255}:(SDL_Color){9,17,31,255};
        theme_panel=menu_theme==4?(SDL_Color){37,25,8,255}:(SDL_Color){17,30,51,255};
        if(menu_theme==5)theme_custom();
    }else if(menu_theme==2){
        theme_panel=(SDL_Color){12,5,10,255};
        green=(SDL_Color){243,44,66,255};
        dim=(SDL_Color){181,116,125,255};
        white=(SDL_Color){248,235,235,255};
    }else if(menu_theme==0&&background){
        green=(SDL_Color){18,91,56,255};
        dim=(SDL_Color){62,74,72,255};
        white=(SDL_Color){24,32,30,255};
    }else{
        green=(SDL_Color){116,255,132,255};
        dim=(SDL_Color){66,126,77,255};
        white=(SDL_Color){212,226,214,255};
    }
}
static void load_menu_background(void){
    if(!renderer)return;
    SDL_DestroyTexture(background);background=NULL;
    if(menu_theme>=3){
        if(menu_theme==5){theme_create_custom();struct stat st;
            if(!stat("themes/custom/background.png",&st)&&st.st_size<=8*1024*1024){
                SDL_Surface *custom=IMG_Load("themes/custom/background.png");
                if(custom){if(custom->w<=4096&&custom->h<=4096)background=SDL_CreateTextureFromSurface(renderer,custom);SDL_FreeSurface(custom);}
            }
        }
        apply_menu_theme();return;
    }
    if(menu_theme==1){apply_menu_theme();return;}
    const char *path=menu_theme==2?
        "assets/backgrounds/romantic_crimson_skulls.png":
        "assets/white-metal-droplets.bmp";
    SDL_Surface *surface=menu_theme==2?IMG_Load(path):SDL_LoadBMP(path);
    if(surface){
        background=SDL_CreateTextureFromSurface(renderer,surface);
        SDL_FreeSurface(surface);
    }
    if(!background)fprintf(stderr,"Menu background unavailable: %s (%s)\n",path,menu_theme==2?IMG_GetError():SDL_GetError());
    apply_menu_theme();
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
#ifdef _WIN32
    const char *args[]={"curl","-fLsS","--proto","=https","--proto-redir","=https","--connect-timeout","10","--max-time","90","--max-filesize","16777216","--cacert",flxtr_win_resource("certs/cacert.pem"),"-o",tmp,url,NULL};
    if(flxtr_win_run("curl",args,"NUL","bios.log")){unlink(tmp);return 5;}
    if(!flxtr_digest_matches(tmp,sha,EVP_sha256())){unlink(tmp);return 6;}
#else
    pid_t child=shell_fork();if(child<0)return 3;
    if(!child){execlp("curl","curl","-fLsS","--proto","=https","--proto-redir","=https","--connect-timeout","10","--max-time","90","--max-filesize","16777216","--cacert","certs/cacert.pem","-o",tmp,url,(char*)NULL);_exit(127);}
    int code=0;if(waitpid(child,&code,0)!=child||!WIFEXITED(code)||WEXITSTATUS(code)){unlink(tmp);return 5;}
    f=fopen("bios-check.txt","w");if(!f){unlink(tmp);return 3;}
    fprintf(f,"%s  %s\n",sha,tmp);fclose(f);
    child=shell_fork();if(child<0){unlink(tmp);unlink("bios-check.txt");return 3;}
    if(!child){int devnull=open("/dev/null",O_WRONLY);if(devnull>=0){dup2(devnull,STDOUT_FILENO);close(devnull);}execlp("sha256sum","sha256sum","-c","bios-check.txt",(char*)NULL);_exit(127);}
    code=0;int ok=waitpid(child,&code,0)==child&&WIFEXITED(code)&&WEXITSTATUS(code)==0;
    unlink("bios-check.txt");if(!ok){unlink(tmp);return 6;}
#endif
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
    if(!slash)return 2;
    int decoded_len=0;
    char *decoded=curl_easy_unescape(NULL,slash+1,0,&decoded_len);
    if(!decoded)return 2;
    const char *base=strrchr(decoded,'/');
    base=base?base+1:decoded;
    const char *extension=strrchr(base,'.');
    int valid=decoded_len>0&&(size_t)decoded_len==strlen(decoded)&&
        !strstr(decoded,"..")&&!strchr(decoded,'\\')&&
        (is_zip?(item->id!=1&&extension&&!strcasecmp(extension,".zip")):!strcasecmp(base,name));
    curl_free(decoded);
    if(!valid)return 2;
    if(mkdir("system",0700)<0&&errno!=EEXIST)return 3;
    char dest[96],temp[128];snprintf(dest,sizeof(dest),"system/%s",name);snprintf(temp,sizeof(temp),"system/.%s.partial",name);
    if(access(dest,F_OK)==0)return 4;
    unlink(temp);
    FILE *output=fopen(temp,"wb");if(!output)return 3;
    CURL *curl=curl_easy_init();if(!curl){fclose(output);unlink(temp);return 3;}
    char curl_error[CURL_ERROR_SIZE]={0};
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,curl_error);
    curl_easy_setopt(curl,CURLOPT_URL,item->url);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,4L);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,output);
    curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,120L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,45L);
    curl_easy_setopt(curl,CURLOPT_CAINFO,FLXTR_CA_BUNDLE);
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
        fprintf(stderr,"BIOS Archive transfer failed: HTTP %ld / curl %d / %s / URL %s\\n",http,(int)result,curl_error[0]?curl_error:curl_easy_strerror(result),item->url);
        unlink(temp);return 5;
    }
#ifdef _WIN32
    int verified=flxtr_digest_matches(temp,item->meta+5,EVP_sha1());
#else
    pid_t pid;int code=0;
    FILE *f=fopen("bios-archive-check.txt","w");if(!f){unlink(temp);return 3;}
    fprintf(f,"%s  %s\n",item->meta+5,temp);fclose(f);
    pid=shell_fork();if(pid<0){unlink(temp);unlink("bios-archive-check.txt");return 3;}
    if(!pid){
        int fd=open("/dev/null",O_WRONLY);if(fd>=0){dup2(fd,STDOUT_FILENO);close(fd);}
        execlp("sha1sum","sha1sum","-c","bios-archive-check.txt",(char*)NULL);_exit(127);
    }
    code=0;int verified=waitpid(pid,&code,0)==pid&&WIFEXITED(code)&&WEXITSTATUS(code)==0;
    unlink("bios-archive-check.txt");
#endif
    if(!verified){unlink(temp);return 6;}
    if(is_zip){
        /* BusyBox unzip supports -l, not Info-ZIP's -Z -1.
         * Parse only properly formatted listing rows and extract one exact member. */
#ifdef _WIN32
        FILE *listing=flxtr_archive_listing(temp);char line[512],member[512]="";
#else
        int pipes[2];if(pipe(pipes)<0){unlink(temp);return 3;}
        pid=shell_fork();if(pid<0){close(pipes[0]);close(pipes[1]);unlink(temp);return 3;}
        if(!pid){
            dup2(pipes[1],STDOUT_FILENO);close(pipes[0]);close(pipes[1]);
            execlp("unzip","unzip","-l",temp,(char*)NULL);_exit(127);
        }
        close(pipes[1]);
        FILE *listing=fdopen(pipes[0],"r");char line[512],member[512]="";
#endif
        if(listing){
            while(fgets(line,sizeof(line),listing)){
                size_t n=strcspn(line,"\r\n");
                if(n>=sizeof(line)-1)continue;
                line[n]=0;
                unsigned long length=0;char date[24],clock[24];int offset=0;
                if(sscanf(line," %lu %23s %23s %n",&length,date,clock,&offset)!=3||offset<=0)continue;
                const char *path=line+offset;
                while(*path==' '||*path=='\t')path++;
                if(!*path||strlen(path)>=sizeof(member)||strstr(path,"..")||strchr(path,'\\'))continue;
                const char *base=strrchr(path,'/');base=base?base+1:path;
                if(!strcmp(base,name)&&length==((item->id==5)?16384UL:524288UL))
                    snprintf(member,sizeof(member),"%s",path);
            }
            fclose(listing);
        }
#ifndef _WIN32
        else close(pipes[0]);
        int listcode=0;waitpid(pid,&listcode,0);
#else
        int listcode=listing?0:256;
#endif
        if(!member[0]||!WIFEXITED(listcode)||WEXITSTATUS(listcode)!=0){unlink(temp);return 7;}
        char extracted[128];snprintf(extracted,sizeof(extracted),"system/.%s.extracted",name);
        unlink(extracted);
        int fd=open(extracted,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0){unlink(temp);return 3;}
#ifdef _WIN32
        int extractcode=flxtr_archive_extract(temp,member,fd,16777216)?256:0;close(fd);
#else
        pid=shell_fork();if(pid<0){close(fd);unlink(extracted);unlink(temp);return 3;}
        if(!pid){
            dup2(fd,STDOUT_FILENO);close(fd);
            execlp("unzip","unzip","-p",temp,member,(char*)NULL);_exit(127);
        }
        close(fd);int extractcode=0;waitpid(pid,&extractcode,0);
#endif
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
/* Narrow right panel: six-pixel glyphs and fourteen-pixel baselines.
 * Unlike wrap() this is designed to fit the 224-pixel detail column. */
static void wrap_detail(int x,int y,const char *str,int lines,SDL_Color ink){
    const int cols=35;
    for(int row=0;*str&&row<lines;row++){
        char line[40];size_t n=strlen(str);
        if(n>(size_t)cols){n=cols;while(n>1&&str[n]!=' ')n--;}
        if(!n)break;
        memcpy(line,str,n);line[n]=0;
        text(x,y+row*15,line,1,ink,cols);
        str+=n;while(*str==' ')str++;
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
    window=SDL_CreateWindow("FlXtR",dummy?SDL_WINDOWPOS_CENTERED:0,dummy?SDL_WINDOWPOS_CENTERED:0,width,height,SDL_WINDOW_SHOWN | (dummy?0:SDL_WINDOW_FULLSCREEN_DESKTOP));
    if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){close_ui();return -1;}
    SDL_RenderSetLogicalSize(renderer,W,H);SDL_ShowCursor(SDL_DISABLE);
    int output_w=0,output_h=0;SDL_GetRendererOutputSize(renderer,&output_w,&output_h);
    fprintf(stderr,"Shell display: %s window=%dx%d output=%dx%d logical=%dx%d\n",driver?driver:"unknown",width,height,output_w,output_h,W,H);
    green=(SDL_Color){116,255,132,255};dim=(SDL_Color){66,126,77,255};white=(SDL_Color){212,226,214,255};
    load_menu_background();
    return 0;
}
static void filter(void) {
    total=0; for(int i=0;i<count;i++)if(!ready_only||titles[i].url[0])visible[total++]=i;
    selection=0; clear_posters();
}
static int read_catalog(const char *path) {
    FILE *f=fopen(path,"r"); char line[6000];
    if(!f) { snprintf(status,sizeof(status),"CATALOG NOT FOUND - ADD CATALOG.TSV");return 0; }
    while(fgets(line,sizeof(line),f) && count<MAX_TITLES) {
        char *fields[12],*p=line; int nf=0;
        if(line[0]=='#'&&!strchr(line,'\t')){int pages,items;if(sscanf(line,"# pages=%d total=%d",&pages,&items)==2&&pages>0&&pages<=2000&&items>=0){listing_pages=pages;listing_total=items;}continue;}
        if(line[0]=='\n')continue;
        if(!strchr(line,'\n')&&!feof(f)) { int ch; while((ch=fgetc(f))!=EOF&&ch!='\n'){} continue; }
        line[strcspn(line,"\r\n")]=0;
        while(nf<12){fields[nf++]=p;char *tab=strchr(p,'\t');if(!tab||nf==12)break;*tab=0;p=tab+1;}
        if((nf!=4&&nf!=8&&nf!=9&&nf!=11&&nf!=12)||!fields[0][0]||strchr(fields[nf-1],'\t'))continue;
        Title *t=&titles[count];
        if(strlen(fields[0])>=sizeof(t->title)||strlen(fields[1])>=sizeof(t->meta)||strlen(fields[2])>=sizeof(t->poster)||strlen(fields[3])>=sizeof(t->url))continue;
        if(fields[3][0] && strncmp(fields[3],"https://",8) && strncmp(fields[3],"http://",7))continue;
        strcpy(t->title,fields[0]);strcpy(t->meta,fields[1]);strcpy(t->poster,fields[2]);strcpy(t->url,fields[3]);count++;
        if(nf>=8){snprintf(t->kind,sizeof(t->kind),"%s",fields[4]);t->id=atoi(fields[5]);t->season=atoi(fields[6]);t->episode=atoi(fields[7]);}
        if(nf>=9){t->height=atoi(fields[8]);if(t->height<0||t->height>1080)t->height=0;}
        if(nf>=11){if(strlen(fields[9])<sizeof(t->subs_eng))strcpy(t->subs_eng,fields[9]);
            if(strlen(fields[10])<sizeof(t->subs_nor))strcpy(t->subs_nor,fields[10]);}
        if(nf==12&&strlen(fields[11])<sizeof(t->subs_spa))strcpy(t->subs_spa,fields[11]);
    }
    fclose(f);
    /* Every parsed library title, including anime, episodes and games,
     * contributes to the persistent dictionary. Never discard older titles. */
    for(int i=0;i<count;i++)search_dict_remember(titles[i].title);
    filter();return count;
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
        mode==9||mode==10||mode==14||mode==15||mode==17||mode==18||mode==30||mode==31||
        mode==32||mode==33||mode==20||mode==21||mode==22||mode==23||mode==24;
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
    if(next.mode==FAVORITES_MODE){
        browse=next;history_size=0;count=0;selection=0;ready_only=0;
        listing_pages=1;listing_total=0;memset(titles,0,sizeof(titles));
        if(access("favorites.tsv",R_OK)==0){
            read_catalog("favorites.tsv");listing_total=total;
            /* Old title-only index rows can cross movie/TV/game boundaries.
             * Match exact identity and take the most recent valid cover, so
             * refreshed covers can replace earlier wrong associations. */
            FILE *idx=fopen("favorites-art-index.tsv","r");
            if(idx){
                char row[800];
                while(fgets(row,sizeof(row),idx)){
                    char *cols[6],*cursor=row;
                    int n=0;
                    while(n<6){
                        cols[n++]=cursor;
                        char *tab=strchr(cursor,'\t');
                        if(!tab)break;
                        *tab=0;cursor=tab+1;
                    }
                    if(n!=6)continue; /* Skip legacy title/path records. */
                    cols[5][strcspn(cols[5],"\r\n")]=0;
                    if(strncmp(cols[5],"game-art-cache/",15)&&
                       strncmp(cols[5],"favorites-art/",14))continue;
                    if(strstr(cols[5],".."))continue;
                    struct stat st;
                    if(stat(cols[5],&st)||!S_ISREG(st.st_mode)||st.st_size<54)continue;
                    for(int i=0;i<count;i++){
                        Title *t=&titles[i];
                        if(!strcmp(t->kind,cols[0])&&
                           t->id==atoi(cols[1])&&t->season==atoi(cols[2])&&
                           t->episode==atoi(cols[3])&&!strcmp(t->title,cols[4])&&
                           strlen(cols[5])<sizeof(t->poster))
                            strcpy(t->poster,cols[5]);
                    }
                }
                fclose(idx);
            }
        }
        else {filter();snprintf(status,sizeof(status),"FAVORITES EMPTY / L3 TO ADD");}
        return;
    }
    if(next.mode==11){browse=next;history_size=0;about=0;total=count=0;prefetch_left=0;snprintf(status,sizeof(status),"A START TV / SELECT SETTINGS / OSLO TIME");return;}
    if(catalog_pid)return;
    if(prefetch_pid){kill(prefetch_pid,SIGKILL);waitpid(prefetch_pid,NULL,0);prefetch_pid=0;}prefetch_left=0;
    if(!next.mode){
        browse=next;history_size=0;count=0;ready_only=0;listing_pages=1;memset(titles,0,sizeof(titles));read_catalog(local_catalog);return;
    }
    if(!auto_active&&library_cache_restore(next,push,pop)){if(play_after_load){play_after_load=0;if(total)play();}return;}
    if(access("./greenlink-catalog",X_OK)){play_after_load=0;snprintf(status,sizeof(status),"CATALOG WORKER NOT INSTALLED");return;}
    mkdir("catalog-cache",0700);
    for(int i=0;i<VISIBLE;i++){char path[80];snprintf(path,sizeof(path),"catalog-cache/poster-%d.next.bmp",i);unlink(path);}
    browse.selected=selection;pending=next;pending_push=push;pending_pop=pop;
    catalog_started=SDL_GetTicks();
    download_background=0;
    if(next.mode==19||next.mode==16)unlink("catalog-cache/download-progress");
    char pg[16],id[16],sn[16],ep[16];
    snprintf(pg,sizeof(pg),"%d",next.page);snprintf(id,sizeof(id),"%d",next.id);
    snprintf(sn,sizeof(sn),"%d",next.season);snprintf(ep,sizeof(ep),"%d",next.episode);
    const char *args[]={"greenlink-catalog",auto_active&&!strcmp(auto_title.kind,"kiss-episode")?
        (next.mode==5?"kiss-source":"kiss-quality"):browse_kinds[next.mode],
        pg,next.query,id,sn,ep,next.mode==6?next.query:"",NULL};
    catalog_pid=shell_spawn("./greenlink-catalog",args,"catalog-cache/result.tsv","catalog.log");
    if(catalog_pid<0){catalog_pid=0;snprintf(status,sizeof(status),"COULD NOT LOAD CATALOG");}
    else snprintf(status,sizeof(status),"LOADING %s - X SKIP / B CANCEL",browse_labels[next.mode]);
}
/* Automatic lookup uses its own small queue and leaves the visible library intact. */
static void auto_request(Browse next){
    request_catalog(next,0,0);
    if(!catalog_pid)auto_active=0;
}
static void auto_prefer_flixer_server(void){
    if(strcmp(auto_title.kind,"movie")&&strcmp(auto_title.kind,"episode"))return;
    /* Flixer's fifth server is echo. Keep all remaining fallbacks in order. */
    for(int i=0;i<auto_count;i++)if(!strcmp(auto_servers[i],"echo")){
        char preferred[32];strcpy(preferred,auto_servers[i]);
        memmove(auto_servers+1,auto_servers,(size_t)i*sizeof(auto_servers[0]));
        strcpy(auto_servers[0],preferred);break;
    }
}
static void auto_next(void){
    if(auto_index>=auto_count&&auto_count>0&&
       !strcmp(auto_title.kind,"kiss-episode")&&!kiss_1080_pass){
        kiss_1080_pass=1;
        auto_index=0;
        fprintf(stderr,"KissAnime: all sources exhausted; trying 1080p across all servers\n");
    }
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
    FILE *f=fopen("catalog-cache/result.tsv","r");char line[6000];int pages=1;
    Title source=auto_title;source.url[0]=0;source.height=0;
    if(f){while(fgets(line,sizeof(line),f)){
        if(line[0]=='#'&&!strchr(line,'\t')){int n;if(sscanf(line,"# pages=%d",&n)==1&&n>=1&&n<=7)pages=n;continue;}
        if(!strchr(line,'\n')&&!feof(f)){int ch;while((ch=fgetc(f))!=EOF&&ch!='\n'){}continue;}
        line[strcspn(line,"\r\n")]=0;char *field[12],*part=line;int nf=0;
        while(nf<12){field[nf++]=part;char *tab=strchr(part,'\t');if(!tab)break;*tab=0;part=tab+1;}
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
            if(nf>=11){if(strlen(field[9])<sizeof(source.subs_eng))strcpy(source.subs_eng,field[9]);
                if(strlen(field[10])<sizeof(source.subs_nor))strcpy(source.subs_nor,field[10]);}
            if(nf==12&&strlen(field[11])<sizeof(source.subs_spa))strcpy(source.subs_spa,field[11]);
        }
    }fclose(f);}
    if(source.url[0]){tv_quality_retry=0;if(tv_active&&tv_break_due&&tv_breaks){tv_stage_source(&source);return;}if(tv_active&&tv_joining){auto_resume=(double)(time(NULL)-tv_air_start);if(auto_resume<0)auto_resume=0;tv_joining=0;}else if(tv_active){TvAiring a;if(tv_airing_read(&a)&&a.id==auto_title.id){a.start=(long long)time(NULL);tv_airing_save(&a);}}subtitle_route_log("auto-result",&source);launch_player(&source);if(!player_pid)auto_active=0;return;}
    if(pending.mode==6)fprintf(stderr,"Source lookup returned no usable URL for server %s\n",pending.query);
    if(pending.mode==5){
        if(pending.page<pages&&auto_count<40){Browse next=pending;next.page++;auto_request(next);}
        else {auto_prefer_flixer_server();auto_index=0;auto_next();}
    }else {auto_index++;auto_next();}
}
static void finish_catalog(void){
    if(!catalog_pid)return;
    if(auto_active&&SDL_GetTicks()-catalog_started>70000)kill(catalog_pid,SIGKILL);
    int code;pid_t p=waitpid(catalog_pid,&code,WNOHANG);if(p!=catalog_pid)return;
    catalog_pid=0;
    download_background=0;
    if(auto_active){auto_result(WIFEXITED(code)&&!WEXITSTATUS(code));return;}
    if(pending.mode==13){
        if(tv_active&&WIFEXITED(code)&&!WEXITSTATUS(code)){
            count=total=0;selection=0;read_catalog("catalog-cache/result.tsv");
            if(total&&!strcmp(titles[visible[0]].kind,"tv-break")){tv_in_break=1;launch_player(&titles[visible[0]]);if(!player_pid)tv_in_break=0;}
        }
        if(!tv_in_break)fprintf(stderr,"TV commercial unavailable; continuing without a break\n");
        if(!tv_in_break&&tv_prepared)tv_begin_prepared();
        tv_next_at=0;return;
    }
    if(pending.mode==12){
        if(WIFEXITED(code)&&!WEXITSTATUS(code)&&tv_active)tv_candidate();
        else snprintf(status,sizeof(status),"TV: NO EPISODE FOUND / TRYING ANOTHER SHOW");
        tv_next_at=SDL_GetTicks()+10000;return;
    }
    if(!WIFEXITED(code)||WEXITSTATUS(code)){
        if(pending.mode==36){
            FILE *error=fopen("core-error.txt","r");char detail[140]="SEE CATALOG.LOG";
            if(error){if(!fgets(detail,sizeof(detail),error))strcpy(detail,"SEE CATALOG.LOG");fclose(error);}
            detail[strcspn(detail,"\r\n")]=0;
            snprintf(status,sizeof(status),"CORE INSTALL FAILED: %.76s",detail);return;
        }
        play_after_load=0;snprintf(status,sizeof(status),(pending.mode==16||pending.mode==19)?"ROM DOWNLOAD FAILED - SEE CATALOG.LOG":pending.mode==6?"SERVER UNAVAILABLE - SELECT ANOTHER SERVER":"COULD NOT LOAD - CHECK CONNECTION / CATALOG.LOG");return;}
    for(int i=0;i<VISIBLE;i++){char src[80],dst[80];snprintf(src,sizeof(src),"catalog-cache/poster-%d.next.bmp",i);snprintf(dst,sizeof(dst),"catalog-cache/poster-%d.bmp",i);rename(src,dst);}
    if(pending_push&&history_size<4)history[history_size++]=browse;
    if(pending_pop&&history_size)history_size--;
    if(pending.mode==16||pending.mode==19||pending.mode==27)pending.mode=14;
    if(pending.mode==29)pending.mode=28;
    if(pending.mode==36){
        for(int i=0;i<LIBRARY_RAM_CACHE;i++)if(library_pages[i].key.mode==14||library_pages[i].key.mode==22)library_pages[i].valid=0;
        pending.mode=35;
    }
    if(pending.mode==14)pending.id=0;
    browse=pending;count=0;ready_only=0;about=0;listing_pages=1;listing_total=0;memset(titles,0,sizeof(titles));read_catalog("catalog-cache/result.tsv");
    if(browse.selected>=0&&browse.selected<total)selection=browse.selected;
    library_cache_store(&browse);
    if((browse.mode<5||browse.mode>=7)&&browse.mode!=23&&browse.mode!=24&&browse.mode<35){prefetch_browse=browse;prefetch_left=browse.page==1?3:2;}
    if((browse.mode==5||browse.mode==6)&&!total)snprintf(status,sizeof(status),"NO SOURCES RETURNED FOR THIS TITLE - B TO RETURN");
    else snprintf(status,sizeof(status),"%s / PAGE %d OF %d / X CHANGE LIBRARY",browse_labels[browse.mode],browse.page,listing_pages);
    if(browse.mode==7||browse.mode==8)snprintf(status,sizeof(status),"KISSANIME: BROWSING WORKS / SOME VIDEO HOSTS ARE NOT SUPPORTED");
    if(browse.mode==14)snprintf(status,sizeof(status),"Y IMPORT / START LICENSED DOWNLOADS / SHARE+OPTIONS EXIT GAME");
    if(browse.mode==17&&!total)snprintf(status,sizeof(status),"NO ARCHIVE TITLES FOUND - TRY ANOTHER PLATFORM");
    if(browse.mode==18&&!total)snprintf(status,sizeof(status),"NO SUPPORTED ROM FILES IN THIS ARCHIVE ITEM");
    if(browse.mode==14||browse.mode==21){
        FILE *f=fopen("game-exit-status","r");int code=0;
        if(f){int got=fscanf(f,"%d",&code);fclose(f);unlink("game-exit-status");
            if(got==1&&code){
                snprintf(status,sizeof(status),"GAME COULD NOT START / SEE GAME.LOG / Y REIMPORT");
                f=fopen("game-error.txt","r");
                if(f){char message[100]="";if(fgets(message,sizeof(message),f))snprintf(status,sizeof(status),"%.99s",message);fclose(f);unlink("game-error.txt");}
            }
        }
    }
    if(browse.mode==10&&!total)snprintf(status,sizeof(status),"NO PUBLIC H.264 / MPEG4 FILES IN THIS ITEM");
    if(play_after_load){play_after_load=0;if(total)play();else if(browse.mode==34)snprintf(status,sizeof(status),"END OF AVAILABLE SERIES");}
}
/* Warm catalog.c's bounded on-disk page cache only while browsing is quiet.
 * Catalog workers remain separate; foreground requests always cancel prefetch. */
static void prefetch_pages(void){
    static Uint32 next_prefetch_at;
    if(prefetch_pid){
        int code;pid_t done=waitpid(prefetch_pid,&code,WNOHANG);
        if(done==0)return;
        prefetch_pid=0;
        next_prefetch_at=SDL_GetTicks()+1500;
    }
    if(!prefetch_left||catalog_pid||player_pid||auto_active||update_pid||
       settings_on||about||search_on||tv_active||browse.mode!=prefetch_browse.mode||
       browse.id!=prefetch_browse.id||browse.season!=prefetch_browse.season||
       browse.episode!=prefetch_browse.episode||strcmp(browse.query,prefetch_browse.query))return;
    Uint32 now=SDL_GetTicks();
    if((Sint32)(now-controller_activity)<1200||(Sint32)(now-next_prefetch_at)<0)return;
    if(!library_cacheable(browse.mode)){prefetch_left=0;return;}
    if(prefetch_browse.page>=listing_pages){prefetch_left=0;return;}
    Browse target=prefetch_browse;target.page++;
    char pg[16],id[16],sn[16],ep[16];
    snprintf(pg,sizeof(pg),"%d",target.page);snprintf(id,sizeof(id),"%d",target.id);
    snprintf(sn,sizeof(sn),"%d",target.season);snprintf(ep,sizeof(ep),"%d",target.episode);
    const char *args[]={"greenlink-catalog",browse_kinds[target.mode],pg,target.query,id,sn,ep,"",NULL};
    prefetch_pid=shell_spawn("./greenlink-catalog",args,"/dev/null","/dev/null");
    if(prefetch_pid<0){prefetch_pid=0;prefetch_left=0;return;}
    prefetch_browse.page=target.page;
    prefetch_left--;
}
static void controller_used(void){controller_activity=SDL_GetTicks();controller_slept=0;}
static void controller_idle(void){
#ifdef _WIN32
    return;
#else
    if(idle_pid){if(waitpid(idle_pid,NULL,WNOHANG)==idle_pid)idle_pid=0;else return;}
    if(controller_slept||SDL_GetTicks()-controller_activity<300000)return;
    controller_slept=1;
    if(access("./controller-idle.sh",R_OK))return;
    const char *args[]={"sh","./controller-idle.sh",NULL};
    idle_pid=shell_spawn("/bin/sh",args,"controller.log","controller.log");
    if(idle_pid<0)idle_pid=0;
#endif
}
/* User-curated mixed library. Store the same bounded TSV fields used by
 * the catalog; no account or remote service is required. */
static void favorites_toggle(void){
    if(!total)return;
    const Title *t=&titles[visible[selection]];
    if(!strcmp(t->kind,"source")||!strcmp(t->kind,"quality")||!strcmp(t->kind,"server"))return;
    FILE *in=fopen("favorites.tsv","r"),*out=fopen("favorites.tsv.next","w");
    if(!out){if(in)fclose(in);return;}
    char row[6000];int found=0;
    if(in){while(fgets(row,sizeof(row),in)){
        row[strcspn(row,"\r\n")]=0;
        /* Match kind, title, id, season and episode using saved columns. */
        char *cols[12],*q=row;int n=0;
        while(n<12){cols[n++]=q;char *tab=strchr(q,'\t');if(!tab)break;*tab=0;q=tab+1;}
        if(n>=8&&!strcmp(cols[0],t->title)&&!strcmp(cols[4],t->kind)&&
           atoi(cols[5])==t->id&&atoi(cols[6])==t->season&&atoi(cols[7])==t->episode){
            found=1;continue;
        }
        for(int i=0;i<n;i++)fprintf(out,"%s%s",cols[i],i+1==n?"\n":"\t");
    }fclose(in);}
    if(!found){
        /* Persist a bounded copy of the original poster. Catalog cache files
         * are page-scoped and can be overwritten by unrelated browsing. */
        char saved_art[192]="";
        if(!strncmp(t->poster,"game-art-cache/",15)&&!strstr(t->poster,".."))
            snprintf(saved_art,sizeof(saved_art),"%s",t->poster);
        struct stat art_stat;
        if(!strncmp(t->poster,"catalog-cache/",14)&&!strstr(t->poster,"..")&&
           !stat(t->poster,&art_stat)&&S_ISREG(art_stat.st_mode)&&
           art_stat.st_size>=54&&art_stat.st_size<=192*288*4+4096){
            unsigned long hash=2166136261UL;
            const char *parts[]={t->kind,t->title,t->url};
            for(size_t k=0;k<sizeof(parts)/sizeof(parts[0]);k++)
                for(const unsigned char *p=(const unsigned char *)parts[k];*p;p++)
                    hash=(hash^*p)*16777619UL;
            hash=(hash^(unsigned)t->id)*16777619UL;
            hash=(hash^(unsigned)t->season)*16777619UL;
            mkdir("favorites-art",0700);
            snprintf(saved_art,sizeof(saved_art),"favorites-art/%08lx.bmp",hash&0xffffffffUL);
            FILE *src=fopen(t->poster,"rb"),*dst=src?fopen(saved_art,"wb"):NULL;
            int good=src&&dst;unsigned char bytes[4096];size_t n;
            if(good){
                while((n=fread(bytes,1,sizeof(bytes),src))>0)
                    if(fwrite(bytes,1,n,dst)!=n){good=0;break;}
                if(ferror(src))good=0;
            }
            if(src)fclose(src);
            if(dst&&fclose(dst))good=0;
            if(!good){unlink(saved_art);saved_art[0]=0;}
        }
        fprintf(out,"%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n",
            t->title,t->meta,saved_art,t->url,t->kind,t->id,t->season,t->episode,t->height,
            t->subs_eng,t->subs_nor,t->subs_spa);
    }
    if(fclose(out)){unlink("favorites.tsv.next");return;}
    if(rename("favorites.tsv.next","favorites.tsv"))return;
    snprintf(status,sizeof(status),found?"REMOVED FROM FAVORITES":"ADDED TO FAVORITES");
    if(browse.mode==FAVORITES_MODE){Browse next=browse;request_catalog(next,0,0);}
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
    if(cached_page==page){
        /* Background-prefetched game posters become visible without navigation. */
        static Uint32 next_check;
        Uint32 now=SDL_GetTicks();
        if((int32_t)(now-next_check)>=0){
            next_check=now+1500;
            for(int i=0;i<VISIBLE&&page*VISIBLE+i<total;i++){
                if(posters[i])continue;
                const char *art=titles[visible[page*VISIBLE+i]].poster;
                if(!strncmp(art,"game-art-cache/",15))posters[i]=load_poster(art);
            }
        }
        return;
    }
    clear_posters();cached_page=page;flow_position=(float)selection;
    for(int i=0;i<VISIBLE && page*VISIBLE+i<total;i++)posters[i]=load_poster(titles[visible[page*VISIBLE+i]].poster);
}
static void placeholder(int x,int y,int w,int h,int seed) {
    if(seed>=0&&seed<MAX_TITLES&&!strcmp(titles[seed].kind,"game-folder")){
        /* Platform badge, not a generic missing-art placeholder. */
        const char *label=titles[seed].title;
        SDL_Color ink={235,231,227,255},shadow={67,25,37,255},accent={190,40,62,255};
        rect(x,y,w,h,shadow,1);
        rect(x+8,y+8,w-16,h-16,accent,0);
        rect(x+18,y+27,w-36,64,ink,0);
        rect(x+25,y+36,w-50,46,shadow,1);
        text(x+30,y+52,"FLXTR",2,ink,8);
        text(x+14,y+h-39,label,1,ink,19);
        return;
    }
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
    char line[90];snprintf(line,sizeof(line),"PAGE %d / %d   A OPEN   UP/DOWN PAGE",browse.mode?browse.page:page+1,browse.mode?listing_pages:(total+5)/6);
    text(194,475,line,1,dim,120);
}
static int flow_animating(void){return coverflow&&!settings_on&&!about&&!search_on&&(browse.mode<5||browse.mode>=7)&&total&&flow_position!=(float)selection;}
static void start_update(void) {
    if(samba_pid){snprintf(status,sizeof(status),"WAIT FOR SAMBA INSTALL/SERVICE WORKER");return;}
#ifdef _WIN32
    ShellExecuteW(NULL,L"open",L"https://github.com/vatomalo/FlXtR-Steamlink/releases",NULL,NULL,SW_SHOWNORMAL);
    snprintf(status,sizeof(status),"DOWNLOAD AND EXTRACT THE WINDOWS UPDATE FROM GITHUB");return;
#else
    if(access("./update.sh",R_OK)){snprintf(status,sizeof(status),"UPDATER NOT INSTALLED");return;}
    const char *args[]={"sh","./update.sh",NULL};
    update_pid=shell_spawn("/bin/sh",args,"update.log","update.log");
    if(update_pid<0){update_pid=0;snprintf(status,sizeof(status),"COULD NOT START UPDATER");return;}
    snprintf(status,sizeof(status),"CHECKING GITHUB FOR A SHELL UPDATE...");
#endif
}
#include "music_ui.h"
#include "youtube_video.h"
#include "settings_services.h"
static int keyboard_help;
static void draw(Uint32 tick) {
    if(keyboard_help){
        SDL_SetRenderDrawColor(renderer,theme_canvas.r,theme_canvas.g,theme_canvas.b,255);SDL_RenderClear(renderer);
        text(45,35,"KEYBOARD CONTROLS",3,green,50);
        const char *lines[]={"ARROWS / ENTER   Navigate / select","ESC              Back / stop video","SPACE / K        Pause / resume playback","J / L            Seek backward / forward 10 seconds","F / M IN VIDEO   Fullscreen / mute","TAB IN VIDEO     Playback menu","F2               Next library","F3 / CTRL+F      Search / TAB completes typed text","F10              Settings / F4 next theme","M / T / G        Music / TV / Games","CTRL+P           Phone setup / CTRL+Q quit","F1 / ESC         Close this help"};
        for(size_t i=0;i<sizeof(lines)/sizeof(lines[0]);i++)text(45,95+(int)i*33,lines[i],1,white,100);
        SDL_RenderPresent(renderer);return;
    }
    if(samba_password_on){
        SDL_SetRenderDrawColor(renderer,theme_canvas.r,theme_canvas.g,theme_canvas.b,255);SDL_RenderClear(renderer);
        text(194,94,"SAMBA PASSWORD / USER ROOT",2,green,50);
        char masked[65];size_t length=strlen(samba_password);memset(masked,'*',length);masked[length]=0;
        text(194,146,masked,2,white,56);
        for(int i=0;i<40;i++){
            int x=194+(i%10)*64,y=202+(i/10)*48;char key[2]={search_keys[i],0};
            if(samba_password_lower)key[0]=(char)tolower((unsigned char)key[0]);
            rect(x,y,48,38,i==samba_password_key?green:dim,0);text(x+15,y+10,key,2,i==samba_password_key?green:white,1);
        }
        text(194,420,"A TYPE / X CASE / START SAVE / B CANCEL",1,green,90);
        text(194,446,"8-64 CHARACTERS / SAVING ENABLES LOCAL SHARING",1,white,90);
        text(194,474,samba_message,1,dim,90);
        SDL_RenderPresent(renderer);return;
    }
    if(music_on){music_draw();SDL_RenderPresent(renderer);return;}
    SDL_SetRenderDrawColor(renderer,theme_canvas.r,theme_canvas.g,theme_canvas.b,255);SDL_RenderClear(renderer);
    if((background&&!monochrome_menu)||menu_theme>=3){
        if(background)SDL_RenderCopy(renderer,background,NULL,NULL);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        SDL_Color pane=theme_pane(210);
        SDL_Color side=theme_pane(225);
        SDL_Color foot=theme_pane(232);
        rect(20,16,920,55,pane,1);
        rect(20,80,155,400,side,1);
        rect(20,483,920,43,foot,1);
        rect(184,419,746,63,side,1);
        if(settings_on||bios_on||filters_on||search_on||about||tv_schedule_on||browse.mode==11)rect(184,80,746,339,theme_pane(225),1);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
    }
    if(stars_on&&(!background||monochrome_menu))for(int i=0;i<STARS;i++) {
        int x=star_x[i]*W/256, y=(star_y[i]*H/256+(tick/100)*star_speed[i]/8)%H;
        SDL_Color c={145+(i%3)*40,145+(i%3)*40,145+(i%3)*40,255};rect(x,y,1+(i%9==0),1+(i%9==0),c,1);
    }
    if(favorite_remove_confirm){
        rect(184,120,746,259,(SDL_Color){0,0,0,235},1);
        text(200,150,"REMOVE FAVORITE?",3,green,40);
        if(total&&selection>=0&&selection<total)
            text(200,213,titles[visible[selection]].title,2,white,60);
        text(200,307,"A CONFIRM REMOVE / B CANCEL",2,green,55);
        SDL_RenderPresent(renderer);return;
    }
    if(rom_core_menu){
        rect(184,80,746,339,(SDL_Color){0,0,0,235},1);
        text(200,95,"CORE OPTIONS / ROM",2,green,50);
        text(200,125,rom_core_title,1,white,70);
        for(int i=rom_core_first;i<rom_core_count&&i<rom_core_first+7;i++){
            char line[112];snprintf(line,sizeof(line),"%c %s",i==rom_core_selected?'>':' ',rom_core_names[i]);
            text(206,161+(i-rom_core_first)*32,line,2,i==rom_core_selected?green:white,55);
        }
        text(200,395,"A SAVE CORE / B CANCEL",1,green,65);
        SDL_RenderPresent(renderer);return;
    }
    text(30,24,"FLXTR",4,green,12);text(180,38,"CINEMA / STEAM LINK",1,dim,24);
    if(music_job==3||music_job==2){text(737,22,"[R1] MUSIC / NOW PLAYING",1,dim,32);music_now_playing(737,43,31,green);}
    else text(737,32,ui_hint("[R1] MUSIC","[M] MUSIC"),2,dim,17);
    rect(30,65,900,1,dim,1);
    text(30,92,"LIBRARY",2,green,12);
    int root_mode=history_size?history[0].mode:browse.mode;
    if(root_mode==14||root_mode==22)root_mode=21;
    if(root_mode==9||root_mode==10||root_mode==31)root_mode=30;
    const int menu_modes[]={11,FAVORITES_MODE,1,2,7,21,30,0};
    const char *const menu_names[]={"TV","FAVORITES","MOVIES","SERIES","KISSANIME","GAMES","ARCHIVE","LOCAL"};
    for(int i=0;i<8;i++){
        char line[32];snprintf(line,sizeof(line),"%c %s",root_mode==menu_modes[i]?'>':' ',menu_names[i]);
        text(30,124+i*21,line,1,root_mode==menu_modes[i]?green:dim,18);
    }
    char num[64];snprintf(num,sizeof(num),browse.mode==7?"%d+ TITLES":"%d TITLES",browse.mode?listing_total:total);text(30,310,num,1,white,20);
    text(30,379,ui_hint("[SELECT] SETTINGS","F10 SETTINGS / F1 HELP"),1,dim,22);text(30,402,ui_hint("[X] LIBRARY","F2 LIBRARY"),1,dim,22);text(30,421,browse.mode==1||browse.mode==2||browse.mode==7?ui_hint("[Y] GENRE / SECTION","F6 GENRE / SECTION"):browse.mode==14?ui_hint("[L2] ROM FOLDERS","F8 ROM FOLDERS"):browse.mode==28?"[Y] DELETE ZIP":browse.mode==21?"[Y] ALL GAMES / [A] FOLDER":background?"WHITE METAL":"[Y] STARS",1,dim,22);text(30,440,browse.mode==1||browse.mode==2||browse.mode==7||browse.mode==9||browse.mode==17||browse.mode==31?ui_hint("[START] SEARCH","F3 SEARCH"):browse.mode==14?ui_hint("[START] GET GAMES","F3 GET GAMES"):browse.mode==21?ui_hint("[START] GET GAMES","F3 GET GAMES"):ui_hint("[START] ABOUT","F1 HELP / I ABOUT"),1,dim,22);text(30,459,browse.mode==14||browse.mode==21||browse.mode==22||browse.mode==26||browse.mode==28?"B BACK / L2 ROM FOLDERS":ui_hint("B BACK / L3 FAVORITE","ESC BACK / F FAVORITE"),1,dim,22);
    if(accounts_on){
        rect(184,80,746,410,(SDL_Color){12,20,14,255},1);
        text(204,96,party_entry==2?"ROMSGAMES / PHONE IMPORT":phone_party.phase||party_entry?"POCKET QUIZ":"PHONE / API ACCOUNTS",3,(SDL_Color){140,240,150,255},40);
        if(phone_party.phase&&party_entry!=2){
            const PhoneQuestion *q=phone_question();char caption[96];
            int last=phone_party.round>=6,answered=0;
            for(int i=0;i<phone_party.count;i++)if(phone_party.players[i].answer>=0)answered++;
            snprintf(caption,sizeof(caption),"ROUND %d / %d  %s%s",phone_party.round<PHONE_ROUNDS?phone_party.round+1:PHONE_ROUNDS,PHONE_ROUNDS,phone_party.phase==3?"FINAL SCORES":q->kind?"READ THE ROOM":"TRIVIA TWIST",last&&phone_party.phase!=3?" / DOUBLE POINTS":"");
            text(204,136,caption,2,(SDL_Color){140,240,150,255},58);
            if(phone_party.phase!=3){
                int left=(int32_t)(phone_party.deadline-phone_now)>0?(int)(phone_party.deadline-phone_now)/1000:0;
                if(phone_party.phase==2&&q->kind)snprintf(caption,sizeof(caption),"ROOM VOTES / A: %d  B: %d  C: %d  D: %d",phone_party.votes[0],phone_party.votes[1],phone_party.votes[2],phone_party.votes[3]);
                else if(phone_party.phase==2)snprintf(caption,sizeof(caption),"ANSWER REVEAL / NEXT ROUND IN %d",left);
                else snprintf(caption,sizeof(caption),"%d SECONDS / %d OF %d LOCKED IN",left,answered,phone_party.count);
                text(204,160,caption,1,(SDL_Color){240,190,110,255},65);
                wrap(204,182,q->question,56,3,(SDL_Color){255,255,255,255});
                for(int i=0;i<4;i++){
                    int win=phone_party.phase==2&&(phone_party.winners&(1<<i));
                    snprintf(caption,sizeof(caption),"%c: %s",'A'+i,q->answers[i]);
                    text(204+(i%2)*350,248+(i/2)*29,caption,2,win?(SDL_Color){140,240,150,255}:(SDL_Color){255,255,255,255},28);
                }
                if(phone_party.phase==2)wrap(204,307,q->explanation,58,2,(SDL_Color){240,190,110,255});
            }
            int order[8];for(int i=0;i<phone_party.count;i++)order[i]=i;
            for(int i=1;i<phone_party.count;i++)for(int j=i;j>0&&phone_party.players[order[j]].score>phone_party.players[order[j-1]].score;j--){int t=order[j];order[j]=order[j-1];order[j-1]=t;}
            if(phone_party.phase==3){
                int best=phone_party.count?phone_party.players[order[0]].score:0,ties=0;
                for(int i=0;i<phone_party.count;i++)if(phone_party.players[i].score==best)ties++;
                snprintf(caption,sizeof(caption),"%d-WAY TIE / SHARED GLORY",ties);
                if(ties<=1&&phone_party.count)snprintf(caption,sizeof(caption),"WINNER: %s",phone_party.players[order[0]].name);
                text(204,195,caption,3,(SDL_Color){240,190,110,255},38);
                text(204,245,"REMATCH FROM YOUR PHONE / FRESH QUESTIONS",1,(SDL_Color){220,230,220,255},70);
            }
            for(int i=0;i<phone_party.count;i++){int p=order[i];snprintf(caption,sizeof(caption),"%s: %d +%d",phone_party.players[p].name,phone_party.players[p].score,phone_party.phase==2?phone_party.players[p].gain:0);text(204+(i%2)*350,355+(i/2)*22,caption,2,(SDL_Color){220,230,220,255},28);}
        }else if(flxtr_socket_valid(api_setup.listener)){
            int size=qrcodegen_getSize(accounts_qr),scale=party_entry==2?280/(size+8):4,left=204,top=130;
            if(scale>4)scale=4;
            if(scale<1)scale=1;
            rect(left,top,(size+8)*scale,(size+8)*scale,(SDL_Color){255,255,255,255},1);
            for(int y=0;y<size;y++)for(int x=0;x<size;x++)if(qrcodegen_getModule(accounts_qr,x,y))
                rect(left+(x+4)*scale,top+(y+4)*scale,scale,scale,(SDL_Color){0,0,0,255},1);
            text(524,155,"SCAN WITH YOUR PHONE",2,(SDL_Color){255,255,255,255},32);
            wrap(524,195,party_entry==2?"OPEN THE GAME LINK ON YOUR PHONE. DOWNLOAD THERE, RETURN TO THE PHONE REMOTE, THEN UPLOAD THE ROM.":party_entry?"JOIN POCKET QUIZ ON YOUR PHONE. ENTER YOUR NAME, THEN START WHEN EVERYONE IS READY.":"API KEYS, VIDEO UPLOADS, A PHONE REMOTE AND POCKET QUIZ. OPEN THE PHONE REMOTE LINK.",30,5,(SDL_Color){220,230,220,255});
            text(524,315,party_entry==2?"KEEP THIS SCREEN OPEN":party_entry?"1-8 PLAYERS / EIGHT ROUNDS":access("subdl-api-key.txt",R_OK)?"SUBDL: NOT SET":"SUBDL: KEY SAVED",2,(SDL_Color){140,240,150,255},32);
            text(524,348,party_entry==2?"B CLOSE AND IMPORT":party_entry?"NO API KEY NEEDED":access("thegamesdb.key",R_OK)?"THEGAMESDB: NOT SET":"THEGAMESDB: KEY SAVED",2,(SDL_Color){140,240,150,255},32);
            if(wifi_qr_valid&&party_entry!=2){
                int ws=qrcodegen_getSize(wifi_qr),unit=2;
                rect(204,322,(ws+8)*unit,(ws+8)*unit,(SDL_Color){255,255,255,255},1);
                for(int y=0;y<ws;y++)for(int x=0;x<ws;x++)if(qrcodegen_getModule(wifi_qr,x,y))rect(204+(x+4)*unit,322+(y+4)*unit,unit,unit,(SDL_Color){0,0,0,255},1);
                text(360,340,"JOIN THIS WI-FI",1,(SDL_Color){220,230,220,255},25);
                text(360,359,"CONTAINS WI-FI PASSWORD",1,(SDL_Color){220,230,220,255},28);
            }else if(party_entry!=2)text(204,370,"WI-FI QR UNAVAILABLE",1,(SDL_Color){220,230,220,255},40);
            text(524,402,api_setup.url,1,(SDL_Color){220,230,220,255},65);
        }
        text(204,443,api_setup.message,1,(SDL_Color){140,240,150,255},90);
        text(204,473,"B CLOSE / A NEW ROOM / EXPIRES AFTER 10 MINUTES IDLE",1,(SDL_Color){220,230,220,255},90);
    }else if(filters_on){
        text(194,100,"BROWSE FILTERS",3,green,40);
        const char *genre=browse.mode==7&&browse.id==11?"KIDS":browse_genres[browse.id];
        text(204,180,filter_row==0?"> GENRE":"  GENRE",2,green,30);text(500,180,genre,2,white,30);
        text(204,240,filter_row==1?"> SECTION":"  SECTION",2,green,30);text(500,240,browse_orders[browse.season],2,white,30);
        text(194,350,"UP/DOWN ROW / LEFT/RIGHT CHANGE",2,green,60);
        text(194,400,"A APPLY / B OR X SKIP",2,green,60);
    }else if(tv_schedule_on||(browse.mode==11&&!settings_on&&!about)){
        text(194,94,tv_schedule_on?"EDIT TV SCHEDULE":mtv_override?"MTV / MUSIC VIDEOS":"TV MODE / OSLO TIME",2,green,55);
        int active=tv_block_at(tv_blocks,tv_hour());
        for(int i=0;i<TV_BLOCKS;i++){
            char line[80];snprintf(line,sizeof(line),"%c %02d-%02d  %s %s",i==active?'>':' ',tv_blocks[i].hour,i+1<TV_BLOCKS?tv_blocks[i+1].hour:24,tv_genres[tv_blocks[i].genre],tv_blocks[i].marathon?"[MARATHON]":"");
            int y=126+i*29;if(tv_schedule_on&&i==tv_row)rect(190,y-5,724,26,green,0);
            text(202,y,line,2,i==active||i==tv_row?green:white,58);
            if(tv_schedule_on&&i==tv_row)text(840,y,tv_column==2?"RUN":tv_column?"GENRE":"HOUR",1,green,10);
        }
        text(194,427,tv_schedule_on?"LEFT/RIGHT: CHANGE  /  A: HOUR, GENRE, MARATHON":"ENTER/A START TV / F10 EDIT SCHEDULE",1,green,90);
        text(194,452,tv_schedule_on?"UP/DOWN: SELECT BLOCK   B: SAVE & EXIT":ui_hint("Y: MTV / SCHEDULE   MUSIC QUERY: MTV-QUERY.TXT","F6: MTV / SCHEDULE   MUSIC QUERY: MTV-QUERY.TXT"),1,dim,90);
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
        const char *labels[]={"QUALITY TARGET","PREBUFFER","RAM LIMIT","LIBRARY VIEW","SUBTITLES","SUBTITLE SIZE","SUBTITLE DELAY","TV SCHEDULE","TV COMMERCIALS","MENU THEME","GAME BIOS","GAME AUTO RESUME","TV RATING WEIGHT","TV POPULARITY WEIGHT","TV VARIETY WEIGHT","API ACCOUNTS","PARTY GAMES","AUTOPLAY SERIES","AUTO UPDATES","GAME OUTPUT","AUTOPLAY NEXT SONG","MUSIC PLAYER","UNUSED","SAMBA DOWNLOAD","SAMBA PASSWORD","SAMBA SHARING"};
#else
        const char *labels[]={"QUALITY TARGET","RAM PREBUFFER","RAM LIMIT","LIBRARY VIEW","SUBTITLES","SUBTITLE SIZE","SUBTITLE DELAY","TV SCHEDULE","TV COMMERCIALS","MENU THEME","GAME BIOS","GAME AUTO RESUME","TV RATING WEIGHT","TV POPULARITY WEIGHT","TV VARIETY WEIGHT","API ACCOUNTS","PARTY GAMES","AUTOPLAY SERIES","AUTO UPDATES","GAME OUTPUT","AUTOPLAY NEXT SONG","MUSIC PLAYER","UNUSED","SAMBA DOWNLOAD","SAMBA PASSWORD","SAMBA SHARING"};
#endif
        char values[26][40];if(quality_setting==3)snprintf(values[0],40,"AUTO / %dP",adaptive_height);else snprintf(values[0],40,"%dP",qualities[quality_setting]);snprintf(values[1],40,"%d SECONDS",buffer_seconds[buffer_setting]);
        snprintf(values[2],40,"%d MB",disk_megabytes[disk_setting]);snprintf(values[3],40,"%s",coverflow?"COVERFLOW":"SIX-COVER WALL");
        const char *sub_names[]={"OFF","AUTOMATIC","ENGLISH","NORWEGIAN","SPANISH"};snprintf(values[4],40,"%s",sub_names[subtitle_setting]);snprintf(values[5],40,"%s",subtitle_scale==2?"NORMAL":"LARGE");snprintf(values[6],40,"%+d SECONDS",subtitle_delay);
        snprintf(values[7],40,"EDIT HOURS / GENRES");snprintf(values[8],40,"%s",tv_breaks?"BETWEEN PROGRAMS":"OFF");snprintf(values[9],40,"%s",menu_theme_names[menu_theme]);snprintf(values[10],40,"VIEW BIOS STATUS");snprintf(values[11],40,"%s",game_autostate?"ON":"OFF");
        snprintf(values[12],40,"%d / 5",tv_rating_weight);snprintf(values[13],40,"%d / 5",tv_popularity_weight);snprintf(values[14],40,"%d / 5",tv_variety_weight);
        snprintf(values[15],40,"SCAN QR WITH PHONE");
        snprintf(values[16],40,"POCKET QUIZ");
        snprintf(values[17],40,"%s",series_autoplay?"ON":"OFF");snprintf(values[18],40,"%s",auto_updates_enabled()?"ON":"OFF");
        if(game_output)snprintf(values[19],40,"%dP / %d X %d",game_output,game_output_width(game_output),game_output);
        else snprintf(values[19],40,"NATIVE / RETROARCH");
        snprintf(values[20],40,"%s",music_autoplay?"ON":"OFF");
        snprintf(values[21],40,"OPEN MUSIC BROWSER");
        snprintf(values[23],40,"%s",samba_pid?"WORKING...":samba_installed()?"INSTALLED":"DOWNLOAD AND INSTALL");
        snprintf(values[24],40,"SET / CHANGE PASSWORD");
        snprintf(values[25],40,"%s",!access("samba/.enabled",R_OK)?"STARTUP ON":"OFF");
#if defined(FLXTR_DESKTOP) || defined(_WIN32)
        for(int i=23;i<=25;i++)snprintf(values[i],40,"STEAM LINK ONLY");
#endif
        text(194,94,"SETTINGS",3,green,40);
        for(int i=0;i<SETTINGS_TABS;i++){
            int x=194+i*103;
            if(i==settings_tab)rect(x-4,122,99,25,green,0);
            text(x,130,settings_tabs[i].name,1,i==settings_tab?green:dim,16);
        }
        const SettingsTab *tab=&settings_tabs[settings_tab];
        for(int row=0;row<tab->count;row++){
            int id=tab->ids[row],y=174+row*36;
            if(row==settings_row)rect(190,y-8,724,29,green,0);
            text(204,y,labels[id],2,white,24);
            text(566,y,values[id],2,row==settings_row?green:dim,28);
        }
        text(194,430,"LB/RB OR TAB: TABS / DOWN: OPTIONS / LEFT/RIGHT: CHANGE",1,green,110);
        text(194,452,"A SELECT / B SAVE AND RETURN",1,dim,90);
        text(194,474,settings_tab==SETTINGS_SERVICES?samba_message:
            settings_tab==SETTINGS_MOVIES||settings_tab==SETTINGS_SERIES?
            "QUALITY AND SUBTITLES APPLY TO ALL VIDEO":status,1,dim,110);
    }else if(search_on){
        text(194,100,"SEARCH",3,green,30);
        const char *completion=search_completion();
        /* Phone-style prediction strip directly above the active input. */
        if(completion){
            rect(194,121,684,24,dim,0);
            text(204,126,completion,1,white,65);
            text(792,126,"Y ACCEPT",1,green,12);
        }else if(strlen(search_text)>=4){
            text(202,126,search_dict_count?"NO SUGGESTION":"DICTIONARY EMPTY",1,dim,65);
        }
        text(194,154,search_text,2,white,60);
        for(int i=0;i<40;i++){
            int x=194+(i%10)*64,y=202+(i/10)*48;char ch[2]={search_keys[i],0};
            rect(x,y,48,38,i==search_key?green:dim,0);text(x+15,y+10,ch,2,i==search_key?green:white,1);
        }
        text(194,442,completion?"Y ACCEPT SUGGESTION / A TYPE / START SEARCH":"A TYPE / B DELETE / START SEARCH",1,green,76);
        text(194,467,"< DELETE    > SEARCH    ESC CANCEL",1,dim,60);
    }else if(about) {
        text(194,100,"SMALL BY DESIGN",3,green,35);
        wrap(194,155,"A QUIET LIBRARY FOR YOUR STEAM LINK. WHITE METAL, WATER DROPS, AND GREEN PIXELS.",53,4,white);
        wrap(194,260,"C + SDL2. SIX POSTERS IN MEMORY. NO BROWSER. NO WEB UI. DIRECT VIDEO PLAYBACK USES A SEPARATE PLAYER.",53,5,dim);
        text(194,355,about_row==0?"> CHECK FOR UPDATE":"  CHECK FOR UPDATE",2,about_row==0?green:white,50);
        text(194,390,about_row==1?"> PARTY GAMES / POCKET QUIZ":"  PARTY GAMES / POCKET QUIZ",2,about_row==1?green:white,50);
        text(194,458,"UP/DOWN SELECT / A OPEN / B RETURN",1,dim,65);
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
            const char *label=!strcmp(t->kind,"core-manager")?"A OPEN CORE DOWNLOADER":!strcmp(t->kind,"core-download")?"A INSTALL MISSING CORE":!strcmp(t->kind,"rg-game")?"A PHONE DOWNLOAD / QR":!strcmp(t->kind,"rg-source")||!strcmp(t->kind,"rg-platform")?"A BROWSE ROMSGAMES":!strcmp(t->kind,"archive")?"BROWSE VIDEO FILES":!strcmp(t->kind,"kiss")?"BROWSE EPISODES":!strcmp(t->kind,"tv")?"BROWSE SEASONS":!strcmp(t->kind,"season")?"BROWSE EPISODES":!strcmp(t->kind,"archive-game")?"A DOWNLOAD GAME":!strcmp(t->kind,"download-zip")?"A BROWSE ZIP / Y DELETE":!strcmp(t->kind,"game-zip")?"A BROWSE ZIP":!strcmp(t->kind,"zip-member")?"A EXTRACT ROM":!strcmp(t->kind,"game")?"A PLAY GAME":t->url[0]?"PLAYABLE SOURCE":"A PLAY / AUTO SERVER";
            text(x,y+177,label,1,dim,22);
        }
        Title *t=&titles[visible[selection]];
        rect(665,94,1,378,dim,1);text(690,97,"NOW SELECTED",1,dim,30);
        wrap_detail(690,126,t->title,4,green);
        /* PSP-style detail pane: consume only local metadata, never wait for web. */
        char detail[400]="",detail_path[128];
        if((!strcmp(t->kind,"movie")||!strcmp(t->kind,"tv"))&&t->id>0){
            snprintf(detail_path,sizeof(detail_path),"media-info-cache/%s-%d.txt",t->kind,t->id);
            FILE *f=fopen(detail_path,"r");
            if(f){if(!fgets(detail,sizeof(detail),f))detail[0]=0;fclose(f);}
        }else if(!strcmp(t->kind,"kiss")&&t->id>0){
            snprintf(detail_path,sizeof(detail_path),"media-info-cache/anime-%d.txt",t->id);
            FILE *f=fopen(detail_path,"r");
            if(f){if(!fgets(detail,sizeof(detail),f))detail[0]=0;fclose(f);}
        }
        if(!detail[0]&&(!strcmp(t->kind,"game")||!strcmp(t->kind,"game-zip"))){
            game_info_path(t->title,detail_path,sizeof(detail_path));
            FILE *f=fopen(detail_path,"r");
            if(f){if(!fgets(detail,sizeof(detail),f))detail[0]=0;fclose(f);}
        }
        if(!detail[0]&&(!strcmp(t->kind,"game")||!strcmp(t->kind,"game-zip")||!strcmp(t->kind,"kiss"))){
            /* Optional local game/anime descriptions keyed by title, kept
             * separate from artwork and launch IDs. No network on selection. */
            FILE *f=fopen("media-descriptions.tsv","r");
            if(f){
                char line[640];
                const char *kind=!strcmp(t->kind,"kiss")?"anime":"game";
                while(fgets(line,sizeof(line),f)){
                    char *first=strchr(line,'\t');if(!first)continue;*first++=0;
                    char *second=strchr(first,'\t');if(!second)continue;*second++=0;
                    if(!strcmp(line,kind)&&!strcasecmp(first,t->title)){
                        snprintf(detail,sizeof(detail),"%s",second);break;
                    }
                }
                fclose(f);
            }
        }
        detail[strcspn(detail,"\r\n")]=0;
        wrap_detail(690,205,t->meta,2,white);
        if(detail[0])wrap_detail(690,250,detail,6,white);
        else if(!strcmp(t->kind,"movie")||!strcmp(t->kind,"tv")||
                !strcmp(t->kind,"game")||!strcmp(t->kind,"kiss"))
            text(690,260,"DESCRIPTION NOT CACHED",1,dim,28);
        text(690,354,t->url[0]?"[A] PLAY":t->kind[0]?"[A] OPEN":"NO STREAM YET",2,t->kind[0]||t->url[0]?green:dim,20);
        text(690,395,"[LB/RB] VIEW",1,dim,25);
        /* Aspect ratio is controlled only while video is playing. */
        snprintf(num,sizeof(num),"PAGE %d / %d",browse.mode?browse.page:page+1,browse.mode?listing_pages:(total+5)/6);text(690,455,num,1,dim,25);
    }
    if(bios_archive_confirm){rect(190,210,680,110,green,0);text(207,225,"ARCHIVE BIOS / COPYRIGHT WARNING",2,green,45);text(207,259,"CONFIRM RIGHTS AND SOURCE BEFORE INSTALL",1,white,75);text(207,289,"A DOWNLOAD / B OR X CANCEL",2,green,42);}
    if(catalog_pid&&!download_background){
        int transfer=pending.mode==19||pending.mode==16;
        rect(190,209,680,112,background?(SDL_Color){248,250,249,255}:(SDL_Color){0,0,0,255},1);
        rect(190,209,680,112,green,0);
        if(transfer){
            long long done=0,size=0;
            FILE *progress=fopen("catalog-cache/download-progress","r");
            if(progress){if(fscanf(progress,"%lld %lld",&done,&size)!=2){done=0;size=0;}fclose(progress);}
            char label[112];
            if(size>0){
                if(done<0)done=0;
                if(done>size)done=size;
                snprintf(label,sizeof(label),"DOWNLOADING %d%%  %lld / %lld KB",(int)(done*100/size),done/1024,size/1024);
                rect(210,259,636,14,dim,1);
                rect(210,259,(int)(636*done/size),14,green,1);
            }else{
                snprintf(label,sizeof(label),"DOWNLOADING  %lld KB / SIZE UNKNOWN",done/1024);
                rect(210,259,636,14,dim,0);
            }
            text(212,226,label,2,green,70);
            text(212,287,"X HIDE PROGRESS / B CANCEL",1,green,65);
        }else text(212,250,auto_active?"FINDING A WORKING SERVER... B CANCEL":"LOADING... B TO CANCEL",2,green,50);
    }
    rect(30,489,900,1,dim,1);text(30,509,status,1,white,92);text(713,509,"DPAD MOVE / B BACK",1,green,30);
    SDL_RenderPresent(renderer);
}
static void play(void) {
    if(settings_on||about||!total||player_pid)return;
    restart_position=auto_resume=0;
    Title *t=&titles[visible[selection]];
#ifdef FLXTR_DESKTOP
    if(!strcmp(t->kind,"core-manager")){Browse next={35,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"core-download")){
        Browse next={36,browse.page,0,0,0,0,"",""};
        snprintf(next.query,sizeof(next.query),"%.64s",t->url);
        request_catalog(next,0,0);return;
    }
#endif
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
    if(!strcmp(t->kind,"downloads-folder")){Browse next={28,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"download-zip")){Browse next={26,1,t->id,1,0,0,"",""};request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"archive-folder")){
        Browse next={t->id==8?9:31,1,t->id,0,0,0,"",""};
        snprintf(next.name,sizeof(next.name),"%s",t->title);
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"scummvm-launch")){
        FILE *f=fopen("game-request","w");
        if(!f){snprintf(status,sizeof(status),"COULD NOT START SCUMMVM");return;}
        fprintf(f,"0 -1 -1\n");
        if(fclose(f)){snprintf(status,sizeof(status),"COULD NOT START SCUMMVM");return;}
        game_exit=1;running=0;return;
    }
    if(!strcmp(t->kind,"game-folder")){
        Browse next={22,1,t->id,0,0,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"rg-source")){Browse next={32,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"rg-platform")){Browse next={33,1,t->id,0,0,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"rg-game")){
        const char *prefix="https://www.romsgames.net";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)||strlen(t->url+n)>240)return;
        const char *path=t->url+n;size_t len=strlen(path);
        if(len<8||path[0]!='/'||path[len-1]!='/'||!strstr(path,"-rom-"))return;
        for(size_t i=1;i<len-1;i++)if(!(path[i]>='a'&&path[i]<='z')&&!(path[i]>='0'&&path[i]<='9')&&path[i]!='-')return;
        snprintf(rom_phone_path,sizeof(rom_phone_path),"%s",path);accounts_open(2);return;
    }
    if(!strcmp(t->kind,"game-curated")){Browse next={20,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!strcmp(t->kind,"game-platform")){
        Browse next={17,1,t->id,0,0,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"game-item")){
        const char *prefix="https://archive.org/details/";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)||strlen(t->url+n)>128){fprintf(stderr,"Archive game item URL invalid: %.180s\n",t->url);snprintf(status,sizeof(status),"INVALID ARCHIVE ITEM - SEE GREENLINK.LOG");return;}
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
        char name[513];size_t ni=0;const char *p=slash+1;
        while(*p){
            if(ni+1>=sizeof(name)){snprintf(status,sizeof(status),"ARCHIVE FILENAME EXCEEDS 512 BYTES");return;}
            if(*p=='%'&&isxdigit((unsigned char)p[1])&&isxdigit((unsigned char)p[2])){
                char hex[3]={p[1],p[2],0};name[ni++]=(char)strtol(hex,NULL,16);p+=3;
            }else name[ni++]=*p++;
        }name[ni]=0;
        Browse next={19,1,t->id,0,0,0,"",""};
        int written=snprintf(next.query,sizeof(next.query),"%s|%s",item,name);
        if(written<0||written>=(int)sizeof(next.query)){snprintf(status,sizeof(status),"ROM FILENAME TOO LONG");return;}
        request_catalog(next,0,0);return;
    }
    if(!strcmp(t->kind,"archive-game")){Browse next={16,1,t->id,0,0,0,"",""};request_catalog(next,0,0);return;}
    if(!strcmp(t->kind,"game-zip")){
        Browse next={26,1,t->id,0,0,0,"",""};
        request_catalog(next,1,0);return;
    }
    if(!strcmp(t->kind,"zip-member")){
        Browse next={27,1,browse.id,browse.season,t->id,0,"",""};
        request_catalog(next,0,0);return;
    }
    if(!strcmp(t->kind,"game")){
        int back=-1,start=-1;
        for(int i=0;i<4;i++)if(pads[i]){SDL_GameControllerButtonBind b=SDL_GameControllerGetBindForButton(pads[i],SDL_CONTROLLER_BUTTON_BACK),a=SDL_GameControllerGetBindForButton(pads[i],SDL_CONTROLLER_BUTTON_START);if(b.bindType==SDL_CONTROLLER_BINDTYPE_BUTTON&&a.bindType==SDL_CONTROLLER_BINDTYPE_BUTTON){back=b.value.button;start=a.value.button;break;}}
        FILE *f=fopen("game-request","w");if(!f){snprintf(status,sizeof(status),"COULD NOT START GAME");return;}
        fprintf(f,"%d %d %d\n",t->id,back,start);if(fclose(f)){snprintf(status,sizeof(status),"COULD NOT START GAME");return;}game_exit=1;running=0;return;
    }
    if(!strcmp(t->kind,"archive")){
        const char *prefix="https://archive.org/details/";size_t n=strlen(prefix);
        if(strncmp(t->url,prefix,n)||strlen(t->url+n)>128){fprintf(stderr,"Archive video item URL invalid: %.180s\n",t->url);snprintf(status,sizeof(status),"INVALID ARCHIVE ITEM - SEE GREENLINK.LOG");return;}
        Browse next={10,1,0,0,0,0,"",""};strcpy(next.query,t->url+n);snprintf(next.name,sizeof(next.name),"%s",t->title);request_catalog(next,1,0);return;
    }
    if(!t->url[0]&&t->kind[0]){
        Browse next={0,1,t->id,t->season,t->episode,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);
        next.mode=!strcmp(t->kind,"kiss")?8:!strcmp(t->kind,"tv")?3:!strcmp(t->kind,"season")?4:!strcmp(t->kind,"server")?6:5;
        if(next.mode==6){snprintf(next.query,sizeof(next.query),"%.31s",t->title);snprintf(next.name,sizeof(next.name),"%s",browse.name);}
        if(next.mode==5){
            if(access("./greenlink-player",X_OK)){snprintf(status,sizeof(status),"PLAYER NOT INSTALLED");return;}
            auto_title=*t;auto_active=1;auto_count=auto_index=0;kiss_1080_pass=0;auto_request(next);
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
    auto_title=*t;auto_count=auto_index=0;kiss_1080_pass=0;auto_resume=restart_position=0;
    FILE *guide=fopen("catalog-cache/result.tsv","r");char guide_line[128];tv_air_start=(long long)time(NULL);
    if(guide){while(fgets(guide_line,sizeof(guide_line),guide))if(sscanf(guide_line,"# tv-start=%lld",&tv_air_start)==1)break;fclose(guide);}
    auto_active=1;
    Browse next={5,1,t->id,t->season,t->episode,0,"",""};snprintf(next.name,sizeof(next.name),"%s",t->title);auto_request(next);
}
static void tv_cancel_prepared(void){
    if(youtube_preparing){youtube_stop();youtube_preparing=0;}
    if(tv_gate>=0)player_pipe_close(tv_gate);
    tv_gate=-1;
    if(tv_buffer_control>=0)player_pipe_close(tv_buffer_control);
    tv_buffer_control=-1;
    if(tv_buffer_pid){kill(tv_buffer_pid,SIGKILL);waitpid(tv_buffer_pid,NULL,0);}tv_buffer_pid=0;tv_prepared=0;
}
static void tv_begin_prepared(void){
    if(!tv_prepared)return;
    tv_prepared=0;auto_active=1;auto_resume=0;playing_title=tv_next_title;
    player_pid=tv_buffer_pid;player_control=tv_buffer_control;tv_buffer_pid=0;tv_buffer_control=-1;
    bookmark_path(&playing_title,adaptive_title_key,sizeof(adaptive_title_key));
    adaptive_blocked=adaptive_pending=0;unlink("adaptive-ready");unlink("adaptive-request");
    tv_history_prepare(&playing_title);unlink("playback-started");unlink("playback-position");
    close_ui();char go='G';if(tv_gate>=0){ssize_t n=player_pipe_write(tv_gate,&go,1);(void)n;player_pipe_close(tv_gate);}tv_gate=-1;
    if(!strcmp(playing_title.kind,"youtube")){strcpy(youtube_audio,youtube_next_audio);strcpy(youtube_agent,youtube_next_agent);auto_active=0;return;}
    TvAiring a;if(tv_airing_read(&a)&&a.id==auto_title.id){a.start=(long long)time(NULL);tv_airing_save(&a);}
}
static void tv_stage_source(const Title *source){
    tv_next_title=*source;tv_preloading=1;auto_resume=0;launch_player(source);tv_preloading=0;
    if(!player_pid){tv_break_due=0;auto_active=0;tv_cancel_prepared();return;}
    tv_buffer_pid=player_pid;tv_buffer_control=player_control;player_pid=0;player_control=-1;
    tv_prepared=1;auto_active=0;tv_break_due=0;
    Browse ad={13,1,tv_sequence("tv-break-sequence.txt"),0,0,0,"",""};request_catalog(ad,0,0);
    if(!catalog_pid)tv_begin_prepared();
}
static void tv_tick(void){
    if(!tv_active||youtube_pid||catalog_pid||player_pid||auto_active||settings_on||tv_schedule_on||(Sint32)(SDL_GetTicks()-tv_next_at)<0)return;
    if(tv_attempts++>=12){tv_active=0;tv_cancel_prepared();snprintf(status,sizeof(status),"TV: NO WORKING SOURCES / A RETRY");return;}
    time_t now=time(NULL);int block=tv_block_at(tv_blocks,tv_hour());long long anchor=(long long)tv_block_start(tv_blocks,now);
    if(mtv_override||tv_blocks[block].genre==TV_MUSIC_VIDEOS){
        char query[221];mtv_query(query,sizeof(query));
        if(youtube_spawn(query,1))tv_active=0;
        return;
    }
    int forced=tv_blocks[block].marathon&&tv_last_block==anchor?tv_last_title.id:0;
    int seed=tv_joining?(int)(anchor/3600)%100000+tv_attempts-1:tv_sequence("tv-sequence.txt");
    Browse next={12,1,seed,forced,0,0,"",""};
    snprintf(next.query,sizeof(next.query),"%d:%lld:%lld:%d:%d:%d",tv_blocks[block].genre,anchor,(long long)now,tv_joining,tv_blocks[block].marathon,tv_breaks);
    request_catalog(next,0,0);tv_next_at=SDL_GetTicks()+10000;
}
static void bookmark_path(const Title *t,char *path,size_t capacity){
    unsigned long hash=2166136261UL;
    const char *parts[]={t->kind,t->title};
    for(size_t i=0;i<2;i++)for(const unsigned char *p=(const unsigned char *)parts[i];*p;p++)
        hash=((hash^*p)*16777619UL)&0xffffffffUL;
    hash=((hash^(unsigned)t->id)*16777619UL)&0xffffffffUL;
    hash=((hash^(unsigned)t->season)*16777619UL)&0xffffffffUL;
    hash=((hash^(unsigned)t->episode)*16777619UL)&0xffffffffUL;
    snprintf(path,capacity,"%08lx",hash);
}
static Title tv_history_title;
static int tv_history_active,tv_history_confirmed;
static double tv_history_position;
static char tv_history_key[96],tv_history_token[16];
static void tv_history_prepare(const Title *t){
    char token[16],key[96];bookmark_path(t,token,sizeof(token));
    if(!strcmp(t->kind,"episode"))snprintf(key,sizeof(key),"episode:%d:%d:%d",t->id,t->season,t->episode);
    else if(!strcmp(t->kind,"youtube")&&!strncmp(t->meta,"https://www.youtube.com/watch?v=",32)&&
            strlen(t->meta+32)==11&&strspn(t->meta+32,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==11)
        snprintf(key,sizeof(key),"youtube:%s",t->meta+32);
    else snprintf(key,sizeof(key),"%.15s:%s",t->kind,token);
    if(tv_history_active&&!strcmp(tv_history_key,key))return; /* seek/quality retry */
    tv_history_title=*t;tv_history_active=1;tv_history_confirmed=0;tv_history_position=0;
    snprintf(tv_history_key,sizeof(tv_history_key),"%s",key);snprintf(tv_history_token,sizeof(tv_history_token),"%s",token);
}
static void tv_history_poll(void){
    if(!tv_history_active)return;
    FILE *f=fopen("playback-started","r");if(!f)return;
    char token[16];double position=-1;
    int good=fscanf(f,"%15s %lf",token,&position)==2&&!strcmp(token,tv_history_token)&&position>=0&&position<=86400;
    fclose(f);unlink("playback-started");if(!good)return;
    tv_history_position=position;
    if(tv_history_confirmed)return;
    tv_history_confirmed=1;
    if(!tv_play_append("started",tv_history_title.kind,tv_history_title.id,tv_history_title.season,
                       tv_history_title.episode,position,tv_history_key,tv_history_title.title))
        fprintf(stderr,"TV history: could not append playback start\n");
}
static double tv_history_last_position(void){
    double position=tv_history_position;FILE *f=fopen("playback-position","r");
    if(f){double p=-1;if(fscanf(f,"%lf",&p)==1&&p>=0&&p<=86400)position=p;fclose(f);}
    return position;
}
static void tv_history_finish(const char *event,double position){
    tv_history_poll();
    if(tv_history_active&&tv_history_confirmed&&!tv_play_append(event,tv_history_title.kind,tv_history_title.id,
       tv_history_title.season,tv_history_title.episode,position,tv_history_key,tv_history_title.title))
        fprintf(stderr,"TV history: could not append playback outcome\n");
    tv_history_active=tv_history_confirmed=0;
}
static void launch_player(const Title *t) {
    if(!strcmp(t->kind,"youtube")){
        if(youtube_audio[0])setenv("FLXTR_EXTERNAL_AUDIO",youtube_audio,1);else unsetenv("FLXTR_EXTERNAL_AUDIO");
        setenv("FLXTR_VIDEO_USER_AGENT",youtube_agent,1);
    }else{unsetenv("FLXTR_EXTERNAL_AUDIO");unsetenv("FLXTR_VIDEO_USER_AGENT");}
    if(prefetch_pid){kill(prefetch_pid,SIGKILL);waitpid(prefetch_pid,NULL,0);prefetch_pid=0;}prefetch_left=0;
    if(access("./greenlink-player",X_OK)) { snprintf(status,sizeof(status),"PLAYER NOT BUILT - RUN SCRIPTS/BUILD-PLAYER.SH");return; }
#ifdef _WIN32
    FlxtrPipe controls={-1,-1},gate={-1,-1};
    if(flxtr_pipe_create(&controls)||(tv_preloading&&flxtr_pipe_create(&gate))){
        flxtr_pipe_close(controls.reader);flxtr_pipe_close(controls.writer);
        snprintf(status,sizeof(status),"PLAYER CONTROL PIPE FAILED");return;
    }
    FlxtrPipeHandle inherited_control=flxtr_pipe_export(controls.reader);
    FlxtrPipeHandle inherited_gate=tv_preloading?flxtr_pipe_export(gate.reader):-1;
    if(inherited_control<0||(tv_preloading&&inherited_gate<0)){
        flxtr_pipe_close(inherited_control);flxtr_pipe_close(inherited_gate);
        flxtr_pipe_close(controls.reader);flxtr_pipe_close(controls.writer);
        flxtr_pipe_close(gate.reader);flxtr_pipe_close(gate.writer);return;
    }
    if(!tv_preloading){unlink("playback-request");unlink("playback-position");}
#else
    int gate[2]={-1,-1};if(tv_preloading&&pipe(gate))return;
    int controls[2];if(pipe(controls)){if(gate[0]>=0){close(gate[0]);close(gate[1]);}snprintf(status,sizeof(status),"PLAYER CONTROL PIPE FAILED");return;}
    fcntl(controls[1],F_SETFL,O_NONBLOCK);fcntl(controls[1],F_SETFD,FD_CLOEXEC);
    signal(SIGPIPE,SIG_IGN);if(!tv_preloading){unlink("playback-request");unlink("playback-position");}
#endif
    if(auto_active)restart_position=auto_resume;
    /* Resume only an explicit bookmark for this title, not TV channel output. */
    char bookmark_key[16],bookmark_file[64];
    bookmark_path(t,bookmark_key,sizeof(bookmark_key));
    if(!tv_preloading&&strcmp(adaptive_title_key,bookmark_key)){
        snprintf(adaptive_title_key,sizeof(adaptive_title_key),"%s",bookmark_key);
        adaptive_blocked=adaptive_pending=0;
    }
    if(!tv_preloading){unlink("adaptive-request");unlink("adaptive-ready");}
    snprintf(bookmark_file,sizeof(bookmark_file),"bookmarks/%s.txt",bookmark_key);
    if(!tv_active&&strcmp(t->kind,"youtube")&&restart_position==0){
        FILE *f=fopen(bookmark_file,"r");
        if(f){double saved=0;if(fscanf(f,"%lf",&saved)==1&&saved>=0&&saved<=86400)restart_position=saved;fclose(f);}
    }
    if(tv_active&&!tv_preloading)tv_history_prepare(t);
    if(!tv_preloading)unlink("playback-started");
    playing_title=*t;player_menu=1; /* Native controller menu is always available. */
    subtitle_route_log("launch-player",t);
    music_video_pause(0);
    if(!tv_preloading)close_ui();
#ifndef _WIN32
    player_pid=shell_fork();
    if(player_pid==0) {
        close(controls[1]);
        /* This one descriptor is intentional IPC, unlike inherited GPU handles. */
        if(fcntl(controls[0],F_SETFD,0)<0)_exit(126);
        if(tv_preloading){close(gate[1]);if(fcntl(gate[0],F_SETFD,0)<0)_exit(126);}
        signal(SIGUSR1,SIG_IGN); /* Ignore a controller press before exec is ready. */
        int log=open(tv_preloading?"tv-preload.log":"player.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log>=0){dup2(log,STDERR_FILENO);dup2(log,STDOUT_FILENO);close(log);}
#else
    {
#endif
        const char *modes[]={"fit","stretch","pixel"};
        /* Keep externally supplied caption URLs separate from media URLs.
         * The player chooses a track according to the subtitle setting. */
        if(t->subs_eng[0])setenv("FLXTR_SUBTITLE_ENGLISH",t->subs_eng,1);
        else unsetenv("FLXTR_SUBTITLE_ENGLISH");
        if(t->subs_spa[0])setenv("FLXTR_SUBTITLE_SPANISH",t->subs_spa,1);
        else unsetenv("FLXTR_SUBTITLE_SPANISH");
        if(t->subs_nor[0])setenv("FLXTR_SUBTITLE_NORWEGIAN",t->subs_nor,1);
        else unsetenv("FLXTR_SUBTITLE_NORWEGIAN");
        /* Metadata is passed to the video child via environment, never via
         * the video URL. Prefer parent-series synopsis for an episode. */
        setenv("FLXTR_VIDEO_TITLE",t->title,1);
        setenv("FLXTR_VIDEO_KIND",t->kind,1);
        char auto_mask[16];snprintf(auto_mask,sizeof(auto_mask),"%d",tv_preloading?0:adaptive_blocked);
        setenv("FLXTR_ADAPTIVE_BLOCKED",auto_mask,1);
        setenv("FLXTR_ADAPTIVE_RESOLVE",!strcmp(t->kind,"youtube")&&t->meta[0]?"1":"0",1);
        char info_path[128],info_text[512]="";
        const char *info_kind=t->kind;
        if(!strcmp(info_kind,"episode"))info_kind="tv";
        if(!strcmp(info_kind,"kiss-episode"))info_kind="anime";
        if((!strcmp(info_kind,"movie")||!strcmp(info_kind,"tv")||
            !strcmp(info_kind,"anime"))&&t->id>0){
            snprintf(info_path,sizeof(info_path),"media-info-cache/%s-%d.txt",info_kind,t->id);
            FILE *info=fopen(info_path,"r");
            if(info){if(!fgets(info_text,sizeof(info_text),info))info_text[0]=0;fclose(info);}
        }
        info_text[strcspn(info_text,"\r\n")]=0;
        setenv("FLXTR_VIDEO_SYNOPSIS",info_text,1);
        setenv("FLXTR_BOOKMARK_KEY",bookmark_key,1);
        if(!strcmp(t->kind,"kiss-episode"))setenv("FLXTR_MEDIA_PROVIDER","megaplay",1);
        else unsetenv("FLXTR_MEDIA_PROVIDER");
        char height[16],buffer[16],disk[16],scale[16],delay[16],control[32],start[32];
        int requested_height=quality_setting==3?adaptive_height:(t->height?t->height:qualities[quality_setting]);
        if(auto_active&&!strcmp(t->kind,"kiss-episode")&&kiss_1080_pass&&quality_setting!=3)requested_height=1080;
        /* Honor the selected TV target, including 1080p. Older players can
         * still request a higher-resolution retry for a missing rendition. */
        if(tv_active){
            int preferred=quality_height();

            requested_height=tv_quality_retry>=2?1080:tv_quality_retry==1?720:preferred;
        }
#ifdef _WIN32
        snprintf(control,sizeof(control),"%" PRIuPTR,(uintptr_t)inherited_control);
#else
        snprintf(control,sizeof(control),"%d",controls[0]);
#endif
        snprintf(start,sizeof(start),"%.3f",restart_position);snprintf(height,sizeof(height),"%d",requested_height);
        snprintf(buffer,sizeof(buffer),"%d",buffer_seconds[buffer_setting]);snprintf(disk,sizeof(disk),"%d",disk_megabytes[disk_setting]);
        snprintf(scale,sizeof(scale),"%d",subtitle_scale);snprintf(delay,sizeof(delay),"%d",subtitle_delay);
#ifndef _WIN32
        if(!player_menu)execl("./greenlink-player","greenlink-player",t->url,"--view",modes[viewing],"--height",height,"--buffer-seconds",buffer,"--buffer-mb",disk,"--subtitles",subtitle_languages[subtitle_setting],"--subtitle-size",scale,"--subtitle-delay",delay,(char*)NULL);
#endif
        char gate_arg[32];
#ifdef _WIN32
        snprintf(gate_arg,sizeof(gate_arg),"%" PRIuPTR,(uintptr_t)inherited_gate);
        const char *control_option="--control-handle",*gate_option="--preload-handle";
#else
        snprintf(gate_arg,sizeof(gate_arg),"%d",gate[0]);
        const char *control_option="--control-fd",*gate_option="--preload-fd";
#endif
        char *args[30]={"greenlink-player",(char*)t->url,"--view",(char*)modes[viewing],"--height",height,"--buffer-seconds",buffer,"--buffer-mb",disk,"--subtitles",(char*)subtitle_languages[subtitle_setting],"--subtitle-size",scale,"--subtitle-delay",delay,(char*)control_option,control,"--start",start,NULL,NULL,NULL};
        int n=20;
#ifndef FLXTR_DESKTOP
        if(quality_setting==3)args[n++]="--adaptive";
#endif
        if(tv_preloading){args[n++]=(char*)gate_option;args[n++]=gate_arg;}if(tv_in_break)args[n++]="120";if(tv_active||!strcmp(t->kind,"episode")||!strcmp(t->kind,"kiss-episode"))args[n++]="--episodes";if(auto_active)args[n++]="--servers";
#ifdef FLXTR_APP_TEST
        args[n++]="--headless";
#endif
        args[n]=NULL;
#ifdef _WIN32
        uintptr_t pass[2]={(uintptr_t)inherited_control,(uintptr_t)inherited_gate};
        const char *log=tv_preloading?"tv-preload.log":"player.log";
        player_pid=flxtr_win_spawn("./greenlink-player",(const char *const*)args,log,log,pass,tv_preloading?2:1,0);
#else
        execv("./greenlink-player",args);_exit(127);
#endif
    }
#ifdef _WIN32
    flxtr_pipe_close(inherited_control);flxtr_pipe_close(inherited_gate);
    flxtr_pipe_close(controls.reader);flxtr_pipe_close(gate.reader);
    player_control=controls.writer;
    if(tv_preloading)tv_gate=gate.writer;
    restart_position=0;
#else
    if(tv_preloading){close(gate[0]);tv_gate=gate[1];fcntl(tv_gate,F_SETFD,FD_CLOEXEC);}
    close(controls[0]);player_control=controls[1];restart_position=0;
#endif
    if(player_pid<0){player_pipe_close(player_control);player_control=-1;player_pid=0;if(!tv_preloading&&open_ui())running=0;snprintf(status,sizeof(status),"COULD NOT START PLAYER");}
    else snprintf(status,sizeof(status),"PLAYING - B TO STOP");
}
static int adaptive_lookup_fallback(void){
    if(quality_setting!=3||!adaptive_pending)return 0;
    adaptive_pending=0;
    adaptive_blocked|=adaptive_height<=480?7:adaptive_height<=720?6:4;
    adaptive_height=adaptive_previous;tv_cancel_prepared();
    restart_position=auto_resume=adaptive_position;playing_title.height=0;
    fprintf(stderr,"Auto quality: resolver failed; retaining previous %dp stream\n",adaptive_height);
    launch_player(&playing_title);return 1;
}
static void series_advance(int direction){
    auto_active=0;restart_position=auto_resume=0;
    if(!strcmp(playing_title.kind,"episode")){
        Browse next={34,1,playing_title.id,playing_title.season,playing_title.episode,0,"",""};
        strcpy(next.query,direction<0?"previous":"next");play_after_load=1;request_catalog(next,0,0);return;
    }
    if(!strcmp(playing_title.kind,"kiss-episode")&&browse.mode==8){
        int next=selection+direction;
        if(next>=0&&next<total){selection=next;play();return;}
        int page=browse.page+direction;
        if(page>=1&&page<=listing_pages){play_after_load=1;next_catalog_page(direction);return;}
    }
    snprintf(status,sizeof(status),"END OF AVAILABLE EPISODES");
}
static void finish_player(int code){
    tv_history_poll();
    int cancelled=stopping_player;
    player_pid=0;stopping_player=0;if(player_control>=0)player_pipe_close(player_control);player_control=-1;
    if(open_ui()){fprintf(stderr,"Restore UI: %s\n",SDL_GetError());running=0;auto_active=0;return;}
    if(!tv_active||strcmp(playing_title.kind,"youtube"))music_video_pause(1);
    if(!cancelled&&quality_setting==3&&WIFEXITED(code)&&WEXITSTATUS(code)==46){
        int target=0,blocked=0,actual=0,reason=0;double position=-1;
        FILE *f=fopen("adaptive-request","r");
        int good=f&&fscanf(f,"%d %lf %d %d %d",&target,&position,&blocked,&actual,&reason)==5;
        if(f)fclose(f);
        unlink("adaptive-request");
        if(!good||(target!=480&&target!=720&&target!=1080)||!(position>=0&&position<=86400)||
           blocked<0||blocked>7||actual<1||actual>1080||reason<1||reason>4){
            tv_history_finish("failed",tv_history_last_position());
            tv_active=auto_active=0;tv_cancel_prepared();snprintf(status,sizeof(status),"INVALID AUTO QUALITY REQUEST");return;
        }
        adaptive_previous=actual<=480?480:actual<=720?720:1080;
        adaptive_height=target;adaptive_blocked=blocked;adaptive_position=position;adaptive_pending=1;
        tv_cancel_prepared();tv_quality_retry=0;playing_title.height=0;
        fprintf(stderr,"Auto quality: %dp -> %dp at %.3f sec (%s)\n",actual,target,position,
            reason==1?"buffer starvation":reason==2?"decoder stalls":reason==3?"audio underruns":"stable playback");
        if(!strcmp(playing_title.kind,"youtube")&&playing_title.meta[0]){
            if(!youtube_spawn(playing_title.meta,0)){youtube_for_tv=tv_active;youtube_resume=position;}
            return;
        }
        restart_position=auto_resume=position;launch_player(&playing_title);return;
    }
    if(adaptive_pending){
        int failed=!cancelled&&WIFEXITED(code)&&WEXITSTATUS(code)!=0&&WEXITSTATUS(code)!=40&&access("adaptive-ready",F_OK);
        adaptive_pending=0;
        if(failed){
            adaptive_blocked|=adaptive_height<=480?7:adaptive_height<=720?6:4;
            adaptive_height=adaptive_previous;tv_cancel_prepared();playing_title.height=0;tv_quality_retry=0;
            fprintf(stderr,"Auto quality: switch failed; reverting to %dp\n",adaptive_height);
            if(!strcmp(playing_title.kind,"youtube")&&playing_title.meta[0]){
                if(!youtube_spawn(playing_title.meta,0)){youtube_for_tv=tv_active;youtube_resume=adaptive_position;}
                return;
            }
            restart_position=auto_resume=adaptive_position;launch_player(&playing_title);return;
        }
    }
    if(!cancelled&&WIFEXITED(code)&&WEXITSTATUS(code)==40){
        int command,view,sub,scale,delay;double position;FILE *f=fopen("playback-request","r");
        int valid=f&&fscanf(f,"%d %lf %d %d %d %d",&command,&position,&view,&sub,&scale,&delay)==6;
        if(f)fclose(f);
        unlink("playback-request");
        if(!valid||command<1||command>10||!(position>=0&&position<=86400)||view<0||view>=VIEW_COUNT||sub<0||sub>4||scale<2||scale>3||delay< -5||delay>5){auto_active=0;tv_history_finish("failed",tv_history_last_position());snprintf(status,sizeof(status),"INVALID PLAYBACK REQUEST");return;}
        if(command>=5)tv_history_finish(command==5||command==6?"skipped":"stopped",position);
        if(command==10){tv_active=auto_active=0;running=0;return;}
        viewing=view;subtitle_setting=sub;subtitle_scale=scale;subtitle_delay=delay;save_settings();
        /* TV is a channel, not a resume-based episode player. */
        if(command==7){tv_cancel_prepared();tv_active=0;auto_active=0;tv_in_break=tv_break_due=0;music_video_pause(1);snprintf(status,sizeof(status),"PLAYBACK STOPPED");return;}
        if(command==9){
            auto_active=0;tv_active=0;
            snprintf(status,sizeof(status),"FROZEN TO BACKGROUND / REOPEN TITLE TO RESUME");
            return;
        }
        if(command==8){
            auto_active=0;tv_active=0;
            if(!strcmp(playing_title.kind,"episode")){Browse next={4,1,playing_title.id,playing_title.season,0,0,"",""};request_catalog(next,0,0);return;}
            if(browse.mode==4||browse.mode==8){
                snprintf(status,sizeof(status),"EPISODE BROWSER / A SELECT / B SEASONS");
            }else snprintf(status,sizeof(status),"EPISODE LIST UNAVAILABLE FOR THIS SOURCE");
            return;
        }
        if(command<=3){
            if(command>1){quality_setting=(quality_setting+(command==2?1:QUALITY_CHOICES-1))%QUALITY_CHOICES;playing_title.height=0;save_settings();}
            if(command>1&&!strcmp(playing_title.kind,"youtube")&&playing_title.meta[0]){
                tv_cancel_prepared();
                if(!youtube_spawn(playing_title.meta,0)){youtube_for_tv=tv_active;youtube_resume=position;}
                return;
            }
            restart_position=auto_resume=position;
            if(auto_active&&!strcmp(playing_title.kind,"kiss-episode"))auto_next();
            else launch_player(&playing_title);
            return;
        }
        if(command==4){if(auto_active){restart_position=auto_resume=position;auto_index++;auto_next();}else snprintf(status,sizeof(status),"NO ALTERNATE SERVER FOR LOCAL VIDEO");return;}
        if(tv_active&&(command==5||command==6)){tv_cancel_prepared();auto_active=0;tv_in_break=tv_break_due=0;tv_attempts=0;tv_next_at=0;return;}
        series_advance(command==5?-1:1);return;
    }
    tv_history_finish(!cancelled&&WIFEXITED(code)&&!WEXITSTATUS(code)?"completed":cancelled?"stopped":"failed",tv_history_last_position());
    if(!strcmp(playing_title.kind,"youtube")){
        int eof=!cancelled&&WIFEXITED(code)&&!WEXITSTATUS(code);
        if(!eof){tv_active=0;tv_cancel_prepared();}
        if(eof&&tv_active&&tv_prepared){tv_begin_prepared();return;}
        if(!tv_active)music_video_pause(1);
        tv_attempts=0;tv_next_at=0;tv_break_due=0;
        snprintf(status,sizeof(status),eof?"MUSIC VIDEO FINISHED":"YOUTUBE PLAYBACK STOPPED / SEE PLAYER.LOG");return;
    }
    if(tv_active&&!tv_in_break&&!cancelled&&WIFEXITED(code)&&WEXITSTATUS(code)==45&&tv_quality_retry<2){
        int current=tv_quality_retry>=1?720:quality_height();
        tv_quality_retry=current<720?1:2;
        fprintf(stderr,"TV source has no compatible %dp track; retrying at %dp\n",current,tv_quality_retry==1?720:1080);
        launch_player(&playing_title);
        return;
    }
    tv_quality_retry=0;
    /* A finished or interrupted TV episode never creates a resume checkpoint. */
    if(tv_in_break){
        tv_in_break=0;tv_attempts=0;tv_next_at=0;
        if(cancelled){tv_active=0;tv_cancel_prepared();return;}
        if(tv_prepared){tv_begin_prepared();return;}
        snprintf(status,sizeof(status),"BACK TO TV / SELECTING NEXT PROGRAM");return;
    }
    if(WIFEXITED(code)&&WEXITSTATUS(code)==44){auto_active=0;snprintf(status,sizeof(status),"SERVER CANNOT SEEK - SELECT TITLE TO RESTART");return;}
    if(auto_active&&!cancelled&&(!WIFEXITED(code)||WEXITSTATUS(code))){auto_index++;auto_next();return;}
    auto_active=0;
    if(series_autoplay&&!tv_active&&!cancelled&&WIFEXITED(code)&&!WEXITSTATUS(code)&&
       (!strcmp(playing_title.kind,"episode")||!strcmp(playing_title.kind,"kiss-episode"))){series_advance(1);return;}
    if(tv_active&&!cancelled&&WIFEXITED(code)&&!WEXITSTATUS(code)){
        tv_last_title=auto_title;TvAiring aired;if(tv_airing_read(&aired))tv_last_block=aired.block;
        tv_progress_save(auto_title.id,auto_title.season,auto_title.episode);unlink("tv-resume.tsv");tv_attempts=0;tv_next_at=0;tv_break_due=tv_breaks;
        fprintf(stderr,"TV completed %d S%d E%d\n",auto_title.id,auto_title.season,auto_title.episode);
    }
    snprintf(status,sizeof(status),cancelled?"PLAYBACK STOPPED":WIFEXITED(code)&&!WEXITSTATUS(code)?"PLAYBACK FINISHED":"PLAYER STOPPED - SEE PLAYER.LOG");
}
static int phone_dispatch(char *reply,size_t size){
    int code=200;
    snprintf(reply,size,"Playback requested. Check FlXtR for playback errors.");
    if(!strcmp(phone_pending,"control")){
        char c=phone_value[0];c=c=='p'?'P':c=='f'?'F':c=='r'?'R':c=='s'?'S':c;
        if(youtube_pid&&!youtube_preparing&&c=='S'){
            youtube_stop();tv_active=0;music_video_pause(1);snprintf(reply,size,"YouTube lookup cancelled.");
        }else if(player_pid&&player_control>=0){
            if(player_pipe_write(player_control,&c,1)!=1){code=503;snprintf(reply,size,"Player control is unavailable.");}
            else snprintf(reply,size,"Playback control sent.");
        }else if(music_job==3&&(c=='P'||c=='S')){
            if(c=='S'){music_stop();snprintf(music_message,sizeof(music_message),"MUSIC STOPPED");}
            else{kill(music_pid,SIGUSR1);music_paused=!music_paused;}
            snprintf(reply,size,c=='S'?"Music stopped.":"Music pause toggled.");
        }else{code=409;snprintf(reply,size,"No active player supports this control.");}
    }else if(!strcmp(phone_pending,"music")){
        if(!player_pid&&!catalog_pid&&!update_pid&&!youtube_pid){
            snprintf(music_query,sizeof(music_query),"%.220s",phone_value);
            music_edit=0;music_on=1;music_phone_play=1;music_spawn(1);
            if(!music_pid){code=503;music_phone_play=0;snprintf(reply,size,"Music worker could not start.");}
            else snprintf(reply,size,"Searching YouTube; the first matching track will play automatically.");
        }else{code=409;snprintf(reply,size,"FlXtR is busy. Stop playback or wait for the current operation.");}
    }else if(!player_pid&&!catalog_pid&&!update_pid&&!youtube_pid){
        if(!strcmp(phone_pending,"url")&&youtube_link(phone_value)){
            tv_active=auto_active=0;
            code=youtube_spawn(phone_value,0)?503:200;
            snprintf(reply,size,code==200?"Resolving YouTube video for FlXtR.":"YouTube worker could not start.");
            phone_pending[0]=phone_value[0]=0;return code;
        }
        Title t;memset(&t,0,sizeof(t));snprintf(t.title,sizeof(t.title),"PHONE VIDEO");
        if(!strcmp(phone_pending,"play"))snprintf(t.url,sizeof(t.url),"phone-files/%.100s",phone_value);
        else snprintf(t.url,sizeof(t.url),"%s",phone_value);
        snprintf(t.subs_eng,sizeof(t.subs_eng),"%s",phone_subtitle);
        if(phone_subtitle[0])subtitle_setting=2;
        auto_active=tv_active=0;restart_position=0;launch_player(&t);
        if(!player_pid){code=503;snprintf(reply,size,"Player could not start. Check player.log on FlXtR.");}
    }else{code=409;snprintf(reply,size,"FlXtR is busy. Stop playback or wait for the current operation.");}
    phone_pending[0]=phone_value[0]=0;
    return code;
}
static void action(SDL_Keycode key) {
    if(samba_password_on){samba_password_action(key);return;}
    if(youtube_pid&&!youtube_preparing){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){youtube_stop();tv_active=0;music_video_pause(1);snprintf(status,sizeof(status),"YOUTUBE LOOKUP CANCELLED");}
        return;
    }
    if(music_on){music_action(key);return;}
    if(key==SDLK_m&&!player_pid&&!catalog_pid&&!update_pid&&!settings_on&&!about&&!accounts_on&&!search_on){
        if(!access("assets/music-required-v1",R_OK)&&(access("greenlink-audio",X_OK)||access("tools/music.py",R_OK)||access(
#ifdef _WIN32
        "music-runtime/bin/python.exe"
#elif defined(FLXTR_DESKTOP)
        "scripts/music-runtime.sh"
#else
        "music-runtime/usr/bin/python3"
#endif
        ,X_OK))){start_update();return;}
        tv_active=0;tv_cancel_prepared();music_on=1;return;
    }
    if(favorite_remove_confirm){
        if(key==SDLK_RETURN&&browse.mode==FAVORITES_MODE&&total){
            favorite_remove_confirm=0;
            favorites_toggle();
        }else if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F2||key==SDLK_F9){
            favorite_remove_confirm=0;
            snprintf(status,sizeof(status),"FAVORITE REMOVAL CANCELLED");
        }
        return;
    }
    if(key==SDLK_F9&&browse.mode==FAVORITES_MODE&&!player_pid&&!catalog_pid&&
       !settings_on&&!about&&!search_on&&total){
        favorite_remove_confirm=1;
        return;
    }
    if(accounts_on&&!player_pid){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5){api_setup_close();accounts_on=0;
            if(party_entry==2||phone_rom_uploaded){phone_rom_uploaded=0;memset(library_pages,0,sizeof(library_pages));Browse next={14,1,1,0,0,0,"",""};request_catalog(next,1,0);}
        }
        else if(key==SDLK_RETURN)accounts_open(party_entry);
        return;
    }
    if(rom_core_menu){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){rom_core_menu=0;return;}
        if(key==SDLK_UP&&rom_core_selected>0)rom_core_selected--;
        if(key==SDLK_DOWN&&rom_core_selected+1<rom_core_count)rom_core_selected++;
        if(rom_core_selected<rom_core_first)rom_core_first=rom_core_selected;
        if(rom_core_selected>=rom_core_first+7)rom_core_first=rom_core_selected-6;
        if(key==SDLK_RETURN){rom_core_save();rom_core_menu=0;snprintf(status,sizeof(status),"ROM CORE SAVED");}
        return;
    }
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
            if(key==SDLK_i||key==SDLK_y||key==SDLK_v){
#ifdef _WIN32
                char menu='m';if(player_control>=0)player_pipe_write(player_control,&menu,1);
#else
                kill(player_pid,SIGUSR1);
#endif
            }
            if((key==SDLK_ESCAPE||key==SDLK_BACKSPACE)&&!stopping_player){auto_active=0;kill(player_pid,SIGTERM);stopping_player=1;stop_time=SDL_GetTicks();}
            return;
        }
        char command=key==SDLK_F2?'n':key==SDLK_i?'m':key==SDLK_ESCAPE||key==SDLK_BACKSPACE?'b':key==SDLK_UP?'u':key==SDLK_DOWN?'d':key==SDLK_LEFT?'l':key==SDLK_RIGHT?'r':key==SDLK_RETURN?'a':key==SDLK_y||key==SDLK_v?'v':0;
        if(command&&player_control>=0){ssize_t sent=player_pipe_write(player_control,&command,1);(void)sent;}
        return;
    }
    if(catalog_pid){
        if(key==SDLK_F2&&(pending.mode==19||pending.mode==16)){download_background=1;snprintf(status,sizeof(status),"DOWNLOAD RUNNING / B CANCEL");return;}
        if(key==SDLK_F2&&auto_active){
            kill(catalog_pid,SIGKILL);waitpid(catalog_pid,NULL,0);catalog_pid=0;
            if(pending.mode==6){auto_index++;auto_next();}
            else {auto_active=0;snprintf(status,sizeof(status),"SERVER DISCOVERY SKIPPED");}
            return;
        }
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F2){
            kill(catalog_pid,SIGKILL);waitpid(catalog_pid,NULL,0);catalog_pid=0;
            tv_cancel_prepared();if(!renderer)open_ui();auto_active=0;tv_active=0;play_after_load=0;
            snprintf(status,sizeof(status),key==SDLK_F2?"LOAD SKIPPED":"LOAD CANCELLED");
        }
        return;
    }
    if(search_on){
        size_t n=strlen(search_text);
        if(key==SDLK_ESCAPE){search_on=0;SDL_StopTextInput();}
        else if(key==SDLK_BACKSPACE){if(n)search_text[n-1]=0;else {search_on=0;SDL_StopTextInput();}}
        else if(key==SDLK_TAB){
            const char *completion=search_completion();
            if(completion)snprintf(search_text,sizeof(search_text),"%s",completion);
        }
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
        if(key==SDLK_RETURN)tv_column=(tv_column+1)%3;
        int step=key==SDLK_LEFT?-1:1;
        if(key==SDLK_LEFT||key==SDLK_RIGHT){
            if(tv_column==2)tv_blocks[tv_row].marathon=!tv_blocks[tv_row].marathon;
            else if(tv_column==1)tv_blocks[tv_row].genre=(tv_blocks[tv_row].genre+step+TV_GENRES)%TV_GENRES;
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
    if(!settings_on&&!about&&(browse.mode==14||browse.mode==22||browse.mode==FAVORITES_MODE)&&key==SDLK_y&&total&&
       !strcmp(titles[visible[selection]].kind,"game")){
        rom_core_open(&titles[visible[selection]]);return;
    }
    if(!settings_on&&!about&&key==SDLK_F8&&(browse.mode==14||browse.mode==21||browse.mode==22||browse.mode==26||browse.mode==28)){Browse next={21,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!settings_on&&!about&&(key==SDLK_f||key==SDLK_F7)){favorites_toggle();return;}
    if(!settings_on&&!about&&browse.mode==28&&key==SDLK_y&&total){
        Browse next={29,1,titles[visible[selection]].id,0,0,0,"",""};
        request_catalog(next,0,0);return;
    }
    if(!settings_on&&!about&&browse.mode==21&&key==SDLK_y){Browse next={14,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!settings_on&&!about&&(browse.mode==21||browse.mode==22)&&key==SDLK_F3){Browse next={15,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
    if(!settings_on&&!about&&browse.mode==14&&key==SDLK_SLASH){Browse next={21,1,0,0,0,0,"",""};request_catalog(next,1,0);return;}
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
            if(bios_present(bios_files[bios_selected])){
                snprintf(status,sizeof(status),"BIOS ALREADY PRESENT");
            }else{
                bios_on=0;
                Browse next={23,1,bios_selected+1,0,0,0,"",""};
                request_catalog(next,1,0);
            }
            return;
        }
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5||key==SDLK_F2)bios_on=0;
        return;
    }
    if(settings_on){
        if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE||key==SDLK_F5){settings_on=0;save_settings();return;}
        if(key==SDLK_TAB||key==SDLK_PAGEDOWN||key==SDLK_m){settings_switch(1);return;}
        if(key==SDLK_PAGEUP||key==SDLK_v){settings_switch(-1);return;}
        if(settings_row<0){
            if(key==SDLK_LEFT)settings_switch(-1);
            else if(key==SDLK_RIGHT)settings_switch(1);
            else if(key==SDLK_DOWN||key==SDLK_RETURN)settings_row=0;
            return;
        }
        if(key==SDLK_UP)settings_row--;
        if(key==SDLK_DOWN&&settings_row+1<settings_tabs[settings_tab].count)settings_row++;
        int step=key==SDLK_LEFT?-1:1;
        if(key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_RETURN){
            int id=settings_id();
            if(key!=SDLK_RETURN&&(id==7||id==10||id==15||id==16||id==21||id>=23))return;
            switch(id){
                case 20:music_autoplay=!music_autoplay;break;
                case 21:settings_on=0;music_on=1;break;
                case 23:if(key==SDLK_RETURN&&!samba_installed())samba_service("install");break;
                case 24:if(key==SDLK_RETURN)samba_password_open();break;
                case 25:if(key==SDLK_RETURN){
                    if(access("samba/.configured",R_OK))samba_password_open();
                    else samba_service(access("samba/.enabled",R_OK)?"enable":"disable");
                }break;
                case 19:{const int outputs[]={0,240,480,720,1080};int index=0;
                    for(int i=0;i<5;i++)if(game_output==outputs[i])index=i;
                    game_output=outputs[(index+step+5)%5];break;}
                case 18:if(!set_auto_updates(!auto_updates_enabled()))snprintf(status,sizeof(status),"COULD NOT CHANGE AUTO UPDATE SETTING");break;
                case 17:series_autoplay=!series_autoplay;break;
                case 15:accounts_open(0);break;
                case 16:accounts_open(1);break;
                case 0:quality_setting=(quality_setting+step+QUALITY_CHOICES)%QUALITY_CHOICES;break;
                case 1:if(!buffer_setting)buffer_setting=1;buffer_setting=1+(buffer_setting-1+step+8)%8;break;
                case 2:disk_setting=(disk_setting+step+3)%3;break;
                case 3:coverflow=!coverflow;break;
                case 4:subtitle_setting=(subtitle_setting+step+5)%5;break;
                case 5:subtitle_scale=subtitle_scale==2?3:2;break;
                case 8:tv_breaks=!tv_breaks;break;
                case 9:menu_theme=(menu_theme+step+MENU_THEMES)%MENU_THEMES;monochrome_menu=menu_theme==1;load_menu_background();break;
                case 10:bios_on=1;break;
                case 11:game_autostate=!game_autostate;break;
                case 12:tv_rating_weight=(tv_rating_weight-1+step+5)%5+1;break;
                case 13:tv_popularity_weight=(tv_popularity_weight+step+6)%6;break;
                case 14:tv_variety_weight=(tv_variety_weight+step+6)%6;break;
                case 7:tv_schedule_on=1;settings_on=0;tv_row=0;tv_column=1;break;
                case 6:subtitle_delay+=step;if(subtitle_delay>5)subtitle_delay=-5;if(subtitle_delay< -5)subtitle_delay=5;break;
            }save_settings();
        }return;
    }
    if(browse.mode==11&&!about&&(key==SDLK_F6||key==SDLK_y)){mtv_override=!mtv_override;snprintf(status,sizeof(status),mtv_override?"MTV MUSIC VIDEOS / A START":"TV SCHEDULE / A START");return;}
    if(browse.mode==11&&!about&&(key==SDLK_RETURN||key==SDLK_SPACE)){tv_cancel_prepared();tv_joining=1;tv_active=1;tv_attempts=0;tv_next_at=0;tv_break_due=tv_in_break=tv_prepared=0;tv_tick();return;}
    if(about&&(key==SDLK_UP||key==SDLK_DOWN)){about_row=!about_row;return;}
    if(about&&(key==SDLK_RETURN||key==SDLK_SPACE||key==SDLK_u)){if(about_row==1&&key!=SDLK_u)accounts_open(1);else start_update();return;}
    if(key==SDLK_u){start_update();return;}
    if(key==SDLK_F5){settings_on=1;about=0;return;}
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE) { if(about)about=0;else if(history_size)request_catalog(history[history_size-1],0,1);else if(browse.mode){Browse next={0,1,0,0,0,0,"",""};request_catalog(next,0,0);}else running=0; }
    else if(key==SDLK_F2){
        const int modes[]={11,FAVORITES_MODE,1,2,7,21,30,0};
        int current=history_size?history[0].mode:browse.mode,index=0;
        if(current==14||current==22)current=21;
        if(current==9||current==10||current==31)current=30;
        for(int i=0;i<8;i++)if(modes[i]==current){index=i;break;}
        Browse next={0,1,0,0,0,0,"",""};next.mode=modes[(index+1)%8];
        history_size=0;request_catalog(next,0,0);
    }
    else if((key==SDLK_F3||key==SDLK_SLASH)&&(browse.mode==1||browse.mode==2||browse.mode==7||browse.mode==9||browse.mode==17||browse.mode==23||browse.mode==31)){search_on=1;search_key=0;strcpy(search_text,browse.query);SDL_StartTextInput();}
    else if(key==SDLK_F3)about=!about;
    else if(key==SDLK_i)about=!about;
    else if(key==SDLK_y)stars_on=!stars_on;
    else if(key==SDLK_v){snprintf(status,sizeof(status),"VIEW OPTIONS ARE AVAILABLE DURING VIDEO PLAYBACK");}
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
static void desktop_key(SDL_Keycode key,SDL_Keymod modifiers){
    int typing=samba_password_on||search_on||(music_on&&music_edit);
    if(key==SDLK_q&&(modifiers&KMOD_CTRL)){running=0;return;}
    if(!typing&&key==SDLK_F1){keyboard_help=!keyboard_help;return;}
    if(keyboard_help){if(key==SDLK_ESCAPE)keyboard_help=0;return;}
    if(typing){action(key);return;}
    if(youtube_pid&&!youtube_preparing){action(key);return;}
    if(player_pid){
        char c=(key==SDLK_SPACE||key==SDLK_k)?'P':key==SDLK_j?'R':key==SDLK_l?'F':key==SDLK_LEFT?'l':key==SDLK_RIGHT?'r':key==SDLK_f?'t':key==SDLK_m?'o':key==SDLK_TAB?'m':0;
        if(c&&player_control>=0){ssize_t n=player_pipe_write(player_control,&c,1);(void)n;return;}
        action(key);return;
    }
    if((key==SDLK_SPACE||key==SDLK_k)&&music_job==3){kill(music_pid,SIGUSR1);music_paused=!music_paused;return;}
    if(key==SDLK_F10){music_on=0;settings_on=1;about=0;return;}
    if(key==SDLK_F4){menu_theme=(menu_theme+1)%MENU_THEMES;monochrome_menu=menu_theme==1;load_menu_background();save_settings();return;}
    if(key==SDLK_p&&(modifiers&KMOD_CTRL)){music_on=0;accounts_open(0);return;}
    if(key==SDLK_f&&(modifiers&KMOD_CTRL)){action(SDLK_F3);return;}
    if(!music_on&&!settings_on&&!accounts_on&&!catalog_pid&&(key==SDLK_t||key==SDLK_g)){
        Browse next={key==SDLK_t?11:21,1,0,0,0,0,"",""};history_size=0;request_catalog(next,0,0);return;
    }
    if(key==SDLK_ESCAPE&&!music_on&&!settings_on&&!accounts_on&&!about&&!history_size&&!browse.mode){
        snprintf(status,sizeof(status),"CTRL+Q TO QUIT / F1 KEYBOARD HELP");return;
    }
    action(key);
}
static void add_pad(int device) {
    if(!SDL_IsGameController(device))return;
    controller_used();
    SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(device);
    for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==id)return;
    for(int i=0;i<4;i++)if(!pads[i]){pads[i]=SDL_GameControllerOpen(device);break;}
}
/* Let hardware-owning children release audio/video before the next app starts.
 * A network-stalled child still has a bounded deadline. */
static void shutdown_children(pid_t *children,size_t count){
    for(size_t i=0;i<count;i++)if(children[i]>0)kill(children[i],SIGTERM);
    Uint32 deadline=SDL_GetTicks()+3000;
    for(;;){
        int pending=0;
        for(size_t i=0;i<count;i++){
            if(children[i]<=0)continue;
            pid_t done=waitpid(children[i],NULL,WNOHANG);
            if(done==children[i]||(done<0&&errno==ECHILD))children[i]=0;
            else pending=1;
        }
        if(!pending||(Sint32)(SDL_GetTicks()-deadline)>=0)break;
        SDL_Delay(10);
    }
    for(size_t i=0;i<count;i++)if(children[i]>0)kill(children[i],SIGKILL);
    for(int attempt=0;attempt<25;attempt++){
        int pending=0;
        for(size_t i=0;i<count;i++){
            if(children[i]<=0)continue;
            pid_t done=waitpid(children[i],NULL,WNOHANG);
            if(done==children[i]||(done<0&&errno==ECHILD))children[i]=0;
            else pending=1;
        }
        if(!pending)break;
        SDL_Delay(10);
    }
}

#ifdef _WIN32
#define main flxtr_shell_main
#endif
int main(int argc,char **argv) {
    const char *catalog="catalog.tsv",*shot=NULL;int frames=0,seen=0,initial_library=0;Uint32 start,last_draw=0;int dirty=1;
    load_settings();search_dict_load();
    for(int i=1;i<argc;i++) {
#ifdef _WIN32
        if(!strcmp(argv[i],"--ui-worker"))continue;
#endif
        if(!strcmp(argv[i],"--version")){puts(FLXTR_VERSION);return 0;}
        else if(!strcmp(argv[i],"--grid"))coverflow=0;
        else if(!strcmp(argv[i],"--settings"))settings_on=1;
        else if(!strcmp(argv[i],"--settings-tab")&&i+1<argc){
            const char *tab=argv[++i];int found=0;
            for(int k=0;k<SETTINGS_TABS;k++)if(!strcasecmp(tab,settings_tabs[k].name)){settings_tab=k;found=1;break;}
            if(!found)return 2;
            settings_on=1;
        }
        else if(!strcmp(argv[i],"--api-accounts")){settings_on=1;accounts_on=1;settings_tab=SETTINGS_SERVICES;settings_row=3;}
        else if(!strcmp(argv[i],"--catalog")&&i+1<argc)catalog=argv[++i];
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)shot=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--no-stars"))stars_on=0;
        else if(!strcmp(argv[i],"--mtv")){initial_library=11;mtv_override=tv_active=1;}
        else if(!strcmp(argv[i],"--music"))music_on=1;
        else if(!strcmp(argv[i],"--keyboard-help"))keyboard_help=1;
        else if(!strcmp(argv[i],"--theme")&&i+1<argc){char *end;long theme=strtol(argv[++i],&end,10);if(*end||theme<0||theme>=MENU_THEMES)return 2;menu_theme=(int)theme;monochrome_menu=menu_theme==1;load_menu_background();}
        else if(!strcmp(argv[i],"--library")&&i+1<argc){const char *v=argv[++i];initial_library=!strcmp(v,"movies")?1:!strcmp(v,"series")?2:!strcmp(v,"anime")?7:!strcmp(v,"archive")?30:!strcmp(v,"tv")?11:!strcmp(v,"games")?21:0;if(!initial_library)return 2;}
        else {fprintf(stderr,"Usage: %s [--catalog file] [--screenshot file.bmp] [--frames N] [--no-stars]\n",argv[0]);return 2;}
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if(SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)) {fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    if(open_ui()){fprintf(stderr,"Renderer: %s\n",SDL_GetError());SDL_Quit();return 1;}
    if(accounts_on)accounts_open(0);
    for(int i=0;i<SDL_NumJoysticks();i++)add_pad(i);
    uint32_t seed=0x31415926;
    for(int i=0;i<STARS;i++){seed=seed*1664525u+1013904223u;star_x[i]=seed>>24;star_y[i]=seed>>16;star_speed[i]=1+(seed%3);}
    local_catalog=catalog;read_catalog(catalog);start=SDL_GetTicks();controller_used();
    if(initial_library){Browse next={initial_library,1,0,0,0,0,"",""};request_catalog(next,0,0);}
    while(running) {
        if(accounts_on){api_setup_tick(SDL_GetTicks());if(SDL_GetTicks()-last_draw>=100)dirty=1;}
        SDL_Event event;
        if(SDL_WaitEventTimeout(&event,phone_upload>=0?1:accounts_on?10:(music_on||music_job==3)?100:((stars_on&&!background)||flow_animating())?33:250)) do {
            dirty=1;
            if(event.type==SDL_QUIT)running=0;
            else if(event.type==SDL_KEYDOWN){keyboard_used=1;desktop_key(event.key.keysym.sym,(SDL_Keymod)event.key.keysym.mod);}
            else if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT){
                int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
                if(ww>0&&wh>0&&event.button.x*W/ww>=730&&event.button.y*H/wh>=16&&event.button.y*H/wh<71)action(SDLK_m);
            }
            else if(event.type==SDL_TEXTINPUT&&samba_password_on)samba_password_text(event.text.text);
            else if(event.type==SDL_TEXTINPUT&&music_on&&music_edit)music_text(event.text.text);
            else if(event.type==SDL_TEXTINPUT&&search_on){for(const char *p=event.text.text;*p;p++){size_t n=strlen(search_text);if(n<64&&(isalnum((unsigned char)*p)||*p==' '||*p=='-')){search_text[n]=(char)toupper((unsigned char)*p);search_text[n+1]=0;}}}
            else if(event.type==SDL_CONTROLLERDEVICEADDED)add_pad(event.cdevice.which);
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERAXISMOTION){
                int value=event.caxis.value;
                if((event.caxis.axis<4&&(value>8000||value< -8000))||
                   (event.caxis.axis>=4&&value>8000))controller_used();
                if(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT){
                    static int left_trigger_pressed;
                    if(value>20000&&!left_trigger_pressed){
                        left_trigger_pressed=1;action(SDLK_F8);
                    }else if(value<8000)left_trigger_pressed=0;
                }
            }
            else if(event.type==SDL_CONTROLLERBUTTONUP)controller_used();
            else if(event.type==SDL_CONTROLLERBUTTONDOWN) {
                keyboard_used=0;
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
                    case SDL_CONTROLLER_BUTTON_A:action(samba_password_on||search_on||(music_on&&music_edit)?SDLK_F4:SDLK_RETURN);break;
                    case SDL_CONTROLLER_BUTTON_B:action(samba_password_on?SDLK_ESCAPE:search_on||(music_on&&music_edit)?SDLK_BACKSPACE:SDLK_ESCAPE);break;
                    case SDL_CONTROLLER_BUTTON_X:action(SDLK_F2);break;
                    case SDL_CONTROLLER_BUTTON_Y:action(search_on||(music_on&&music_edit)?SDLK_TAB:SDLK_y);break;
                    case SDL_CONTROLLER_BUTTON_LEFTSTICK:action(SDLK_F7);break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:action(SDLK_F9);break;
                    case SDL_CONTROLLER_BUTTON_BACK:action(SDLK_F5);break;
                    case SDL_CONTROLLER_BUTTON_START:action(samba_password_on?SDLK_RETURN:player_pid?SDLK_i:SDLK_F3);break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:action(player_pid?SDLK_LEFT:SDLK_v);break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:action(player_pid?SDLK_RIGHT:SDLK_m);break;
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
        if(samba_poll())dirty=1;
        if(music_poll())dirty=1;
        if(youtube_poll())dirty=1;
        tv_history_poll();
        youtube_prefetch();
        if(!music_on){prefetch_pages();tv_tick();}controller_idle();
        if(player_pid){int code;pid_t p=waitpid(player_pid,&code,WNOHANG);
            if(p==player_pid){finish_player(code);dirty=1;}
            else if(stopping_player&&SDL_GetTicks()-stop_time>3000)kill(player_pid,SIGKILL);
            SDL_Delay(30);continue;
        }
        Uint32 now=SDL_GetTicks();
        if(dirty||((music_job==3||music_job==2)&&now-last_draw>=230)||(browse.mode==11&&now-last_draw>=1000)||(!music_on&&!accounts_on&&((stars_on&&!background)||flow_animating())&&now-last_draw>=33)||shot||frames){draw(now-start);last_draw=now;dirty=0;seen++;
            if(shot&&!catalog_pid){int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
                if(!s||SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch)||SDL_SaveBMP(s,shot)){fprintf(stderr,"Screenshot: %s\n",SDL_GetError());SDL_FreeSurface(s);return 1;}SDL_FreeSurface(s);running=0;}
            if(frames>0&&seen>=frames)running=0;
        }
    }
    tv_history_finish("interrupted",tv_history_last_position());
    music_stop();youtube_stop();
    tv_cancel_prepared();
    api_setup_close();
    pid_t children[]={player_pid,catalog_pid,prefetch_pid,idle_pid,update_pid,samba_pid};
    shutdown_children(children,sizeof(children)/sizeof(children[0]));
    close_ui();for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerClose(pads[i]);
    SDL_Quit();return game_exit?43:restart_shell?42:0;
}

#ifdef _WIN32
#undef main
static int windows_supervisor(int argc,char **argv){
    const char **args=(const char **)calloc((size_t)argc+4,sizeof(*args));if(!args)return 126;
    args[0]="FlXtR";args[1]="--ui-worker";
    for(int i=1;i<argc;i++)args[i+1]=argv[i];
    int code;
    for(;;){
        code=flxtr_win_run("FlXtR",args,"greenlink.log","greenlink.log");
        if(code==43){
            const char *game_args[]={"greenlink-catalog","--run-game",NULL};
            int result=flxtr_win_run("greenlink-catalog",game_args,"game.log","game.log");
            FILE *f=fopen("game-exit-status","w");if(f){fprintf(f,"%d\n",result);fclose(f);}
            memset(args,0,((size_t)argc+4)*sizeof(*args));
            args[0]="FlXtR";args[1]="--ui-worker";args[2]="--library";args[3]="games";continue;
        }
        if(code==42)continue;
        break;
    }
    free(args);return code<0?1:code;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show){
    (void)instance;(void)previous;(void)command;(void)show;
    int argc=0;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 126;
    SDL_SetMainReady();
    int worker=argc>1&&!wcscmp(argv[1],L"--ui-worker");
    int code=flxtr_win_entry(worker?flxtr_shell_main:windows_supervisor,argc,argv,!worker);
    LocalFree(argv);return code;
}
#endif
