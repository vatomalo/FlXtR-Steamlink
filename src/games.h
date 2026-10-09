/* Local RetroArch importer. No ROMs or cores are downloaded. */
#include <dirent.h>
#include <limits.h>
#include <openssl/sha.h>
#define GAME_LIMIT 2048
static const char *games_home(void){const char *p=getenv("FLXTR_RETROARCH_HOME");return p&&p[0]=='/'?p:"/home/apps/retroarch";}
static int game_file(const char *p){struct stat st;return p&&p[0]=='/'&&!stat(p,&st)&&S_ISREG(st.st_mode)&&!access(p,R_OK);}
static json_object *games_read(const char *path){struct stat st;if(stat(path,&st)||st.st_size>8*1024*1024)return NULL;return json_object_from_file(path);}
static const char *game_core(const char *path){
    const char *ext=strrchr(path,'.');if(!ext)return NULL;ext++;
    if(!strcasecmp(ext,"nes"))return "fceumm";
    if(!strcasecmp(ext,"sfc")||!strcasecmp(ext,"smc"))return "snes9x2005_plus";
    if(!strcasecmp(ext,"gba"))return "gpsp";
    if(!strcasecmp(ext,"gb")||!strcasecmp(ext,"gbc"))return "gambatte";
    if(!strcasecmp(ext,"md")||!strcasecmp(ext,"gen")||!strcasecmp(ext,"smd")||!strcasecmp(ext,"sms")||!strcasecmp(ext,"gg"))return "picodrive";
    if(!strcasecmp(ext,"cue")||!strcasecmp(ext,"pbp")||!strcasecmp(ext,"chd")||!strcasecmp(ext,"m3u"))return "pcsx_rearmed";
    if(!strcasecmp(ext,"a26"))return "stella";
    if(!strcasecmp(ext,"zip")){
        /* Neo Geo uses a shared BIOS ZIP alongside game ZIPs. Never launch
         * neogeo.zip itself as a game. Prefer FBNeo when installed. */
        const char *base=strrchr(path,'/');base=base?base+1:path;
        if(!strcasecmp(base,"neogeo.zip"))return NULL;
        if(strcasestr(path,"/neogeo/")||strcasestr(path,"/neo-geo/"))return "fbneo";
        if(strcasestr(path,"/mame/"))return "mame2003_plus";
        if(strcasestr(path,"/nes/"))return "fceumm";
        if(strcasestr(path,"/snes/"))return "snes9x2005_plus";
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
    snprintf(core,sizeof(core),"%s/cores/%s_libretro.so",games_home(),name);
    if(!game_file(core))snprintf(core,sizeof(core),"%s/.home/.config/retroarch/cores/%s_libretro.so",games_home(),name);
    /* Steam Link RetroArch installations may ship MAME instead of FBNeo. */
    if(!game_file(core)&&!strcmp(name,"fbneo")){
        snprintf(core,sizeof(core),"%s/cores/mame2003_plus_libretro.so",games_home());
        if(!game_file(core))snprintf(core,sizeof(core),"%s/.home/.config/retroarch/cores/mame2003_plus_libretro.so",games_home());
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
    FILE *f=fopen("games-paths.txt","r");if(f){while(fgets(path,sizeof(path),f)){path[strcspn(path,"\r\n")]=0;if(path[0]=='/')game_scan(games,path,0,0);}fclose(f);}
    if(!json_object_to_file("games.json.next",games))rename("games.json.next","games.json");
    return games;
}
static const char *const local_folders[]={"NES","SNES","GAME BOY","GAME BOY COLOR","GAME BOY ADVANCE","PLAYSTATION","PSP","NEO GEO","ARCADE","OTHER"};
static int local_category(const char *core,const char *rom){
    if(strstr(core,"fceumm"))return 1;
    if(strstr(core,"snes9x"))return 2;
    if(strstr(core,"gambatte"))return strcasestr(rom,".gbc")?4:3;
    if(strstr(core,"gpsp"))return 5;
    if(strstr(core,"pcsx_rearmed"))return 6;
    if(strstr(core,"ppsspp"))return 7;
    if(strstr(core,"fbneo")||strcasestr(rom,"/neogeo/"))return 8;
    if(strstr(core,"mame"))return 9;
    return 10;
}
static int games_folders(int page){
    total=10;
    for(int i=(page-1)*PAGE_SIZE;i<10&&used<PAGE_SIZE;i++){
        Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"%s",local_folders[i]);
        snprintf(e->meta,sizeof(e->meta),"[ %s ] / A OPEN FOLDER",local_folders[i]);
        strcpy(e->kind,"game-folder");e->id=i+1;
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
        const char *core=string(g,"core"),*base=strrchr(core,'/');clean(e->meta,sizeof(e->meta),base?base+1:core);strcpy(e->kind,"game");e->id=i+1;
        /* Prefer RetroArch's locally downloaded Named_Boxarts thumbnails. */
        const char *rom=string(g,"rom"),*file=strrchr(rom,'/');file=file?file+1:rom;
        const char *systems[]={"Nintendo - Nintendo Entertainment System","Nintendo - Super Nintendo Entertainment System","Nintendo - Game Boy Advance","Nintendo - Game Boy Color","Nintendo - Game Boy","Sony - PlayStation","Sony - PlayStation Portable","SNK - Neo Geo"};
        const char *needle[]={"fceumm","snes9x","gpsp","gambatte","gambatte","pcsx_rearmed","ppsspp","fbneo"};
        const char *system=NULL;
        for(int k=0;k<8;k++)if(strstr(core,needle[k])){system=systems[k];break;}
        if(system){
            char stem[128];snprintf(stem,sizeof(stem),"%s",e->title);
            char thumb[PATH_MAX];
            snprintf(thumb,sizeof(thumb),"%s/.home/.config/retroarch/thumbnails/%s/Named_Boxarts/%s.png",games_home(),system,stem);
            if(!game_file(thumb))snprintf(thumb,sizeof(thumb),"%s/thumbnails/%s/Named_Boxarts/%s.png",games_home(),system,stem);
            if(game_file(thumb)){
                const char prefix[]="localthumb:";
                size_t length=strlen(thumb);
                if(length<sizeof(e->poster)-sizeof(prefix)){
                    memcpy(e->poster,prefix,sizeof(prefix)-1);
                    memcpy(e->poster+sizeof(prefix)-1,thumb,length+1);
                }
            }
        }
    }json_object_put(games);return 0;
}
static int games_list(int page,int refresh){return games_list_filtered(page,refresh,0);}
static int games_system(int page,int folder){return folder>=1&&folder<=10?games_list_filtered(page,0,folder):-1;}
static int games_run(void){
    int id=0,back=-1,start=-1;FILE *f=fopen("game-request","r");if(!f)return 2;
    int fields=fscanf(f,"%d %d %d",&id,&back,&start);fclose(f);unlink("game-request");if(fields<1||id<1||id>GAME_LIMIT)return 2;
    json_object *games=games_read("games.json");
    if(!games||!json_object_is_type(games,json_type_array)||id>(int)json_object_array_length(games)){json_object_put(games);return 2;}
    json_object *g=json_object_array_get_idx(games,id-1);const char *rom=string(g,"rom"),*core=string(g,"core");
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
    fputs("config_save_on_exit = \"false\"\nvideo_fullscreen = \"true\"\nmenu_enable_widgets = \"false\"\nmenu_show_start_screen = \"false\"\nmenu_pause_libretro = \"false\"\ninput_menu_toggle = \"nul\"\ninput_menu_toggle_btn = \"nul\"\ninput_menu_toggle_gamepad_combo = \"0\"\ninput_exit_emulator = \"escape\"\n",f);
    if(back>=0&&back<64&&start>=0&&start<64&&back!=start)fprintf(f,"input_enable_hotkey_btn = \"%d\"\ninput_exit_emulator_btn = \"%d\"\n",back,start);
    /* Shared BIOS directory: the UI checks this same location. */
    fprintf(f,"system_directory = \"%.*s/system\"\n",(int)n,extra);
    if(fclose(f))return 2;
    snprintf(config,sizeof(config),"%s/.home/.config/retroarch/retroarch.cfg",games_home());
    snprintf(runtime,sizeof(runtime),"%s/retroarch.exec",games_home());
    snprintf(home,sizeof(home),"%s/.home",games_home());
    if(chdir(games_home()))return 2;
    setenv("HOME",home,1);
    /* Argument vectors preserve spaces/metacharacters; never execute a playlist as shell text. */
    const char *test=getenv("FLXTR_GAME_TEST_FRAMES");
    if(test&&positive(test,0)>0&&positive(test,0)<=3600)
        execl(runtime,runtime,"--config",config,"--appendconfig",extra,"--max-frames",test,"--max-frames-ss","--max-frames-ss-path","/tmp/flxtr-game-test.png","--libretro",core,rom,(char*)NULL);
    else execl(runtime,runtime,"--config",config,"--appendconfig",extra,"--libretro",core,rom,(char*)NULL);
    perror("RetroArch runtime");json_object_put(games);return 1;
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
    total=7;
    for(int i=(page-1)*PAGE_SIZE;i<7&&used<PAGE_SIZE;i++){
        Entry *e=&entries[used++];snprintf(e->title,sizeof(e->title),"%s",i==6?"VERIFIED HOMEBREW":game_platform_names[i]);
        strcpy(e->kind,i==6?"game-curated":"game-platform");e->id=i+1;
        strcpy(e->meta,i==6?"LICENSED DOWNLOADS / A OPEN":"INTERNET ARCHIVE / BROWSE TITLES / A OPEN");
    }return 0;
}
static int games_archive_search(int page,int platform,const char *query){
    if(platform<1||platform>6)return -1;
    char term[128];size_t n=0;const char *base=*query?query:game_platform_queries[platform-1];
    for(size_t i=0;base[i]&&n+1<sizeof(term);i++)
        if(isalnum((unsigned char)base[i])||base[i]==' '||base[i]=='-')term[n++]=base[i];
    term[n]=0;
    char *q=curl_easy_escape(NULL,term,0);if(!q)return -1;
    char url[1024];int len=snprintf(url,sizeof(url),ARCHIVE "/advancedsearch.php?q=mediatype%%3Asoftware%%20AND%%20%%28%s%%29%%20AND%%20-access-restricted-item%%3Atrue&fl%%5B%%5D=identifier&fl%%5B%%5D=title&rows=6&page=%d&output=json",q,page);
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
        if(!ext||strchr(name,'/')||strchr(name,'\\')||strlen(name)>160||strlen(name)<5)continue;
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
static int games_archive_file_download(const char *item,const char *name){
    if(!archive_identifier(item)||!name||!*name||strlen(name)>160||
       strchr(name,'/')||strchr(name,'\\')||strstr(name,"..")||strpbrk(name,"\r\n\t"))return -1;
    const char *extension=strrchr(name,'.');
    if(!extension|| (strcasecmp(extension,".zip")&&strcasecmp(extension,".nes")&&
        strcasecmp(extension,".sfc")&&strcasecmp(extension,".smc")&&
        strcasecmp(extension,".gba")&&strcasecmp(extension,".gb")&&
        strcasecmp(extension,".gbc")&&strcasecmp(extension,".chd")&&
        strcasecmp(extension,".pbp")&&strcasecmp(extension,".iso")))return -1;
    char *escaped=curl_easy_escape(NULL,name,0);if(!escaped)return -1;
    char url[1024];int n=snprintf(url,sizeof(url),ARCHIVE "/download/%s/%s",item,escaped);curl_free(escaped);
    if(n<0||n>=(int)sizeof(url))return -1;
    mkdir("roms",0700);
    char target[400],tmp[420];snprintf(target,sizeof(target),"roms/%s-%s",item,name);
    snprintf(tmp,sizeof(tmp),"%s.next",target);
    FILE *f=fopen(tmp,"wb");if(!f)return -1;
    CURL *c=curl_easy_init();if(!c){fclose(f);unlink(tmp);return -1;}
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_MAXREDIRS,4L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,game_write);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,f);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,45L);
    curl_easy_setopt(c,CURLOPT_CAINFO,access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem");
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
    curl_easy_cleanup(c);int failed=fclose(f)!=0||rc!=CURLE_OK||http!=200;
    struct stat st;if(stat(tmp,&st)||st.st_size<=0)failed=1;
    if(!failed&&rename(tmp,target))failed=1;
    if(failed){unlink(tmp);fprintf(stderr,"Archive ROM download failed: HTTP %ld, curl %d\n",http,(int)rc);return -1;}
    fprintf(stderr,"Archive ROM downloaded: %s\n",target);
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
            SHA_CTX ctx;char header[64];int h=snprintf(header,sizeof(header),"blob %zu",data.length);
            SHA1_Init(&ctx);SHA1_Update(&ctx,header,(size_t)h+1);
            SHA1_Update(&ctx,data.data,data.length);SHA1_Final(digest,&ctx);
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
