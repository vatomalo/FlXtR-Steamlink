#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void file(const char *name){FILE *f=fopen(name,"w");assert(f);fputs("fixture",f);fclose(f);}
int main(void){
 char temp[]="/tmp/flxtr-rom-menu-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
 assert(!mkdir("cores",0700));file("cores/zeta_libretro.so");file("cores/alpha_libretro.so");file("cores/not-a-core.txt");
 FILE *catalog=fopen("catalog.tsv","w");assert(catalog);
 fputs("Example ROM\tSNES\t\tromid:0123456789abcdef\tgame\t1\t0\t0\nBad\t\t\tromid:0123456789abcdef\tmovie\t1\t0\t0\nBad key\t\t\tromid:invalid\tgame\t1\t0\t0\n",catalog);fclose(catalog);
 assert(read_catalog("catalog.tsv")==1&&count==1&&!strcmp(titles[0].url,"romid:0123456789abcdef"));
 total=count=1;visible[0]=selection=0;browse.mode=22;
 titles[0]=(Title){.title="Example ROM",.kind="game",.url="romid:0123456789abcdef",.id=1};
 action(SDLK_y);assert(rom_assets_on&&!rom_core_menu&&rom_core_count==3&&!game_exit);
 assert(!strcmp(rom_core_names[0],"AUTOMATIC")&&!strcmp(rom_core_names[1],"alpha"));
 action(SDLK_RETURN);assert(rom_core_menu);
 action(SDLK_DOWN);action(SDLK_RETURN);assert(!rom_core_menu&&rom_assets_on);
 FILE *f=fopen("game-cores.tsv","r");assert(f);char saved[256];assert(fgets(saved,sizeof(saved),f));fclose(f);assert(strstr(saved,"alpha"));
 char preferred[96];assert(rom_core_preference("0123456789abcdef",preferred,sizeof(preferred))&&!strcmp(preferred,"alpha"));
 action(SDLK_ESCAPE);assert(!rom_assets_on);
 settings_on=1;settings_tab=SETTINGS_ROMS;settings_row=0;
 action(SDLK_RIGHT);assert(game_view==GAME_STRETCH);
 action(SDLK_RIGHT);assert(game_view==GAME_PIXEL);
 action(SDLK_DOWN);game_output=0;action(SDLK_RIGHT);assert(game_output==240);
 action(SDLK_ESCAPE);assert(!settings_on);
 action(SDLK_y);assert(rom_core_selected==1&&game_view==GAME_PIXEL);
 action(SDLK_RETURN);assert(rom_core_menu);action(SDLK_UP);action(SDLK_RETURN);assert(rom_core_selected==0);
 assert(!rom_core_preference("0123456789abcdef",preferred,sizeof(preferred)));
 f=fopen("game-cores.tsv","r");assert(f);assert(!fgets(saved,sizeof(saved),f));fclose(f);
 action(SDLK_ESCAPE);action(SDLK_x);assert(game_exit&&!running);
 f=fopen("game-request","r");int id=-1;assert(f&&fscanf(f,"%d",&id)==1&&id==1);fclose(f);
 unlink("catalog.tsv");unlink("game-request");unlink("settings.cfg");unlink("game-cores.tsv");unlink("rom-cores.tsv");
 unlink("cores/zeta_libretro.so");unlink("cores/alpha_libretro.so");unlink("cores/not-a-core.txt");rmdir("cores");
 assert(!chdir("/tmp"));rmdir(temp);
 puts("PASS: Y ROM options, sorted cores, per-ROM selection/automatic reset, ROM settings scaling/output and X play handoff");
 return 0;
}
