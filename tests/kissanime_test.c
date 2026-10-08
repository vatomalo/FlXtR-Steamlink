#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
int main(void){
    char value[80];
    assert(kiss_attr("<a title='A &amp; B &#039;test&#039;' rel=\"42\">","title",value,sizeof(value)));
    assert(!strcmp(value,"A & B 'test'"));
    assert(!kiss_attr("<a notitle=\"bad\">","title",value,sizeof(value)));
    assert(!kiss_attr("<a title=\"unterminated>","title",value,sizeof(value)));
    char html[]="<div class=\"listupd\"><article><a href=\"https://kissanime.com.cv/anime/demo/\" rel=\"42\" title=\"Demo\"><img src=\"https://kissanime.com.cv/wp-content/uploads/a.jpg\"></a></article>"
        "<article><a href=\"https://elsewhere.invalid/ad\" rel=\"9\" title=\"Ad\"></a></article><div class=\"hpage\">Next</div>";
    assert(kiss_cards(html,0)==1&&used==1&&entries[0].id==42&&!strcmp(entries[0].kind,"kiss"));
    used=0;assert(kiss_cards(html,1)==1&&!used);
    char url[2048];
    assert(kiss_direct("<source src='https://media.example/video.m3u8?x=1&amp;y=2'>",url,sizeof(url)));
    assert(!strcmp(url,"https://media.example/video.m3u8?x=1&y=2"));
    assert(kiss_direct("{\"file\":\"https:\\/\\/media.example\\/video.mp4\"}",url,sizeof(url)));
    assert(!strcmp(url,"https://media.example/video.mp4"));
    assert(!kiss_direct("<source src='file:///etc/passwd'>",url,sizeof(url)));
    assert(!kiss_direct("<h1>File unavailable</h1><script src='https://ads.example/ad.js'></script>",url,sizeof(url)));
    KissEpisode episodes[]={{12,2},{11,1}};qsort(episodes,2,sizeof(*episodes),kiss_episode_compare);assert(episodes[0].number==1);
    puts("PASS: bounded KissAnime attributes, catalog filtering, episode ordering, direct media parsing and unavailable hosts");
    return 0;
}
