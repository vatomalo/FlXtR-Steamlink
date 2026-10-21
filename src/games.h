/* Local RetroArch importer; desktop core downloads are explicit user actions. */
#include <dirent.h>
#include <limits.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include "windows_archive.h"
#include <fcntl.h>
#include <errno.h>
#define GAME_LIMIT 2048
#include "desktop_retroarch.h"
#include "game_output.h"
#include "rom_core_preferences.h"
#define FLXTR_GAME_CONFIG
#include "game_scaling.h"
#undef FLXTR_GAME_CONFIG
#include "platform_process.h"
static const char *games_executable;
static int game_absolute(const char *p){return p&&(p[0]=='/'
#ifdef _WIN32
 ||(isalpha((unsigned char)p[0])&&p[1]==':'&&(p[2]=='/'||p[2]=='\\'))
#endif
 );}
static const char *games_home(void){const char *p=getenv("FLXTR_RETROARCH_HOME");if(p&&game_absolute(p))return p;
#ifdef _WIN32
static char home[PATH_MAX];snprintf(home,sizeof(home),"%s/retroarch",flxtr_win_binary_dir());return home;
#else
return "/home/apps/retroarch";
#endif
}
static int game_file(const char *p){struct stat st;return game_absolute(p)&&!stat(p,&st)&&S_ISREG(st.st_mode)&&!access(p,R_OK);}
/* Side-by-side builds: never replace system cores or a per-ROM choice. */
static int game_bundled_core(const char *current,char *out,size_t cap){
    if(getenv("FLXTR_USE_SYSTEM_CORES"))return 0;
    const char *base=strrchr(current,'/');base=base?base+1:current;
    const char *name=NULL;
    if(!strcmp(base,"snes9x2005_libretro" FLXTR_CORE_EXT))name="snes9x2005_flxtr_libretro" FLXTR_CORE_EXT;
    if(!strcmp(base,"mame2003_plus_libretro" FLXTR_CORE_EXT))name="mame2003_plus_flxtr_libretro" FLXTR_CORE_EXT;
    char cwd[PATH_MAX];if(!name||!getcwd(cwd,sizeof(cwd)))return 0;
    int n=snprintf(out,cap,"%s/cores/%s",cwd,name);
    return n>0&&n<(int)cap&&game_file(out);
}
/* Scope the legacy SDL runtime to RetroArch, never the shell or system. */
static inline void game_native_sdl(void){
    if(getenv("FLXTR_USE_SYSTEM_SDL"))return;
    char cwd[PATH_MAX],library[PATH_MAX],search[PATH_MAX*2];
    if(!getcwd(cwd,sizeof(cwd)))return;
    int n=snprintf(library,sizeof(library),"%s/retroarch-libs/libSDL2-2.0.so.0",cwd);
    if(n<0||n>=(int)sizeof(library)||!game_file(library))return;
    const char *old=getenv("LD_LIBRARY_PATH");
    n=snprintf(search,sizeof(search),"%s/retroarch-libs%s%s",cwd,old&&*old?":":"",old&&*old?old:"");
    if(n<0||n>=(int)sizeof(search)||setenv("LD_LIBRARY_PATH",search,1))return;
    fprintf(stderr,"Game runtime: native SDL2 from %s\n",library);
}
static json_object *games_read(const char *path){struct stat st;if(stat(path,&st)||st.st_size>8*1024*1024)return NULL;return json_object_from_file(path);}
static const char *game_core(const char *path){
    const char *ext=strrchr(path,'.');if(!ext)return NULL;ext++;
    if(!strcasecmp(ext,"nes"))return "fceumm";
    if(!strcasecmp(ext,"sfc")||!strcasecmp(ext,"smc"))return "snes9x2005";
    if(!strcasecmp(ext,"gba"))return
#ifdef FLXTR_DESKTOP
        "mgba";
#else
        "gpsp";
#endif
    if(!strcasecmp(ext,"gb")||!strcasecmp(ext,"gbc"))return "gambatte";
    if(!strcasecmp(ext,"md")||!strcasecmp(ext,"gen")||!strcasecmp(ext,"smd")||!strcasecmp(ext,"sms")||!strcasecmp(ext,"gg"))return "picodrive";
    if(!strcasecmp(ext,"cue")||!strcasecmp(ext,"pbp")||!strcasecmp(ext,"chd")||!strcasecmp(ext,"m3u"))return
#ifdef FLXTR_DESKTOP
        "mednafen_psx";
#else
        "pcsx_rearmed";
#endif
    if(!strcasecmp(ext,"a26"))return "stella";
    if(!strcasecmp(ext,"zip")){
        /* Neo Geo uses a shared BIOS ZIP alongside game ZIPs. Never launch
         * neogeo.zip itself as a game. Prefer FBNeo when installed. */
        const char *base=strrchr(path,'/');base=base?base+1:path;
        if(!strcasecmp(base,"neogeo.zip"))return NULL;
        if(strcasestr(path,"/neogeo/")||strcasestr(path,"/neo-geo/"))return "fbneo";
        if(strcasestr(path,"/mame/"))return "mame2003_plus";
        if(strcasestr(path,"/nes/"))return "fceumm";
        if(strcasestr(path,"/snes/"))return "snes9x2005";
        if(strcasestr(path,"/gba/"))return "gpsp";
    }return NULL;
}
static void game_add(json_object *games,const char *rom,const char *core,const char *label){
    if(json_object_array_length(games)>=GAME_LIMIT||!game_file(rom)||!game_file(core))return;
    for(size_t i=0;i<(size_t)json_object_array_length(games);i++)if(!strcmp(string(json_object_array_get_idx(games,i),"rom"),rom))return;
    const char *name=strrchr(rom,'/');name=name?name+1:rom;char title[80];clean(title,sizeof(title),label&&*label?label:name);
    if(!label||!*label){char *dot=strrchr(title,'.');if(dot)*dot=0;}
    json_object *g=json_object_new_object();json_object_object_add(g,"title",json_object_new_string(title));
    json_object_object_add(g,"rom",json_object_new_string(rom));json_object_object_add(g,"core",json_object_new_string(core));json_object_array_add(games,g);
}
static void game_infer(json_object *games,const char *rom,const char *label){
    const char *name=game_core(rom);if(!name)return;char core[PATH_MAX];
    char cwd[PATH_MAX];
    if(getcwd(cwd,sizeof(cwd))){
        int n=snprintf(core,sizeof(core),"%s/cores/%s_libretro" FLXTR_CORE_EXT,cwd,name);
        if(n>0&&n<(int)sizeof(core)&&game_file(core)){game_add(games,rom,core,label);return;}
    }
    snprintf(core,sizeof(core),"%s/cores/%s_libretro" FLXTR_CORE_EXT,games_home(),name);
    if(!game_file(core))snprintf(core,sizeof(core),"%s/.home/.config/retroarch/cores/%s_libretro" FLXTR_CORE_EXT,games_home(),name);
    /* Steam Link RetroArch installations may ship MAME instead of FBNeo. */
    if(!game_file(core)&&!strcmp(name,"fbneo")){
        snprintf(core,sizeof(core),"%s/cores/mame2003_plus_libretro" FLXTR_CORE_EXT,games_home());
        if(!game_file(core))snprintf(core,sizeof(core),"%s/.home/.config/retroarch/cores/mame2003_plus_libretro" FLXTR_CORE_EXT,games_home());
    }
    game_add(games,rom,core,label);
}
static void game_playlist(json_object *games,const char *path){
    json_object *root=games_read(path),*items=field(root,"items");
    if(items&&json_object_is_type(items,json_type_array))for(size_t i=0;i<(size_t)json_object_array_length(items)&&i<GAME_LIMIT;i++){
        json_object *g=json_object_array_get_idx(items,i);const char *rom=string(g,"path"),*core=string(g,"core_path"),*label=string(g,"label");
        if(game_file(core))game_add(games,rom,core,label);else game_infer(games,rom,label);
    }
    int parsed=items!=NULL;json_object_put(root);
    /* RetroArch 1.6 also uses six-line playlists. */
    if(parsed)return;
    FILE *f=fopen(path,"r");if(!f)return;char rows[6][PATH_MAX];int n=0;
    while(n<GAME_LIMIT&&fgets(rows[0],sizeof(rows[0]),f)){
        int ok=1;for(int i=1;i<6;i++)if(!fgets(rows[i],sizeof(rows[i]),f)){ok=0;break;}if(!ok)break;
        for(int i=0;i<6;i++)rows[i][strcspn(rows[i],"\r\n")]=0;
        if(game_file(rows[2]))game_add(games,rows[0],rows[2],rows[1]);else game_infer(games,rows[0],rows[1]);n++;
    }fclose(f);
}
static int game_visited;
static void game_scan(json_object *games,const char *path,int depth,int playlists){
    if(depth>5||json_object_array_length(games)>=GAME_LIMIT)return;
    DIR *d=opendir(path);if(!d)return;struct dirent *de;
    while(game_visited<50000&&(de=readdir(d))){
        if(de->d_name[0]=='.')continue;
        game_visited++;
        char file[PATH_MAX];if(snprintf(file,sizeof(file),"%s/%s",path,de->d_name)>=(int)sizeof(file))continue;
        struct stat st;if(lstat(file,&st))continue;
        if(S_ISDIR(st.st_mode))game_scan(games,file,depth+1,playlists);
        else if(S_ISREG(st.st_mode)){
            const char *ext=strrchr(file,'.');
            if(playlists){if(ext&&!strcasecmp(ext,".lpl"))game_playlist(games,file);}
            else game_infer(games,file,NULL);
        }
    }closedir(d);
}
static json_object *games_import(void){
    json_object *games=json_object_new_array();char path[PATH_MAX];game_visited=0;
    snprintf(path,sizeof(path),"%s/.home/.config/retroarch",games_home());game_scan(games,path,0,1);
    snprintf(path,sizeof(path),"%s/roms",games_home());game_scan(games,path,0,0);
    if(getcwd(path,sizeof(path))){size_t n=strlen(path);if(n+6<sizeof(path)){strcpy(path+n,"/roms");game_scan(games,path,0,0);}}
    FILE *f=fopen("games-paths.txt","r");if(f){while(fgets(path,sizeof(path),f)){path[strcspn(path,"\r\n")]=0;if(game_absolute(path))game_scan(games,path,0,0);}fclose(f);}
    if(!json_object_to_file("games.json.next",games))rename("games.json.next","games.json");
    return games;
}
static const char *const local_folders[]={"NES","SNES","GAME BOY","GAME BOY COLOR","GAME BOY ADVANCE","PLAYSTATION","PSP","NEO GEO","ARCADE","OTHER"};
static int local_category(const char *core,const char *rom){
    if(strstr(core,"fceumm"))return 1;
    if(strstr(core,"snes9x"))return 2;
    if(strstr(core,"gambatte"))return strcasestr(rom,".gbc")?4:3;
    if(strstr(core,"gpsp")||strstr(core,"mgba"))return 5;
    if(strstr(core,"pcsx_rearmed")||strstr(core,"mednafen_psx"))return 6;
    if(strstr(core,"ppsspp"))return 7;
    if(strstr(core,"fbneo")||strcasestr(rom,"/neogeo/"))return 8;
    if(strstr(core,"mame"))return 9;
    return 10;
}
static int games_folders(int page){
    total=12;
#ifdef FLXTR_DESKTOP
    total=13;
#endif
    for(int slot=(page-1)*PAGE_SIZE;slot<total&&used<PAGE_SIZE;slot++){
        int i=slot;
#ifdef FLXTR_DESKTOP
        i=slot==0?12:slot-1;
        if(i==12){
            Entry *e=&entries[used++];strcpy(e->title,"DOWNLOAD LIBRETRO CORES");
            strcpy(e->meta,"OFFICIAL PLATFORM CORES / RETROARCH REQUIRED");
            strcpy(e->kind,"core-manager");continue;
        }
#endif
        Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"%s",i==10?"DOWNLOADS":i==11?"SCUMMVM":local_folders[i]);
        snprintf(e->meta,sizeof(e->meta),"[ %s ] / A OPEN FOLDER",i==10?"DOWNLOADS":local_folders[i]);
        strcpy(e->kind,i==10?"downloads-folder":i==11?"scummvm-launch":"game-folder");e->id=i+1;
        const char *icons[]={"FC","SFC","GB","GBC","GBA","PS","PSP","NEOGEO","ARCADE","MAME","ARCADE","SCUMMVM"};
        snprintf(e->poster,sizeof(e->poster),"gameart:FlXtR Console|%s",icons[i]);
    }
    return 0;
}
static int games_list_filtered(int page,int refresh,int folder){

    json_object *games=refresh?NULL:games_read("games.json");
    if(games&&!json_object_is_type(games,json_type_array)){json_object_put(games);games=NULL;}
    if(!games)games=games_import();
    if(!games)return -1;
    int count=(int)json_object_array_length(games),matched=0;
    if(count>GAME_LIMIT)count=GAME_LIMIT;
    for(int i=0;i<count;i++){
        json_object *g=json_object_array_get_idx(games,i);
        if(folder&&local_category(string(g,"core"),string(g,"rom"))!=folder)continue;
        matched++;
    }
    total=matched;int skipped=0;
    for(int i=0;i<count&&used<PAGE_SIZE;i++){
        json_object *g=json_object_array_get_idx(games,i);
        if(folder&&local_category(string(g,"core"),string(g,"rom"))!=folder)continue;
        if(skipped++<(page-1)*PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),string(g,"title"));
        const char *core=string(g,"core"),*base=strrchr(core,'/');clean(e->meta,sizeof(e->meta),base?base+1:core);
        const char *rom_path=string(g,"rom");size_t rom_len=strlen(rom_path);
        int browse_zip=rom_len>=4&&!strcasecmp(rom_path+rom_len-4,".zip")&&
            !strcasestr(rom_path,"/neogeo/")&&!strcasestr(rom_path,"/neo-geo/")&&
            !strcasestr(rom_path,"/mame/");
        strcpy(e->kind,browse_zip?"game-zip":"game");e->id=i+1;
        if(!browse_zip){char key[17];rom_identity(rom_path,key);snprintf(e->url,sizeof(e->url),"romid:%s",key);}
        /* Prefer RetroArch's locally downloaded Named_Boxarts thumbnails. */
        const char *rom=string(g,"rom"),*file=strrchr(rom,'/');file=file?file+1:rom;
        /* Artwork classification is independent of the selected launch core. */
        const char *system=NULL;
        const char *ext=strrchr(rom,'.');
        if(strcasestr(rom,"/neogeo/")||strcasestr(rom,"/neo-geo/")||
           strstr(core,"fbneo")||strstr(core,"fbalpha2012")||
           !strncasecmp(e->title,"kof",3)||!strncasecmp(e->title,"mslug",5)||
           !strncasecmp(e->title,"lastbld",7)||!strncasecmp(e->title,"samsho",6)||
           !strncasecmp(e->title,"Samurai Shodown",14)||!strncasecmp(e->title,"Garou",5))
            system="SNK - Neo Geo";
        else if(strstr(core,"fceumm")||strstr(core,"nestopia")||(ext&&!strcasecmp(ext,".nes")))
            system="Nintendo - Nintendo Entertainment System";
        else if(strstr(core,"snes9x")||strstr(core,"bsnes")||(ext&&(!strcasecmp(ext,".sfc")||!strcasecmp(ext,".smc"))))
            system="Nintendo - Super Nintendo Entertainment System";
        else if(strstr(core,"gpsp")||strstr(core,"mgba")||(ext&&!strcasecmp(ext,".gba")))
            system="Nintendo - Game Boy Advance";
        else if(ext&&!strcasecmp(ext,".gbc"))system="Nintendo - Game Boy Color";
        else if(strstr(core,"gambatte")||(ext&&!strcasecmp(ext,".gb")))system="Nintendo - Game Boy";
        else if(strstr(core,"pcsx_rearmed"))system="Sony - PlayStation";
        else if(strstr(core,"ppsspp"))system="Sony - PlayStation Portable";
        else if(strstr(core,"picodrive")||strstr(core,"genesis_plus_gx"))
            system="Sega - Mega Drive - Genesis";
        if(system){
            char stem[128];snprintf(stem,sizeof(stem),"%s",e->title);
            char thumb[PATH_MAX];
            snprintf(thumb,sizeof(thumb),"%s/.home/.config/retroarch/thumbnails/%s/Named_Boxarts/%s.png",games_home(),system,stem);
            if(!game_file(thumb))snprintf(thumb,sizeof(thumb),"%s/thumbnails/%s/Named_Boxarts/%s.png",games_home(),system,stem);
            if(!game_file(thumb)){
                /* ROM-set identity takes precedence over display-title guessing.
                 * Manifest columns: Libretro system, ROM stem ID, canonical title.
                 * This also supports titles supplied by a future CRC/serial DB. */
                const char *art_title=e->title;
                char canonical[128]="";
                char rom_id[128];snprintf(rom_id,sizeof(rom_id),"%s",file);
                char *extension=strrchr(rom_id,'.');if(extension)*extension=0;
                FILE *ids=fopen("game-art-ids.tsv","r");
                if(ids){
                    char line[512];
                    while(fgets(line,sizeof(line),ids)){
                        char *first=strchr(line,'\t');if(!first)continue;
                        *first++=0;char *second=strchr(first,'\t');if(!second)continue;
                        *second++=0;second[strcspn(second,"\r\n")]=0;
                        if(!strcmp(line,system)&&!strcasecmp(first,rom_id)&&
                           *second&&strlen(second)<sizeof(canonical)){
                            snprintf(canonical,sizeof(canonical),"%s",second);break;
                        }
                    }
                    fclose(ids);
                }
                if(canonical[0])art_title=canonical;
                /* Secret of Mana Turbo is a ROM hack: borrow the original
                 * game's cover without changing the game name or launch path. */
                if(strstr(system,"Super Nintendo")&&
                   (!strncasecmp(rom_id,"Secret of Mana Turbo",19)||
                    !strncasecmp(e->title,"Secret of Mana Turbo",19)))
                    art_title="Secret of Mana";
                const char *short_names[]={"kof2002","kof94","lastblad","lastbld2","mslug","mslug2","mslug3","mslug4","mslug5","samsho","samsho2","garou","msword","gng","Pocky&Rocky","SecretofMana","SecretofMana2","SuperMarioAll-Stars+SuperMarioWorld"};
                const char *full_names[]={"The King of Fighters 2002","The King of Fighters '94","The Last Blade","The Last Blade 2","Metal Slug","Metal Slug 2","Metal Slug 3","Metal Slug 4","Metal Slug 5","Samurai Shodown","Samurai Shodown II","Garou - Mark of the Wolves","Magic Sword","Ghosts'n Goblins","Pocky & Rocky","Secret of Mana","Seiken Densetsu 3","Super Mario All-Stars + Super Mario World"};
                for(size_t k=0;k<sizeof(short_names)/sizeof(short_names[0]);k++)
                    if(!canonical[0]&&!strcasecmp(rom_id,short_names[k])){art_title=full_names[k];break;}
                if(strlen(system)+strlen(art_title)+10<sizeof(e->poster))
                    snprintf(e->poster,sizeof(e->poster),"gameart:%s|%s",system,art_title);
            }
            if(game_file(thumb)){
                const char prefix[]="localthumb:";
                size_t length=strlen(thumb);
                if(length<sizeof(e->poster)-sizeof(prefix)){
                    memcpy(e->poster,prefix,sizeof(prefix)-1);
                    memcpy(e->poster+sizeof(prefix)-1,thumb,length+1);
                }
            }
        }
        /* A curated ROM ID is a stronger display alias than the filename.
         * The launch manifest and poster keys stay untouched. */
        {
            char rom_id[128];snprintf(rom_id,sizeof(rom_id),"%s",file);
            char *dot=strrchr(rom_id,'.');if(dot)*dot=0;
            FILE *names=system?fopen("game-art-ids.tsv","r"):NULL;
            if(names){
                char line[512];
                while(fgets(line,sizeof(line),names)){
                    char *first=strchr(line,'\t');if(!first)continue;*first++=0;
                    char *second=strchr(first,'\t');if(!second)continue;*second++=0;
                    second[strcspn(second,"\r\n")]=0;
                    if(!strcmp(line,system)&&!strcasecmp(first,rom_id)&&*second){
                        clean(e->title,sizeof(e->title),second);break;
                    }
                }
                fclose(names);
            }
        }
        /* Game information panel: platform instead of internal .so filename.
         * Keep the original ROM/core mapping and poster key unchanged. */
        if(system){
            const char *platform="RETRO GAME";
            if(strstr(system,"Super Nintendo"))platform="SUPER NINTENDO";
            else if(strstr(system,"Nintendo Entertainment System"))platform="NINTENDO NES";
            else if(strstr(system,"Game Boy Advance"))platform="GAME BOY ADVANCE";
            else if(strstr(system,"Game Boy Color"))platform="GAME BOY COLOR";
            else if(strstr(system,"Game Boy"))platform="GAME BOY";
            else if(strstr(system,"PlayStation Portable"))platform="PLAYSTATION PORTABLE";
            else if(strstr(system,"PlayStation"))platform="PLAYSTATION";
            else if(strstr(system,"Neo Geo"))platform="NEO GEO";
            else if(strstr(system,"Mega Drive"))platform="SEGA GENESIS";
            char bundled[PATH_MAX];const char *shown_core=core;
            if(game_bundled_core(core,bundled,sizeof(bundled)))shown_core=bundled;
            const char *core_file=strrchr(shown_core,'/');core_file=core_file?core_file+1:shown_core;
            char core_name[64];snprintf(core_name,sizeof(core_name),"%.63s",core_file);
            FILE *selected=fopen("game-cores.tsv","r");
            if(selected){
                char row[256],saved_title[80],saved_core[96];int selected_id;
                while(fgets(row,sizeof(row),selected)){
                    if(sscanf(row,"%d\t%79[^\t]\t%95[^\n]",&selected_id,saved_title,saved_core)!=3)continue;
                    if(selected_id==e->id){
                        snprintf(core_name,sizeof(core_name),"%.63s",saved_core);
                        break;
                    }
                }
                fclose(selected);
            }
            char preference_key[17];rom_identity(rom_path,preference_key);
            rom_core_preference(preference_key,core_name,sizeof(core_name));
            char *core_suffix=strstr(core_name,"_libretro" FLXTR_CORE_EXT);if(core_suffix)*core_suffix=0;
            snprintf(e->meta,sizeof(e->meta),"%s / CORE: %.48s",platform,core_name);
        }
        char *suffix=strrchr(e->title,'.');
        if(suffix&&(!strcasecmp(suffix,".sfc")||!strcasecmp(suffix,".smc")||
                    !strcasecmp(suffix,".nes")||!strcasecmp(suffix,".gba")||
                    !strcasecmp(suffix,".gb")||!strcasecmp(suffix,".gbc")||
                    !strcasecmp(suffix,".iso")||!strcasecmp(suffix,".cue")))
            *suffix=0;
        /* Canonicalize only obvious ROM aliases, never rewrite launch IDs. */
        const char *aliases[][2]={
            {"CHRONOTRIGGER","Chrono Trigger"},
            {"FINALFANTASYIII","Final Fantasy III"},
            {"SUPER MARIO BROS 2","Super Mario Bros. 2"},
            {"SecretofMana","Secret of Mana"},
            {"SecretofMana2","Seiken Densetsu 3"}
        };
        for(size_t k=0;k<sizeof(aliases)/sizeof(aliases[0]);k++)
            if(!strcasecmp(e->title,aliases[k][0])){
                snprintf(e->title,sizeof(e->title),"%s",aliases[k][1]);break;
            }
    }json_object_put(games);return 0;
}
static int games_list(int page,int refresh){return games_list_filtered(page,refresh,0);}
static int games_system(int page,int folder){return folder>=1&&folder<=10?games_list_filtered(page,0,folder):-1;}
static int game_pixel_geometry(const char *core,const char *rom,int output,int *width,int *height,int *dw,int *dh){
    char executable[PATH_MAX],system[PATH_MAX],save[PATH_MAX],cwd[PATH_MAX];
    if(!games_executable||!getcwd(cwd,sizeof(cwd)))return -1;
    if(game_absolute(games_executable))snprintf(executable,sizeof(executable),"%s",games_executable);
    else{
        char *base=SDL_GetBasePath();const char *name=strrchr(games_executable,'/');name=name?name+1:games_executable;
        int n=base?snprintf(executable,sizeof(executable),"%s%s",base,name):-1;SDL_free(base);
        if(n<1||n>=(int)sizeof(executable))return -1;
    }
    if(snprintf(system,sizeof(system),"%s/system",cwd)>=(int)sizeof(system)||
       snprintf(save,sizeof(save),"%s/core-probe-cache",cwd)>=(int)sizeof(save))return -1;
    char config[PATH_MAX],candidate[PATH_MAX];
    snprintf(config,sizeof(config),"%s/.home/.config/retroarch/retroarch.cfg",games_home());
    FILE *cfg=fopen(config,"r");
    if(cfg){char row[PATH_MAX+64];
        while(fgets(row,sizeof(row),cfg)){
            char *value=row;while(isspace((unsigned char)*value))value++;
            if(strncmp(value,"system_directory",16))continue;
            value+=16;while(isspace((unsigned char)*value))value++;
            if(*value++!='=')continue;
            while(isspace((unsigned char)*value))value++;
            if(*value++!='"')continue;
            char *end=strchr(value,'"');if(!end||(size_t)(end-value)>=sizeof(candidate))continue;
            memcpy(candidate,value,(size_t)(end-value));candidate[end-value]=0;
            if(game_absolute(candidate))snprintf(system,sizeof(system),"%s",candidate);
        }
        fclose(cfg);
    }
    mkdir(save,0700);
    const char *args[]={executable,"--probe-core",core,rom,system,save,output?"fixed":"native",NULL};
    FlxtrSpawnOptions options={.stdout_path="game-geometry.txt",.stderr_path="game-probe.log",.process_tree=1};
    FlxtrProcess child={0};unlink("game-geometry.txt");
    if(flxtr_process_start(&child,executable,args,&options))return -1;
    int code=1,result=flxtr_process_wait(&child,8000,&code);
    if(result!=1){flxtr_process_stop(&child,1);flxtr_process_wait(&child,2000,NULL);return -1;}
    if(code)return -1;
    FILE *f=fopen("game-geometry.txt","r");if(!f)return -1;
    char line[256];int found=0,sw=0,sh=0;
    while(fgets(line,sizeof(line),f))
        if(sscanf(line,"FLXTR_GEOMETRY %d %d %d %d",width,height,&sw,&sh)==4)found=1;
    fclose(f);
    *dw=output?game_output_width(output):sw;*dh=output?output:sh;
    if(!found||*width<1||*height<1||*dw<1||*dh<1||*width>*dw||*height>*dh)return -1;
    return 0;
}
static int games_run(void){
    int id=0,back=-1,start=-1;FILE *f=fopen("game-request","r");if(!f)return 2;
    int fields=fscanf(f,"%d %d %d",&id,&back,&start);fclose(f);unlink("game-request");if(fields<1||id<0||id>GAME_LIMIT)return 2;
    unlink("game-error.txt");
    if(id==0){
        /* Launch the installed ScummVM frontend and its configured games. */
        const char *paths[]={
#ifdef _WIN32
            "scummvm","./scummvm/scummvm.exe",
#else
            "/home/apps/scummvm/scummvm.exec",
            "/home/apps/scummvm/scummvm","/home/apps/scummvm/bin/scummvm",
            "/usr/local/bin/scummvm","/usr/bin/scummvm"
#endif
        };
        /* Launcher options live in FlXtR's own config, not ScummVM's INI.
         * Command-line overrides take effect on the next ScummVM launch. */
        int scumm_size=0,scumm_stretch=3,scumm_filter=0,scumm_fullscreen=1,scumm_subtitles=0;
        FILE *sf=fopen("scummvm-settings.cfg","r");
        if(sf){char line[96];int n;
            while(fgets(line,sizeof(line),sf)){
                if(sscanf(line,"size=%d",&n)==1&&n>=0&&n<5)scumm_size=n;
                if(sscanf(line,"stretch=%d",&n)==1&&n>=0&&n<6)scumm_stretch=n;
                if(sscanf(line,"filter=%d",&n)==1&&n>=0&&n<=1)scumm_filter=n;
                if(sscanf(line,"fullscreen=%d",&n)==1&&n>=0&&n<=1)scumm_fullscreen=n;
                if(sscanf(line,"subtitles=%d",&n)==1&&n>=0&&n<=1)scumm_subtitles=n;
            }
            fclose(sf);
        }
        const char *sizes[]={"","--window-size=640,480","--window-size=800,600",
                            "--window-size=1280,720","--window-size=1920,1080"};
        const char *stretches[]={"--stretch-mode=center","--stretch-mode=pixel-perfect",
            "--stretch-mode=even-pixels","--stretch-mode=fit","--stretch-mode=stretch",
            "--stretch-mode=fit_force_aspect"};
        const char *filter=scumm_filter?"--filtering":"--no-filtering";
        const char *screen=scumm_fullscreen?"--fullscreen":"--no-fullscreen";
        const char *subs=scumm_subtitles?"--subtitles":NULL;
        for(size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
            if(access(paths[i],X_OK)==0){
#ifndef FLXTR_DESKTOP
                /* Match launching the Steam Link app itself: ScummVM must not
                 * inherit FlXtR's private home or relative asset directory. */
                if(!strncmp(paths[i],"/home/apps/scummvm/",19)){
                    if(mkdir("/home/apps/scummvm/.home",0700)&&errno!=EEXIST)return 2;
                    if(mkdir("/home/apps/scummvm/.home/.config",0700)&&errno!=EEXIST)return 2;
                    if(chdir("/home/apps/scummvm")||
                       setenv("HOME","/home/apps/scummvm/.home",1)||
                       setenv("XDG_CONFIG_HOME","/home/apps/scummvm/.home/.config",1))return 2;
                }
#endif
                if(scumm_size)execl(paths[i],paths[i],sizes[scumm_size],stretches[scumm_stretch],filter,screen,subs,(char*)NULL);
                else execl(paths[i],paths[i],stretches[scumm_stretch],filter,screen,subs,(char*)NULL);
                fprintf(stderr,"ScummVM launch failed: %s (%s)\n",paths[i],strerror(errno));
            }
        }
        fprintf(stderr,"ScummVM executable not found in known installation locations\n");
        return 2;
    }
    json_object *games=games_read("games.json");
    if(!games||!json_object_is_type(games,json_type_array)||id>(int)json_object_array_length(games)){json_object_put(games);return 2;}
    json_object *g=json_object_array_get_idx(games,id-1);const char *rom=string(g,"rom"),*core=string(g,"core");
    /* ROM-specific core selection overrides inferred or playlist defaults.
     * Accept names only, never paths from the TSV, and require an installed
     * readable Libretro shared object. */
    char bundled[PATH_MAX];if(game_bundled_core(core,bundled,sizeof(bundled)))core=bundled;
    char selected_core[PATH_MAX]={0},selected_name[96]={0};
    FILE *choices=fopen("game-cores.tsv","r");
    if(choices){
        char row[256],title[80],name[96];int selected_id;
        while(fgets(row,sizeof(row),choices))
            if(sscanf(row,"%d\t%79[^\t]\t%95[^\n]",&selected_id,title,name)==3&&
               selected_id==id&&!strcmp(title,string(g,"title"))&&rom_core_name_valid(name))
                snprintf(selected_name,sizeof(selected_name),"%s",name);
        fclose(choices);
    }
    char preference_key[17];rom_identity(rom,preference_key);
    /* Priority: per-ROM preference, platform default, original core. */
    if(!rom_core_preference(preference_key,selected_name,sizeof(selected_name))&&!selected_name[0])
        rom_platform_core_get(rom_platform_for_path(rom),selected_name,sizeof(selected_name));
    if(selected_name[0]){
        snprintf(selected_core,sizeof(selected_core),"%s/cores/%s_libretro" FLXTR_CORE_EXT,games_home(),selected_name);
        if(!game_file(selected_core))
            snprintf(selected_core,sizeof(selected_core),"%s/.home/.config/retroarch/cores/%s_libretro" FLXTR_CORE_EXT,games_home(),selected_name);
        if(!game_file(selected_core)){
            char cwd[PATH_MAX];
            if(getcwd(cwd,sizeof(cwd))){
                int n=snprintf(selected_core,sizeof(selected_core),"%s/cores/%s_libretro" FLXTR_CORE_EXT,cwd,selected_name);
                if(n<0||n>=(int)sizeof(selected_core))selected_core[0]=0;
            }
        }
        if(game_file(selected_core))core=selected_core;
    }
    if(!game_file(rom)||!game_file(core)){fprintf(stderr,"Game launch: missing ROM or RetroArch core\n");json_object_put(games);return 2;}
    if(strcasestr(rom,"/neogeo/")||strcasestr(rom,"/neo-geo/")){
        char bios[PATH_MAX];snprintf(bios,sizeof(bios),"%s",rom);
        char *slash=strrchr(bios,'/');if(slash)strcpy(slash+1,"neogeo.zip");
        if(!game_file(bios)){
            fprintf(stderr,"Neo Geo requires a legally obtained neogeo.zip BIOS in the ROM directory or a core-supported system directory\n");
            /* Do not block: a configured RetroArch system directory may have BIOS. */
        }
    }
    char config[PATH_MAX],extra[PATH_MAX],runtime[PATH_MAX],home[PATH_MAX];
    if(!getcwd(extra,sizeof(extra)))return 2;
    size_t n=strlen(extra);if(n+22>=sizeof(extra))return 2;strcpy(extra+n,"/game-runtime.cfg");
    f=fopen(extra,"w");if(!f)return 2;
    fputs("config_save_on_exit = \"false\"\nvideo_fullscreen = \"true\"\nmenu_enable_widgets = \"false\"\nmenu_show_start_screen = \"false\"\nkiosk_mode_enable = \"true\"\nmenu_pause_libretro = \"false\"\ninput_menu_toggle = \"nul\"\ninput_menu_toggle_btn = \"nul\"\ninput_menu_toggle_gamepad_combo = \"0\"\ninput_exit_emulator = \"escape\"\n",f);
    int output=GAME_OUTPUT_DEFAULT,view=GAME_FIT,performance=1;
    FILE *display_settings=fopen("settings.cfg","r");
    if(display_settings){char line[96];int height;
        while(fgets(line,sizeof(line),display_settings)){
            /* Retain the experimental output path, but do not apply legacy
             * non-native preferences from settings.cfg to ordinary launches. */
            (void)height;
            if(sscanf(line,"game_view=%d",&height)==1&&game_view_valid(height))view=height;
            if(sscanf(line,"game_performance=%d",&height)==1&&height>=0&&height<3)performance=height;
        }
        fclose(display_settings);
    }
    /* Non-native modes remain implemented for targeted experiments, but
     * normal launches always use native output until hardware modes are validated. */
    if(output){
        fprintf(f,"video_windowed_fullscreen = \"false\"\nvideo_fullscreen_x = \"%d\"\nvideo_fullscreen_y = \"%d\"\n",
            game_output_width(output),output);
        fprintf(stderr,"Game output: request %dx%d; core-native rendering remains unchanged\n",game_output_width(output),output);
    }
    else fputs("video_windowed_fullscreen = \"true\"\nvideo_fullscreen_x = \"0\"\nvideo_fullscreen_y = \"0\"\n",f);
    int native_w=0,native_h=0,display_w=output?game_output_width(output):0,display_h=output;
    /* Stretch uses a custom full-screen viewport: discover the physical
     * desktop dimensions when output is native, rather than leaving 0x0. */
    if(view==GAME_STRETCH&&!output){
        SDL_DisplayMode dm={0};int started=0;
        if(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO)){
            if(!SDL_InitSubSystem(SDL_INIT_VIDEO))started=1;
        }
        if((SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO)&&!SDL_GetCurrentDisplayMode(0,&dm)){
            display_w=dm.w;display_h=dm.h;
        }
        if(started)SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
#ifndef FLXTR_DESKTOP
    if(view==GAME_PIXEL)game_native_sdl();
#endif
    if(view==GAME_PIXEL&&game_pixel_geometry(core,rom,output,&native_w,&native_h,&display_w,&display_h)){
        fclose(f);FILE *error=fopen("game-error.txt","w");
        if(error){fputs("1:1 GEOMETRY UNAVAILABLE OR TOO LARGE / CHOOSE FIT / SEE GAME-PROBE.LOG",error);fclose(error);}
        fprintf(stderr,"1:1 geometry probe failed or picture does not fit selected output\n");
        json_object_put(games);return 2;
    }
    game_scaling_config(f,view,native_w,native_h,display_w,display_h);
    /* Profiles affect rendering/synchronization overhead, never the display
     * mode, CPU core implementation, or the emulated game's clock speed. */
    fputs("video_shader_enable = \"false\"\nvideo_filter = \"\"\n"
          "rewind_enable = \"false\"\nrun_ahead_enabled = \"false\"\n"
          "video_frame_delay = \"0\"\n",f);
    if(performance==0){
        fputs("video_threaded = \"true\"\nvideo_hard_sync = \"false\"\n"
              "video_vsync = \"false\"\n",f);
    }else if(performance==2){
        /* Hard GPU synchronization may reduce input latency at a CPU cost. */
        fputs("video_threaded = \"false\"\nvideo_hard_sync = \"true\"\n"
              "video_hard_sync_frames = \"0\"\nvideo_vsync = \"true\"\n",f);
    }else{
        fputs("video_threaded = \"false\"\nvideo_hard_sync = \"false\"\n"
              "video_vsync = \"true\"\n",f);
    }
    fprintf(stderr,"Game performance profile: %d (0=performance 1=balanced 2=low latency)\\n",performance);
    /* Keep Neo Geo and other working systems on their existing configuration. */
    const char *core_base=strrchr(core,'/');core_base=core_base?core_base+1:core;
    if(!strncmp(core_base,"snes9x",6)||!strncmp(core_base,"mame2003",8)){
        fputs("video_shader_enable = \"false\"\nrewind_enable = \"false\"\nrun_ahead_enabled = \"false\"\nvideo_frame_delay = \"0\"\nvideo_hard_sync = \"false\"\n",f);
    }
    /* FlXtR setting is opt-in; do not change the user's global RetroArch config. */
    { FILE *settings=fopen("settings.cfg","r");int enabled=0;
      if(settings){
          char line[96];
          while(fgets(line,sizeof(line),settings)){
              if(sscanf(line,"game_autostate=%d",&enabled)==1)break;
          }
          fclose(settings);
      }
      fprintf(f,"savestate_auto_save = \"%s\"\nsavestate_auto_load = \"%s\"\n",
          enabled==1?"true":"false",enabled==1?"true":"false");
    }
    if(back>=0&&back<64&&start>=0&&start<64&&back!=start)fprintf(f,"input_enable_hotkey_btn = \"%d\"\ninput_exit_emulator_btn = \"%d\"\n",back,start);
    /* Shared BIOS directory: the UI checks this same location. */
    fprintf(f,"system_directory = \"%.*s/system\"\n",(int)n,extra);
#ifdef FLXTR_DESKTOP
    {
        char data[PATH_MAX];
        if(!getcwd(data,sizeof(data))){fclose(f);return 2;}
        mkdir("system",0700);mkdir("saves",0700);mkdir("states",0700);
        fprintf(f,"savefile_directory = \"%s/saves\"\nsavestate_directory = \"%s/states\"\n",data,data);
    }
#endif
    if(fclose(f))return 2;
#ifdef FLXTR_DESKTOP
    (void)config;(void)runtime;(void)home;
    unlink("game-error.txt");
    return desktop_run_game(core,rom,extra);
#else
    snprintf(config,sizeof(config),"%s/.home/.config/retroarch/retroarch.cfg",games_home());
    snprintf(runtime,sizeof(runtime),"%s/"
#ifdef _WIN32
        "retroarch.exe"
#else
        "retroarch.exec"
#endif
        ,games_home());
    snprintf(home,sizeof(home),"%s/.home",games_home());
    if(view!=GAME_PIXEL)game_native_sdl();
    if(chdir(games_home()))return 2;
    setenv("HOME",home,1);
    fprintf(stderr,"Game launch: core=%s ROM=%s\n",core,rom);
    /* Argument vectors preserve spaces/metacharacters; never execute a playlist as shell text. */
    const char *test=getenv("FLXTR_GAME_TEST_FRAMES");
    if(test&&positive(test,0)>0&&positive(test,0)<=3600)
        execl(runtime,runtime,"--verbose","--config",config,"--appendconfig",extra,"--max-frames",test,"--max-frames-ss","--max-frames-ss-path","/tmp/flxtr-game-test.png","--libretro",core,rom,(char*)NULL);
    else execl(runtime,runtime,"--verbose","--config",config,"--appendconfig",extra,"--libretro",core,rom,(char*)NULL);
    perror("RetroArch runtime");json_object_put(games);return 1;
#endif
}

