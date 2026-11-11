#include <assert.h>
#include <stdio.h>
#include "../src/rom_core_category.h"
int main(void){
    assert(rom_category_from_core("ppsspp","/roms/psx/game.iso")==7);
    assert(rom_category_from_core("PPSSPP_libretro.so","/roms/mame/game.zip")==7);
    assert(rom_category_from_core("/cores/pcsx_rearmed_libretro.so","/roms/psp/game.iso")==6);
    assert(rom_category_from_core("snes9x2010","/roms/nes/rom.nes")==2);
    assert(rom_category_from_core("mgba","/roms/psp/game.cso")==5);
    assert(rom_category_from_core("gambatte","/roms/gb/rom.gbc")==4);
    assert(rom_category_from_core("gambatte","/roms/gb/rom.gb")==3);
    assert(rom_category_from_core("fbneo","/roms/mame/game.zip")==9);
    assert(rom_category_from_core("fbalpha2012_neogeo","/roms/mame/game.zip")==8);
    assert(rom_category_from_core("unknown_core","/roms/psp/game.iso")==0);
    puts("PASS: recognized core takes precedence over ROM folder and extension");
    return 0;
}
