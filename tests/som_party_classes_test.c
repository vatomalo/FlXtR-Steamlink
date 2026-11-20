#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "../src/som_ai_proxy.c"

int main(void){
    static uint8_t ram[0x20000];
    static uint8_t rom[0x110000];
    for(unsigned i=0;i<SOM_PARTY_COUNT;i++){
        som_disc_init(som_party_disc(i));
        uint8_t *actor=ram+0xE000u+0x200u*i;
        actor[0]=1;
        actor[0x180]=(uint8_t)(0x80u+i);
        actor[0x182]=(uint8_t)(80u-30u*i);
        actor[0x184]=100;
    }
    disciplines.selected=10;
    som_party_disc(1)->selected=4;
    som_party_disc(2)->selected=3;
    som_party_disc(1)->selected=6;
    assert(disciplines.selected==10&&som_party_disc(2)->selected==3);
    assert(som_party_heal_target(ram)==2);
    ram[0xE591]=0x80;
    assert(som_party_heal_target(ram)==1);
    assert(som_party_can_cast(som_party_actor(ram,1),1));
    ram[0xE22C]=2;
    assert(!som_party_can_cast(som_party_actor(ram,1),1));
    ram[0xE22C]=0;
    ram[0xE390]=0x20;
    assert(!som_party_can_cast(som_party_actor(ram,1),1));
    /* Exercise the proxy's actual cast request with synthetic ROM rows.
     * The backend is responsible for resolving it and debiting native MP. */
    ram[0xE4]=1;
    ram[0xE6]=0x0F;
    ram[0xE02C]=1;
    ram[0xE386]=10;
    ram[0xE390]=0;
    som_party_disc(1)->selected=4;
    uint8_t *cure=rom+0x102AD0u+64u*0x0Bu;
    cure[8]=1;
    cure[15]=4;
    som_rom=rom;
    som_rom_size=sizeof(rom);
    som_party_cast_frame(ram);
    assert(ram[0xE370]==0x0B&&ram[0xE378]==2);
    assert(ram[0xE060]==0&&follower_ready[0]==180&&follower_ready[1]==0);
    ram[0xE260]=ram[0xE21C]=0;
    ram[0xE370]=0;
    som_party_cast_frame(ram);
    assert(ram[0xE370]==0); /* cooldown */
    frame_number=180;
    ram[0xDA]=2;
    som_party_cast_frame(ram);
    assert(ram[0xE370]==0); /* human binding */
    ram[0xDA]=0;
    ram[0xE386]=3;
    som_party_cast_frame(ram);
    assert(ram[0xE370]==0); /* MP */
    ram[0xE386]=10;
    frame_number=210;
    ram[0xD0]=1;
    som_party_cast_frame(ram);
    assert(ram[0xE370]==0); /* event/map transition */
    ram[0xD0]=0;
    ram[0xE591]=0;
    ram[0xE386]=0;
    ram[0xE586]=8;
    ram[0xE600]=1;
    ram[0xE620]=32;
    ram[0xE622]=32;
    uint8_t *fire=rom+0x102AD0u+64u*0x0Cu;
    fire[8]=0x81;
    fire[15]=3;
    frame_number=240;
    som_party_cast_frame(ram);
    assert(ram[0xE570]==0x0C&&ram[0xE578]==4);
    assert(follower_ready[0]==270&&follower_ready[1]==420);

    char root[]="/tmp/flxtr-party-test-XXXXXX";
    assert(mkdtemp(root));
    char path[128],sidecar[128];
    assert(snprintf(path,sizeof(path),"%s/classes",root)>0);
    assert(snprintf(sidecar,sizeof(sidecar),"%s/classes.party-1",root)>0);
    disc_file=path;
    disciplines.level[10]=7;
    disciplines.experience[10]=42;
    som_disc_save();
    som_follower_disc_file(1,1);
    som_disc_init(&disciplines);
    som_disc_init(som_party_disc(1));
    som_disc_load();
    som_follower_disc_file(1,0);
    assert(disciplines.selected==10&&disciplines.level[10]==7&&disciplines.experience[10]==42);
    assert(som_party_disc(1)->selected==4&&som_party_disc(2)->selected==3);
    assert(unlink(sidecar)==0&&unlink(path)==0&&rmdir(root)==0);
    return 0;
}