static json_object *games_archive_manifest(void){
    return games_read(access("archive-games.local.json",R_OK)?"assets/archive-games.json":"archive-games.local.json");
}
static int games_archive_list(int page){
    json_object *list=games_archive_manifest();if(!list||!json_object_is_type(list,json_type_array)){json_object_put(list);return -1;}
    total=(int)json_object_array_length(list);if(total>GAME_LIMIT)total=GAME_LIMIT;
    for(int i=(page-1)*PAGE_SIZE;i<total&&used<PAGE_SIZE;i++){
        json_object *g=json_object_array_get_idx(list,i);Entry *e=&entries[used++];clean(e->title,sizeof(e->title),string(g,"title"));
        clean(e->meta,sizeof(e->meta),string(g,"description"));strcpy(e->kind,"archive-game");e->id=i+1;
        const char *archive_id=string(g,"identifier");
        if(!*string(g,"git_blob_sha1")&&archive_identifier(archive_id))
            snprintf(e->poster,sizeof(e->poster),ARCHIVE "/services/img/%s",archive_id);
    }json_object_put(list);return 0;
}
/* Archive catalog: discovery is metadata-only. Download remains user initiated. */
static const char *game_platform_names[]={"NES","SNES","GAME BOY ADVANCE","PLAYSTATION","PSP","ARCADE"};
static const char *game_platform_queries[]={"NES Nintendo ROM","SNES Super Nintendo ROM","Game Boy Advance GBA ROM","PlayStation PSX game","PSP PlayStation Portable game","Arcade MAME game"};
static int games_platforms(int page){
    total=8;
    for(int i=(page-1)*PAGE_SIZE;i<8&&used<PAGE_SIZE;i++){
        Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"%s",i==7?"ROMSGAMES (PHONE)":i==6?"VERIFIED HOMEBREW":game_platform_names[i]);
        strcpy(e->kind,i==7?"rg-source":i==6?"game-curated":"game-platform");e->id=i+1;
        strcpy(e->meta,i==7?"BROWSE / QR DOWNLOAD + PHONE UPLOAD":i==6?"LICENSED DOWNLOADS / A OPEN":"INTERNET ARCHIVE / BROWSE TITLES / A OPEN");
    }return 0;
}
/* BIOS discovery is intentionally separate from the game installer.
 * A search result is not evidence of a lawful or compatible firmware download. */
