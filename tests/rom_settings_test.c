#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char temp[]="/tmp/flxtr-rom-settings-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    assert(!mkdir("cores",0700));
    FILE *f=fopen("cores/test_libretro.so","w");assert(f);fclose(f);
    settings_on=1;settings_tab=SETTINGS_ROMS;settings_row=2;
    assert(settings_tabs[SETTINGS_ROMS].count==6&&settings_id()==28);
    game_performance=1;action(SDLK_LEFT);assert(game_performance==0);
    action(SDLK_LEFT);assert(game_performance==2);
    save_settings();game_performance=1;load_settings();assert(game_performance==2);
    settings_row=5;assert(settings_id()==27);action(SDLK_RIGHT);assert(!platform_core_menu);
    action(SDLK_RETURN);assert(platform_core_menu&&!platform_core_selecting);
    for(int i=0;i<6;i++)action(SDLK_DOWN);
    assert(platform_core_index==6);
    action(SDLK_RETURN);assert(platform_core_selecting&&rom_core_count>=2);
    int selected=0;for(int i=1;i<rom_core_count;i++)if(!strcmp(rom_core_names[i],"test"))selected=i;
    assert(selected);platform_core_choice=selected;action(SDLK_RETURN);
    char name[96]={0};assert(rom_platform_core_get(6,name,sizeof(name))&&!strcmp(name,"test"));
    action(SDLK_ESCAPE);assert(!platform_core_menu&&settings_on);
    /* A Y-menu choice is saved separately, so changing a console cannot erase it. */
    Title rom={.kind="game",.url="romid:0123456789abcdef",.title="PSP example",.id=1};
    rom_core_open(&rom);rom_core_selected=selected;assert(rom_core_save());rom_core_menu=0;
    assert(rom_platform_core_save(6,"other"));
    assert(rom_core_preference("0123456789abcdef",name,sizeof(name))&&!strcmp(name,"test"));
    assert(rom_platform_core_get(6,name,sizeof(name))&&!strcmp(name,"other"));
    rom_core_selected=0;assert(rom_core_save());
    assert(!rom_core_preference("0123456789abcdef",name,sizeof(name)));
    assert(rom_platform_core_get(6,name,sizeof(name))&&!strcmp(name,"other"));
    assert(rom_platform_for_path("/mnt/disk/roms/PSPISO/game.iso")==6);
    assert(rom_platform_for_path("/mnt/disk/roms/PSXISO/game.iso")==5);
    settings_on=0;rom_core_menu=0;total=count=1;visible[0]=selection=0;browse.mode=22;
    titles[0]=rom;action(SDLK_y);assert(rom_core_menu||rom_options_menu);
    action(SDLK_ESCAPE);assert(!rom_options_menu&&!rom_core_menu);
    play();assert(game_exit&&!running&&!rom_options_menu);
    f=fopen("game-request","r");int id=0;assert(f&&fscanf(f,"%d",&id)==1&&id==1);fclose(f);
    unlink("game-request");
    unlink("cores/test_libretro.so");rmdir("cores");
    unlink("settings.cfg");unlink("tv-gambits.cfg");unlink("platform-cores.tsv");
    unlink("rom-cores.tsv");unlink("game-cores.tsv");
    assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: ROM performance persistence, console core menu, independent Y overrides and automatic reset");
}
