#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
 char dir[]="/tmp/flxtr-rom-routes-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
 FILE *f=fopen("catalog.tsv","w");assert(f);fputs("DOWNLOADS\tROM ARCHIVES\t\t\trom-downloads\t11\t0\t0\n",f);fclose(f);
 assert(read_catalog("catalog.tsv")==1&&!strcmp(titles[0].kind,"rom-downloads"));
 f=fopen("greenlink-catalog","w");assert(f);fputs("#!/bin/sh\nexit 0\n",f);fclose(f);assert(!chmod("greenlink-catalog",0700));
 browse.mode=21;action(SDLK_y);assert(browse.mode==21&&!search_on);
 action(SDLK_RETURN);assert(pending.mode==28&&!player_pid&&!game_exit);
 if(catalog_pid>0){int code;waitpid(catalog_pid,&code,0);catalog_pid=0;}
 pending.mode=0;snprintf(titles[0].kind,sizeof(titles[0].kind),"downloads-folde");action(SDLK_RETURN);
 assert(!player_pid&&!auto_active&&strstr(status,"UNKNOWN ROM ITEM"));
 total=count=1;selection=visible[0]=0;browse.mode=22;
 titles[0]=(Title){.title="Example ROM",.kind="game",.url="romid:0123456789abcdef",.id=1};
 action(SDLK_y);assert(search_on&&rom_rename_on&&!rom_assets_on&&!strcmp(search_text,"Example ROM"));
 action(SDLK_ESCAPE);assert(!rom_rename_on&&!search_on);
 unlink("greenlink-catalog");unlink("catalog.tsv");unlink("catalog.log");unlink("catalog-cache/result.tsv");rmdir("catalog-cache");
 assert(!chdir("/tmp"));rmdir(dir);
 puts("PASS: ROM Downloads opens archive browser, folders stay selected, Triangle opens rename");
 return 0;
}