static int games_bios_search(int page,int bios_id,const char *query){
    if(bios_id<1||bios_id>5)return -1;
    const char *const defaults[]={"Neo Geo BIOS","PlayStation BIOS","PlayStation BIOS","PlayStation BIOS","Game Boy Advance BIOS"};
    const char *term=*query?query:defaults[bios_id-1];
    char clean_term[128];size_t n=0;
    for(size_t i=0;term[i]&&n+1<sizeof(clean_term);i++)
        if(isalnum((unsigned char)term[i])||term[i]==' '||term[i]=='-')clean_term[n++]=term[i];
    clean_term[n]=0;
    char *escaped=curl_easy_escape(NULL,clean_term,0);if(!escaped)return -1;
    char url[1024];int len=snprintf(url,sizeof(url),ARCHIVE "/advancedsearch.php?q=mediatype%%3Asoftware%%20AND%%20%%28%s%%29%%20AND%%20-access-restricted-item%%3Atrue&fl%%5B%%5D=identifier&fl%%5B%%5D=title&rows=6&page=%d&output=json",escaped,page);
    curl_free(escaped);if(len<0||len>=(int)sizeof(url))return -1;
    json_object *root=archive_json(url);if(!root)return -1;
    json_object *response=field(root,"response"),*docs=field(response,"docs");
    if(!docs||!json_object_is_type(docs,json_type_array)){json_object_put(root);return -1;}
    total=number(response,"numFound");if(total<0)total=0;if(total>12000)total=12000;
    for(size_t i=0;i<(size_t)json_object_array_length(docs)&&used<PAGE_SIZE;i++){
        json_object *doc=json_object_array_get_idx(docs,i);const char *identifier=string(doc,"identifier");
        if(!archive_identifier(identifier))continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),string(doc,"title"));
        if(!*e->title)clean(e->title,sizeof(e->title),identifier);
        snprintf(e->url,sizeof(e->url),ARCHIVE "/details/%s",identifier);
        snprintf(e->poster,sizeof(e->poster),ARCHIVE "/services/img/%s",identifier);
        snprintf(e->meta,sizeof(e->meta),"ARCHIVE RESULT / SOURCE UNVERIFIED");
        strcpy(e->kind,"bios-item");e->id=bios_id;
    }
    json_object_put(root);return 0;
}
static int games_archive_search(int page,int platform,const char *query){
    if(platform<1||platform>6)return -1;
    char term[128];size_t n=0;const char *base=*query?query:game_platform_queries[platform-1];
    for(size_t i=0;base[i]&&n+1<sizeof(term);i++)
        if(isalnum((unsigned char)base[i])||base[i]==' '||base[i]=='-')term[n++]=base[i];
    term[n]=0;
    char *q=curl_easy_escape(NULL,term,0);if(!q)return -1;
    char url[1024];int len=snprintf(url,sizeof(url),ARCHIVE "/advancedsearch.php?q=mediatype%%3Asoftware%%20AND%%20%%28%s%%29%%20AND%%20-access-restricted-item%%3Atrue&fl%%5B%%5D=identifier&fl%%5B%%5D=title&sort%%5B%%5D=downloads%%20desc&rows=6&page=%d&output=json",q,page);
    curl_free(q);if(len<0||len>=(int)sizeof(url))return -1;
    json_object *root=archive_json(url);if(!root)return -1;
    json_object *response=field(root,"response"),*docs=field(response,"docs");
    if(!docs||!json_object_is_type(docs,json_type_array)){json_object_put(root);return -1;}
    total=number(response,"numFound");if(total<0)total=0;if(total>12000)total=12000;
    for(size_t i=0;i<(size_t)json_object_array_length(docs)&&used<PAGE_SIZE;i++){
        json_object *doc=json_object_array_get_idx(docs,i);const char *identifier=string(doc,"identifier");
        if(!archive_identifier(identifier))continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),string(doc,"title"));
        if(!*e->title)clean(e->title,sizeof(e->title),identifier);
        snprintf(e->url,sizeof(e->url),ARCHIVE "/details/%s",identifier);
        snprintf(e->poster,sizeof(e->poster),ARCHIVE "/services/img/%s",identifier);
        snprintf(e->meta,sizeof(e->meta),"%s / A VIEW FILES",game_platform_names[platform-1]);
        strcpy(e->kind,"game-item");e->id=platform;
    }
    json_object_put(root);return 0;
}
static int games_bios_files(int page,const char *identifier,int bios_id){
    if(!archive_identifier(identifier)||bios_id<1||bios_id>5)return -1;
    char url[256];snprintf(url,sizeof(url),ARCHIVE "/metadata/%s",identifier);
    json_object *root=archive_json(url);if(!root)return -1;
    json_object *files=field(root,"files");
    if(!files||!json_object_is_type(files,json_type_array)){json_object_put(root);return -1;}
    const char *names[]={"neogeo.zip","scph5500.bin","scph5501.bin","scph5502.bin","gba_bios.bin"};
    total=0;
    for(size_t i=0;i<(size_t)json_object_array_length(files);i++){
        json_object *f=json_object_array_get_idx(files,i);
        const char *name=string(f,"name"),*digest=string(f,"sha1");
        const char *base=strrchr(name,'/');base=base?base+1:name;
        const char *ext=strrchr(base,'.');
        int container=ext&&!strcasecmp(ext,".zip")&&strlen(name)<140&&strcasestr(base,"bios")!=NULL;
        if(strlen(name)>160||strchr(name,'\\')||strstr(name,"..")||
           (strcasecmp(base,names[bios_id-1])&&!container)||strlen(digest)!=40)continue;
        int good=1;for(int k=0;k<40;k++)if(!isxdigit((unsigned char)digest[k]))good=0;
        if(!good)continue;
        if(total++<(page-1)*PAGE_SIZE||used==PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),name);
        snprintf(e->meta,sizeof(e->meta),"SHA1:%s",digest);
        char *escaped=curl_easy_escape(NULL,name,0);
        if(escaped){snprintf(e->url,sizeof(e->url),ARCHIVE "/download/%s/%s",identifier,escaped);curl_free(escaped);}
        strcpy(e->kind,container?"bios-zip":"bios-file");e->id=bios_id;
    }
    json_object_put(root);return 0;
}
static int games_archive_files(int page,const char *identifier,int platform){
    if(!archive_identifier(identifier)||platform<1||platform>6)return -1;
    char url[256];snprintf(url,sizeof(url),ARCHIVE "/metadata/%s",identifier);
    json_object *root=archive_json(url);if(!root)return -1;
    json_object *files=field(root,"files");
    if(!files||!json_object_is_type(files,json_type_array)){json_object_put(root);return -1;}
    total=0;
    for(size_t i=0;i<(size_t)json_object_array_length(files);i++){
        json_object *f=json_object_array_get_idx(files,i);
        const char *name=string(f,"name"),*ext=strrchr(name,'.');
        if(!ext||strchr(name,'/')||strchr(name,'\\')||strlen(name)>512||strlen(name)<5)continue;
        if(strcasecmp(ext,".zip")&&strcasecmp(ext,".nes")&&strcasecmp(ext,".sfc")&&strcasecmp(ext,".smc")&&
           strcasecmp(ext,".gba")&&strcasecmp(ext,".gb")&&strcasecmp(ext,".gbc")&&
           strcasecmp(ext,".chd")&&strcasecmp(ext,".pbp")&&strcasecmp(ext,".iso"))continue;
        if(total++<(page-1)*PAGE_SIZE||used==PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),name);
        strcpy(e->kind,"game-rom");snprintf(e->meta,sizeof(e->meta),"ARCHIVE FILE / A DOWNLOAD");
        char *escaped=curl_easy_escape(NULL,name,0);if(escaped){
            snprintf(e->url,sizeof(e->url),ARCHIVE "/download/%s/%s",identifier,escaped);
            curl_free(escaped);
        }
        snprintf(e->poster,sizeof(e->poster),ARCHIVE "/services/img/%s",identifier);
        e->id=platform;
    }
    json_object_put(root);return 0;
}
/* Stream large disc images to disk instead of the 16 MiB metadata buffer. */
static size_t game_write(void *ptr,size_t size,size_t count,void *opaque){
    FILE *f=opaque;return fwrite(ptr,size,count,f)*size;
}
/* BusyBox unzip -l archive browser. All member lookups are by index, so UI
 * never constructs a shell command from archive-controlled filenames. */
