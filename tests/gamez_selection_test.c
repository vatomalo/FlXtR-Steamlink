#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char directory[]="/tmp/flxtr-gamez-selection-XXXXXX";
    assert(mkdtemp(directory));assert(!chdir(directory));
    Title game={.kind="moonlight-game",.title="Approved PC game",.url="gamez:5E4132C860DED717ED0FEE86"};
    total=1;visible[0]=0;selection=0;titles[0]=game;
    running=1;moonlight_exit=0;play();
    assert(!running&&moonlight_exit);
    FILE *file=fopen("gamez-request","r");char id[64]="";
    assert(file&&fgets(id,sizeof(id),file));assert(!fclose(file));
    assert(!strcmp(id,"5E4132C860DED717ED0FEE86\n"));assert(!unlink("gamez-request"));
    const char *bad[]={"","gamez:","gamez:bad;command","gamez:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        snprintf(game.url,sizeof(game.url),"%s",bad[i]);
        titles[0]=game;running=1;moonlight_exit=0;play();
        assert(running&&!moonlight_exit&&access("gamez-request",F_OK));
    }
    assert(!chdir("/tmp"));assert(!rmdir(directory));
    puts("PASS: full companion game ID survives UI selection; old and invalid links cannot launch");
    return 0;
}
