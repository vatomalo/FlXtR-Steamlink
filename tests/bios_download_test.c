#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(int argc,char **argv){
 if(argc==5||argc==6){Title item={.id=argc==6?atoi(argv[5]):1};snprintf(item.url,sizeof(item.url),"%s",argv[1]);snprintf(item.meta,sizeof(item.meta),"SHA1:%s",argv[2]);snprintf(item.kind,sizeof(item.kind),"%s",argv[3]);assert(!chdir(argv[4]));return bios_archive_download(&item);}
 char dir[]="/tmp/flxtr-bios-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
 assert(bios_archive_name_valid("NEO GEO + BIOS.zip","neogeo.zip",1));
 assert(bios_archive_name_valid("neogeo.zip","neogeo.zip",0));
 assert(!bios_archive_name_valid("anything.exe","neogeo.zip",1));
 assert(bios_member_size(1,1200000)&&!bios_member_size(1,16777217));
 assert(bios_member_size(2,524288)&&!bios_member_size(2,524287));
 assert(bios_member_size(5,16384)&&!bios_member_size(5,16383));
 assert(bios_archive_name_valid("SCPH5501.BIN","scph5501.bin",0));
 assert(bios_archive_name_valid("GBA BIOS.zip","gba_bios.bin",1));
 FILE *payload=fopen("psx","wb");assert(payload);assert(!ftruncate(fileno(payload),524288));fclose(payload);
 assert(bios_payload_valid("psx",2)&&bios_payload_valid("psx",3)&&bios_payload_valid("psx",4)&&!bios_payload_valid("psx",5));
 payload=fopen("gba","wb");assert(payload);assert(!ftruncate(fileno(payload),16384));fclose(payload);
 assert(bios_payload_valid("gba",5)&&!bios_payload_valid("gba",2));unlink("psx");unlink("gba");
 FILE *f=fopen("download","wb");assert(f);fputs("PK\003\004",f);for(int i=0;i<30;i++)fputc(0,f);fclose(f);
 assert(bios_payload_valid("download",1)&&!bios_payload_valid("download",2));
 assert(!bios_install_verified("download","neogeo.zip"));
 f=fopen("download","w");assert(f);fputs("broken",f);fclose(f);
 assert(!bios_payload_valid("download",1));
 assert(bios_install_verified("download","neogeo.zip")==4);
 assert(bios_payload_valid("neogeo.zip",1)&&access("download",F_OK));
 unlink("neogeo.zip");assert(!symlink("missing","neogeo.zip"));
 f=fopen("download","w");assert(f);fputs("fixture",f);fclose(f);
 assert(bios_install_verified("download","neogeo.zip")==4);assert(!unlink("neogeo.zip"));
 assert(!chdir("/tmp"));assert(!rmdir(dir));
 puts("PASS: Neo Geo bundle accepted, BIOS sizes validated, existing BIOS preserved");return 0;
}