/* Downloaded ZIP library: only regular ZIP files inside the dedicated directory. */
static int downloaded_zip_path(int id,char *out,size_t cap){
    DIR *dir=opendir("downloads");if(!dir)return -1;
    struct dirent *entry;int count=0,found=-1;
    while((entry=readdir(dir))){
        const char *name=entry->d_name;size_t len=strlen(name);
        if(len<5||strcasecmp(name+len-4,".zip")||strchr(name,'/'))continue;
        char path[PATH_MAX];int n=snprintf(path,sizeof(path),"downloads/%s",name);
        struct stat st;
        if(n<0||n>=(int)sizeof(path)||lstat(path,&st)||!S_ISREG(st.st_mode))continue;
        if(++count==id){if(snprintf(out,cap,"%s",path)<(int)cap)found=0;break;}
    }
    closedir(dir);return found;
}
static int downloaded_zip_list(int page){
    DIR *dir=opendir("downloads");if(!dir)return 0;
    struct dirent *entry;int index=0;total=0;
    while((entry=readdir(dir))){
        const char *name=entry->d_name;size_t len=strlen(name);
        if(len<5||strcasecmp(name+len-4,".zip"))continue;
        char path[PATH_MAX];int n=snprintf(path,sizeof(path),"downloads/%s",name);
        struct stat st;
        if(n<0||n>=(int)sizeof(path)||lstat(path,&st)||!S_ISREG(st.st_mode))continue;
        index++;total++;
        if(total<=(page-1)*PAGE_SIZE||used>=PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),name);
        snprintf(e->meta,sizeof(e->meta),"DOWNLOADED ZIP / A BROWSE / Y DELETE");
        strcpy(e->kind,"download-zip");e->id=index;
    }
    closedir(dir);return 0;
}
static int downloaded_zip_delete(int id){
    char path[PATH_MAX];
    if(downloaded_zip_path(id,path,sizeof(path)))return -1;
    if(unlink(path))return -1;
    fprintf(stderr,"Deleted downloaded ZIP: %s\n",path);
    return downloaded_zip_list(1);
}
static int zip_members(const char *path,int page,int extract_index);
static int zip_game_by_id(int game_id,int page,int member_id){
    if(game_id<0){char relative[PATH_MAX],path[PATH_MAX],cwd[PATH_MAX];
        if(downloaded_zip_path(-game_id,relative,sizeof(relative))||!getcwd(cwd,sizeof(cwd)))return -1;
        int n=snprintf(path,sizeof(path),"%s/%s",cwd,relative);
        if(n<0||n>=(int)sizeof(path))return -1;
        return zip_members(path,page,member_id);
    }
    json_object *games=games_read("games.json");
    if(!games||!json_object_is_type(games,json_type_array)||
       game_id<1||game_id>(int)json_object_array_length(games)){
        json_object_put(games);return -1;
    }
    const char *source=string(json_object_array_get_idx(games,game_id-1),"rom");
    char path[PATH_MAX];
    int n=snprintf(path,sizeof(path),"%s",source);
    json_object_put(games);
    if(n<0||n>=(int)sizeof(path))return -1;
    return zip_members(path,page,member_id);
}
static int zip_members(const char *path,int page,int extract_index){
    if(!game_absolute(path)||!game_file(path)||strcasecmp(path+strlen(path)-4,".zip"))return -1;
#ifdef _WIN32
    FILE *in=flxtr_archive_listing(path);int code=0;if(!in)return -1;
#else
    int pipes[2];if(pipe(pipes))return -1;
    pid_t pid=fork();
    if(pid<0){close(pipes[0]);close(pipes[1]);return -1;}
    if(pid==0){close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);close(pipes[1]);
        execlp("unzip","unzip","-l",path,(char*)NULL);_exit(127);}
    close(pipes[1]);FILE *in=fdopen(pipes[0],"r");
    if(!in){close(pipes[0]);waitpid(pid,NULL,0);return -1;}
