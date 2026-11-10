#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
int main(void){
    json_object *results=json_tokener_parse("{\"items\":["
        "{\"type\":\"app\",\"id\":1,\"name\":\"FINAL FANTASY VII REMAKE Original Soundtrack\"},"
        "{\"type\":\"app\",\"id\":2,\"name\":\"FINAL FANTASY VII REBIRTH\"},"
        "{\"type\":\"app\",\"id\":1462040,\"name\":\"FINAL FANTASY VII REMAKE INTERGRADE\"},"
        "{\"type\":\"bundle\",\"id\":3,\"name\":\"Final Fantasy VII Remake\"}]}");
    assert(pc_game_art_id(results,"Final Fantasy VII Remake")==1462040);
    assert(pc_game_art_id(results,"Unrelated game")==0);
    assert(pc_game_art_id(results,"Final")==0);
    assert(pc_game_art_id(results,"Final Fantasy VII REBIRTH")==2);
    json_object_put(results);
    puts("PASS: PC cover matching chooses game editions, rejects unrelated titles, bundles and soundtracks");
    return 0;
}
