#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
int main(void){
    assert(pc_game_release("13 Jun, 2020")==20200613);
    assert(pc_game_release("Jun 13, 2020")==20200613);
    assert(pc_game_release("Coming soon")==0);
    assert(pc_title_score("Golden-Persona-4","Persona 4 Golden")<10000);
    assert(pc_title_score("Fading.Afternoon.v20231104-P2P","Fading Afternoon")<10000);
    assert(pc_title_score("BlackMythWukong","Black Myth: Wukong")<10000);
    assert(pc_title_score("FFXV","FINAL FANTASY XV")<10000);
    assert(pc_title_score("Final Fantasy 7 Remake","FINAL FANTASY VII REMAKE INTERGRADE")<10000);
    assert(pc_title_score("NieRAutomata","NieR:Automata")<10000);
    assert(pc_title_score("What's That","What\xe2\x80\x99s That")<10000);
    assert(pc_title_score("Persona 4 Golden","Persona 5 Royal")==10000);
    assert(pc_title_score("Final Fantasy VII Remake","Final Fantasy VII Rebirth")==10000);
    char queries[8][256];int count=pc_title_queries("Golden-Persona-4",queries);assert(count>1&&count<=8);
    int reordered=0;for(int i=0;i<count;i++){if(!strcmp(queries[i],"persona 4 golden"))reordered=1;for(int j=0;j<i;j++)assert(strcmp(queries[i],queries[j]));}
    assert(reordered);
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