#endif
    char line[2048],member[1024];int index=0,found=0;total=0;
    while(fgets(line,sizeof(line),in)){
        unsigned long long size=0;char date[32],timepart[32];
        if(sscanf(line," %llu %31s %31s %1023[^\n]",&size,date,timepart,member)!=4)continue;
        const char *ext=strrchr(member,'.');
        if(!ext||member[0]=='/'||strstr(member,"..")||strchr(member,'\\')||strpbrk(member,"\r\t")||
            (strcasecmp(ext,".nes")&&strcasecmp(ext,".sfc")&&strcasecmp(ext,".smc")&&
             strcasecmp(ext,".gba")&&strcasecmp(ext,".gb")&&strcasecmp(ext,".gbc")&&
             strcasecmp(ext,".md")&&strcasecmp(ext,".gen")&&strcasecmp(ext,".bin")&&
             strcasecmp(ext,".a26")&&strcasecmp(ext,".cue")))continue;
        if(size==0||size>128ULL*1024*1024)continue;
        index++;total++;
        if(extract_index){
            if(index!=extract_index)continue;
            found=1;break;
        }
        if(total<=(page-1)*PAGE_SIZE||used>=PAGE_SIZE)continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),member);
        strcpy(e->kind,"zip-member");e->id=index;
        snprintf(e->meta,sizeof(e->meta),"ZIP ROM / %llu BYTES / A EXTRACT",size);
    }
    fclose(in);
