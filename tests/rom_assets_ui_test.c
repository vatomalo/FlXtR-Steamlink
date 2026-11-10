#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void done(void){
    Uint32 start=SDL_GetTicks();
    while(rom_asset_pid&&SDL_GetTicks()-start<10000){rom_assets_tick();SDL_Delay(5);}
    assert(!rom_asset_pid&&rom_asset_count>0);
}
static void snapshot(const char *folder,const char *name){
    if(!folder)return;
    rom_assets_draw();
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,W,H,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch));
    char path[PATH_MAX];snprintf(path,sizeof(path),"%s/%s.bmp",folder,name);assert(!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
int main(int argc,char **argv){
    char repo[PATH_MAX],catalog[PATH_MAX],tool[PATH_MAX];assert(getcwd(repo,sizeof(repo)));
    assert(snprintf(catalog,sizeof(catalog),"%s/build/greenlink-catalog",repo)<(int)sizeof(catalog));
    assert(snprintf(tool,sizeof(tool),"%s/tools/rom_assets.py",repo)<(int)sizeof(tool));
    char temp[]="/tmp/flxtr-rom-assets-ui-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    assert(!mkdir("cores",0700));assert(!mkdir("scripts",0700));assert(!mkdir("tools",0700));assert(!mkdir("cheats",0700));
    assert(!symlink(catalog,"greenlink-catalog"));assert(!symlink(tool,"tools/rom_assets.py"));
    FILE *f=fopen("scripts/music-runtime.sh","w");assert(f);fputs("#!/bin/sh\nexec python3 \"$@\"\n",f);fclose(f);
    f=fopen("cores/fceumm_libretro.so","w");assert(f);fclose(f);
    char rom[PATH_MAX],donor[PATH_MAX];snprintf(rom,sizeof(rom),"%s/Test.nes",temp);snprintf(donor,sizeof(donor),"%s/Donor.nes",temp);
    f=fopen("games.json","w");assert(f);
    fprintf(f,"[{\"title\":\"Target\",\"rom\":\"%s\",\"core\":\"%s/cores/fceumm_libretro.so\"},{\"title\":\"Donor\",\"rom\":\"%s\",\"core\":\"%s/cores/fceumm_libretro.so\"}]",rom,temp,donor,temp);fclose(f);
    char key[17],url[32];rom_identity(rom,key);snprintf(url,sizeof(url),"romid:%s",key);
    Title selected={.kind="game",.title="Target",.id=1};strcpy(selected.url,url);
    assert(!SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO));assert(!open_ui());
    rom_assets_open(&selected);assert(rom_assets_on&&!rom_options_menu&&!rom_asset_pid);
    snapshot(argc>1?argv[1]:NULL,"rom-core");
    action(SDLK_RETURN);assert(rom_core_menu);action(SDLK_ESCAPE);assert(!rom_core_menu&&rom_assets_on);
    action(SDLK_m);assert(rom_asset_tab==ROM_ASSET_COVER);done();assert(rom_asset_count==2);
    action(SDLK_DOWN);snapshot(argc>1?argv[1]:NULL,"rom-cover");
    action(SDLK_RETURN);done();assert(rom_asset_cover_dirty);
    f=fopen("rom-covers.tsv","r");char line[1024];assert(f&&fgets(line,sizeof(line),f));fclose(f);
    assert(strstr(line,"Donor")&&strstr(line,"gameart:"));
    char path[96];snprintf(path,sizeof(path),"cheats/%s.cht",key);
    f=fopen(path,"w");assert(f);fputs("cheats = 1\ncheat0_desc = \"Infinite lives\"\ncheat0_code = \"SXIOPO\"\ncheat0_enable = false\n",f);fclose(f);
    action(SDLK_m);done();assert(rom_asset_tab==ROM_ASSET_CHEATS&&rom_asset_count==2);
    action(SDLK_DOWN);action(SDLK_RETURN);done();assert(!strcmp(rom_asset_rows[1].detail,"ON"));
    snapshot(argc>1?argv[1]:NULL,"rom-cheats");
    f=fopen("Test.state3","w");assert(f);fputs("original state",f);fclose(f);
    action(SDLK_m);done();assert(rom_asset_tab==ROM_ASSET_STATES&&rom_asset_count>=3);
    action(SDLK_RIGHT);done();assert(strstr(rom_asset_rows[0].label,"SLOT: 1"));
    f=fopen("Test.state3","w");assert(f);fputs("replaced state",f);fclose(f);
    action(SDLK_DOWN);action(SDLK_DOWN);action(SDLK_RETURN);assert(rom_asset_confirm[0]);
    action(SDLK_ESCAPE);assert(!rom_asset_confirm[0]&&rom_assets_on);
    action(SDLK_RETURN);action(SDLK_RETURN);done();assert(!rom_asset_confirm[0]);
    f=fopen("Test.state3","r");assert(f&&fgets(line,sizeof(line),f));fclose(f);assert(!strcmp(line,"original state"));
    snapshot(argc>1?argv[1]:NULL,"rom-states");
    action(SDLK_m);done();assert(rom_asset_tab==ROM_ASSET_AUDIO&&rom_asset_count==2);
    assert(strstr(rom_asset_rows[0].label,"OFF"));
    action(SDLK_RETURN);done();assert(strstr(rom_asset_rows[0].label,"ON"));
    assert(rom_background_music(key));
    action(SDLK_DOWN);action(SDLK_RETURN);assert(!rom_asset_pid);
    snapshot(argc>1?argv[1]:NULL,"rom-audio");
    rom_asset_cover_dirty=0;action(SDLK_ESCAPE);assert(!rom_assets_on&&!game_exit);
    close_ui();SDL_Quit();assert(!chdir(repo));
    puts("PASS: Y tabs, core submenu, asynchronous existing-cover selection and persistent per-ROM cheat toggles");
}
