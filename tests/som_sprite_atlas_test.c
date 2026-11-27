#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <unistd.h>
#include "../src/som_sprite_atlas.h"

int main(void){
    char path[]="/tmp/som-atlas-test-XXXXXX";
    const int fd=mkstemp(path);
    assert(fd>=0);
    FILE *file=fdopen(fd,"wb");
    assert(file);
    const uint8_t header[]={'S','A','T','1',1,0,2,0};
    uint8_t a[SOM_ATLAS_RECORD]={0xe0,1,0xc8,1,3};
    uint8_t b[SOM_ATLAS_RECORD]={0xe8,1,0xca,1,7};
    assert(fwrite(header,1,sizeof(header),file)==sizeof(header));
    assert(fwrite(a,1,sizeof(a),file)==sizeof(a));
    assert(fwrite(b,1,sizeof(b),file)==sizeof(b));
    assert(fclose(file)==0);
    SomSpriteAtlas atlas={0};
    assert(!som_atlas_load(&atlas,path,0)&&!atlas.records);
    assert(som_atlas_load(&atlas,path,1));
    som_atlas_select(&atlas,0x1e0);
    assert(atlas.tiles[0x1c8]&&atlas.tiles[0x1c8][0]==3&&!atlas.tiles[0x1ca]);
    const uint8_t *first=atlas.tiles[0x1c8];
    som_atlas_select(&atlas,0x1e0);
    assert(atlas.tiles[0x1c8]==first);
    som_atlas_select(&atlas,0x1e8);
    assert(!atlas.tiles[0x1c8]&&atlas.tiles[0x1ca]&&atlas.tiles[0x1ca][0]==7);
    som_atlas_select(&atlas,0x1ff);
    assert(!atlas.tiles[0x1c8]&&!atlas.tiles[0x1ca]);
    assert(truncate(path,10)==0);
    assert(!som_atlas_load(&atlas,path,1)&&atlas.count==2);
    som_atlas_clear(&atlas);
    assert(!atlas.records&&!atlas.count);
    assert(unlink(path)==0);
    return 0;
}