#ifndef _WIN32
    int code=0;waitpid(pid,&code,0);
#endif
    if(extract_index&&!found)return -1;
    if(!extract_index)return WIFEXITED(code)&&WEXITSTATUS(code)==0?0:-1;
    /* Route ROMs by their extension, retaining their actual filename.
     * Ambiguous .bin entries use the parent ZIP's console directory. */
    const char *ext=strrchr(member,'.');
    const char *folder=NULL;
    if(!strcasecmp(ext,".nes"))folder="NES";
    else if(!strcasecmp(ext,".sfc")||!strcasecmp(ext,".smc"))folder="SNES";
    else if(!strcasecmp(ext,".gba"))folder="GBA";
    else if(!strcasecmp(ext,".gb"))folder="GB";
    else if(!strcasecmp(ext,".gbc"))folder="GBC";
    else if(!strcasecmp(ext,".md")||!strcasecmp(ext,".gen"))folder="MegaDrive";
    else if(!strcasecmp(ext,".a26"))folder="Atari2600";
    else if(!strcasecmp(ext,".cue")||!strcasecmp(ext,".bin"))folder="PlayStation";
    if(!folder)return -1;
    const char *base=strrchr(member,'/');base=base?base+1:member;
    if(!*base||strlen(base)>240||base[0]=='.')return -1;
    for(const unsigned char *p=(const unsigned char*)base;*p;p++)
        if(*p<32||*p==127||*p=='/'||*p=='\\')return -1;
    /* Save under the RetroArch ROM tree that the importer already scans. */
    char directory[PATH_MAX],dest[PATH_MAX],tmp[PATH_MAX];
    int n=snprintf(directory,sizeof(directory),"%s/roms/%s",games_home(),folder);
    if(n<0||n>=(int)sizeof(directory))return -1;
    char root[PATH_MAX];
    n=snprintf(root,sizeof(root),"%s/roms",games_home());
    if(n<0||n>=(int)sizeof(root))return -1;
    if(mkdir(root,0700)&&errno!=EEXIST)return -1;
    if(mkdir(directory,0700)&&errno!=EEXIST)return -1;
    n=snprintf(dest,sizeof(dest),"%s/%s",directory,base);
    if(n<0||n>=(int)sizeof(dest))return -1;
    n=snprintf(tmp,sizeof(tmp),"%s.next",dest);
    if(n<0||n>=(int)sizeof(tmp))return -1;
    if(access(dest,F_OK)==0){
        fprintf(stderr,"ZIP ROM already exists, refusing overwrite: %s\n",dest);
        return games_list(1,1);
    }
    int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return -1;
#ifdef _WIN32
    code=flxtr_archive_extract(path,member,fd,128*1024*1024)?256:0;close(fd);struct stat st;
#else
    pid=fork();if(pid<0){close(fd);unlink(tmp);return -1;}
    if(pid==0){dup2(fd,STDOUT_FILENO);close(fd);execlp("unzip","unzip","-p",path,member,(char*)NULL);_exit(127);}
    close(fd);waitpid(pid,&code,0);struct stat st;
#endif
    if(!WIFEXITED(code)||WEXITSTATUS(code)!=0||stat(tmp,&st)||st.st_size==0||st.st_size>128*1024*1024||link(tmp,dest)){
        unlink(tmp);return -1;
    }
    unlink(tmp);
    fprintf(stderr,"ZIP ROM extracted: %s\n",dest);
    return games_list(1,1);
}
/* Progress is written independently of the catalog result stream. */
static int game_download_progress(void *ctx,double total,double done,
                                  double upload_total,double upload_done){
    (void)ctx;(void)upload_total;(void)upload_done;
    static double last_done=-1;
    static time_t last_time;
    time_t now=time(NULL);
    if(done==last_done&&now==last_time)return 0;
    if(now==last_time&&total>0&&done<total)return 0;
    FILE *f=fopen("catalog-cache/download-progress.next","w");
    if(f){
        fprintf(f,"%lld %lld\n",(long long)done,(long long)total);
        if(!fclose(f))rename("catalog-cache/download-progress.next","catalog-cache/download-progress");
    }
    last_done=done;last_time=now;
    return 0;
}
#if LIBCURL_VERSION_NUM >= 0x072000
static int game_download_xfer(void *ctx,curl_off_t total,curl_off_t done,curl_off_t ut,curl_off_t ud){
    return game_download_progress(ctx,(double)total,(double)done,(double)ut,(double)ud);
}
#endif
static int games_archive_file_download(const char *item,const char *name,int platform){
    fprintf(stderr,"Archive ROM request: item=%.128s filename=%.180s platform=%d\n",item?item:"(null)",name?name:"(null)",platform);
    if(!archive_identifier(item)||!name||!*name||strlen(name)>512||
       strchr(name,'/')||strchr(name,'\\')||strstr(name,"..")||strpbrk(name,"\r\n\t"))return -1;
    const char *extension=strrchr(name,'.');
    if(!extension|| (strcasecmp(extension,".zip")&&strcasecmp(extension,".nes")&&
        strcasecmp(extension,".sfc")&&strcasecmp(extension,".smc")&&
        strcasecmp(extension,".gba")&&strcasecmp(extension,".gb")&&
        strcasecmp(extension,".gbc")&&strcasecmp(extension,".chd")&&
        strcasecmp(extension,".pbp")&&strcasecmp(extension,".iso")))return -1;
    char *escaped=curl_easy_escape(NULL,name,0);if(!escaped)return -1;
    char url[1024];int n=snprintf(url,sizeof(url),ARCHIVE "/download/%s/%s",item,escaped);curl_free(escaped);
    if(n<0||n>=(int)sizeof(url)){fprintf(stderr,"Archive download URL exceeds buffer\n");return -1;}
    mkdir("roms",0700);
    mkdir("downloads",0700);
    const char *folder[]={"","NES","SNES","GBA","PlayStation","PSP","MAME"};
    if(platform<1||platform>6)return -1;
    char rom_dir[80];snprintf(rom_dir,sizeof(rom_dir),"%s",!strcasecmp(extension,".zip")?"downloads":"roms");
    if(strcasecmp(extension,".zip"))snprintf(rom_dir,sizeof(rom_dir),"roms/%s",folder[platform]);
    mkdir(rom_dir,0700);
    /* Archive names may exceed the filesystem's 255-byte component limit.
     * Keep a recognizable prefix and extension, plus a stable full-name hash. */
    unsigned long hash=2166136261UL;
    for(const unsigned char *p=(const unsigned char*)name;*p;p++)
        hash=((hash^*p)*16777619UL)&0xffffffffUL;
    char prefix[65];size_t j=0;
    for(const unsigned char *p=(const unsigned char*)name;*p&&j<60;p++){
        unsigned char c=*p;
        prefix[j++]=(isalnum(c)||c=='-'||c=='_')?(char)c:'_';
    }
    prefix[j]=0;
    char target[400],tmp[420];
    n=snprintf(target,sizeof(target),"%s/%.64s-%s-%08lx%s",rom_dir,item,prefix,hash,extension);
    if(n<0||n>=(int)sizeof(target))return -1;
    n=snprintf(tmp,sizeof(tmp),"%s.next",target);
    if(n<0||n>=(int)sizeof(tmp))return -1;
    FILE *f=fopen(tmp,"wb");if(!f){fprintf(stderr,"Archive ROM file open failed: %s (%s)\n",tmp,strerror(errno));return -1;}
    CURL *c=curl_easy_init();if(!c){fclose(f);unlink(tmp);return -1;}
    curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
#if LIBCURL_VERSION_NUM >= 0x072000
    curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,game_download_xfer);
#else
    curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,game_download_progress);
#endif
    curl_easy_setopt(c,CURLOPT_PROGRESSDATA,NULL);
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_MAXREDIRS,4L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,game_write);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,f);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,45L);
    curl_easy_setopt(c,CURLOPT_CAINFO,FLXTR_CA_BUNDLE);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    fprintf(stderr,"Archive ROM starting HTTPS transfer to %s\n",target);
    CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
    curl_easy_cleanup(c);int failed=fclose(f)!=0||rc!=CURLE_OK||http!=200;
    struct stat st;if(stat(tmp,&st)||st.st_size<=0)failed=1;
    if(!failed&&rename(tmp,target))failed=1;
    if(failed){unlink(tmp);fprintf(stderr,"Archive ROM download failed: HTTP %ld, curl %d (%s), file %s\n",http,(int)rc,curl_easy_strerror(rc),name);return -1;}
    fprintf(stderr,"Archive ROM downloaded: %s\n",target);
    /* Download completion is not contingent on a compatible installed core.
     * The user can choose a core later; the archive transfer itself succeeded. */
    return games_list(1,1);
}
static int games_archive_download(int id){
    json_object *list=games_archive_manifest();
    if(!list||!json_object_is_type(list,json_type_array)||id<1||id>(int)json_object_array_length(list)){json_object_put(list);return -1;}
    json_object *g=json_object_array_get_idx(list,id-1);
    const char *item=string(g,"identifier"),*name=string(g,"file"),*sha=string(g,"sha256");
    const char *blob=string(g,"git_blob_sha1");
    const char *revision="50293559a496a3e20382fbf6a2e84b70ec622f88";
    int github=*blob!=0;
    if(!archive_identifier(item)||!game_core(name)||strlen(name)>160||strchr(name,'/')||strchr(name,'\\')||strchr(name,'\n')||strchr(name,'\t')||
       (github?strlen(blob)!=40:strlen(sha)!=64)){json_object_put(list);return -1;}
    for(const char *p=github?blob:sha;*p;p++)if(!isxdigit((unsigned char)*p)){json_object_put(list);return -1;}
    char url[1024];
    if(github){
        /* Only fetch pinned, redistributable Homebrew Hub database artifacts.
         * Caller-controlled URLs are deliberately not accepted. */
        if(snprintf(url,sizeof(url),"https://raw.githubusercontent.com/gbdev/database/%s/entries/%s/%s",revision,item,name)>=(int)sizeof(url)){json_object_put(list);return -1;}
    }else{
        char *escaped=curl_easy_escape(NULL,name,0);if(!escaped){json_object_put(list);return -1;}
        int n=snprintf(url,sizeof(url),ARCHIVE "/download/%s/%s",item,escaped);curl_free(escaped);
        if(n<0||n>=(int)sizeof(url)){json_object_put(list);return -1;}
    }
    Buffer data={NULL,0,16*1024*1024};int rc=fetch(url,&data);
    unsigned char digest[32];char hex[65];
    if(!rc){
        if(github){
            /* Git blob SHA-1 authenticates exact file contents at the pinned revision. */
            EVP_MD_CTX *ctx=EVP_MD_CTX_create();char header[64];int h=snprintf(header,sizeof(header),"blob %zu",data.length);
            if(!ctx||!EVP_DigestInit_ex(ctx,EVP_sha1(),NULL)||
               !EVP_DigestUpdate(ctx,header,(size_t)h+1)||
               !EVP_DigestUpdate(ctx,data.data,data.length)||!EVP_DigestFinal_ex(ctx,digest,NULL)){
                EVP_MD_CTX_destroy(ctx);free(data.data);json_object_put(list);return -1;
            }
            EVP_MD_CTX_destroy(ctx);
            for(int i=0;i<20;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
            if(strcasecmp(hex,blob))rc=-1;
        }else{
            SHA256((unsigned char*)data.data,data.length,digest);
            for(int i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
            if(strcasecmp(hex,sha))rc=-1;
        }
    }
    if(!rc){
        mkdir("roms",0700);char path[400],tmp[420];snprintf(path,sizeof(path),"roms/%s-%s",item,name);snprintf(tmp,sizeof(tmp),"%s.next",path);
        FILE *f=fopen(tmp,"wb");if(!f)rc=-1;else{if(fwrite(data.data,1,data.length,f)!=data.length)rc=-1;if(fclose(f))rc=-1;if(!rc&&rename(tmp,path))rc=-1;if(rc)unlink(tmp);}
    }
    if(rc)fprintf(stderr,"Game download failed (HTTP, integrity verification, destination or size limit); item %d\n",id);
    else fprintf(stderr,"Game downloaded and verified; refresh local games to see playable ROMs (a matching RetroArch core is required)\n");
    free(data.data);json_object_put(list);return rc?rc:games_list(1,1);
}
