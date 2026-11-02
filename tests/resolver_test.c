#define main resolver_main
#include "../src/resolver.c"
#undef main
#include <assert.h>
/* Network/decoder are deliberately not used in this deterministic test. */
int resolver_init(void){return -1;}
char *resolver_key(void){return NULL;}
char *resolver_decode(const char *p,const char *k){(void)p;(void)k;return NULL;}
void resolver_free(void){}
const char *resolver_error(void){return "test";}
int main(void){
    assert(valid_server("alpha")&&!valid_server("bad\r\nheader"));
    assert(valid_url("https://example.org/master.m3u8")&&!valid_url("file:///etc/passwd"));
    json_object *o=parse("{\"sources\":[{\"server\":\"alpha\"},{\"server\":\"bravo\"},{\"server\":\"alpha\"},{\"server\":\"bad\\nname\"}]}");
    servers(o);assert(count==2&&!strcmp(choices[1].label,"bravo"));json_object_put(o);
    char english[1024]="",norwegian[1024]="",spanish[1024]="";
    o=parse("[{\"label\":\"English Hi3\",\"file\":\"https://cache.example/Movie Name.vtt\"},{\"label\":\"Norwegian\",\"file\":\"https://cache.example/no.vtt\"},{\"label\":\"Spanish 2\",\"file\":\"https://cache.example/es.vtt\"}]");
    select_subtitles(o,english,norwegian,spanish);json_object_put(o);
    assert(!strcmp(english,"https://cache.example/Movie%20Name.vtt")&&strstr(norwegian,"/no.vtt")&&strstr(spanish,"/es.vtt"));
    char encoded[1024];assert(!caption_url("file:///etc/passwd",encoded)&&!caption_url("https://x/\nsecret",encoded));
    count=1;
    char master[]="#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\"audio.m3u8\"\n"
        "#EXT-X-STREAM-INF:RESOLUTION=1280x720,CODECS=\"avc1.64001f\",AUDIO=\"audio\"\n720.m3u8\n"
        "#EXT-X-STREAM-INF:RESOLUTION=1920x1080,CODECS=\"avc1.640028\"\n1080.m3u8\n"
        "#EXT-X-STREAM-INF:RESOLUTION=3840x2160,CODECS=\"hvc1\"\n4k.m3u8\n"
        "#EXT-X-STREAM-INF:RESOLUTION=1280x720,CODECS=\"avc1\"\nduplicate.m3u8\n";
    add_variants(master,"https://example.org/master.m3u8","alpha");
    assert(count==3&&choices[1].height==720&&choices[2].height==1080);
    assert(!strcmp(choices[1].url,"https://example.org/master.m3u8")); /* Keep alternate audio group. */
    count=0;server_choice("alpha");server_choice("echo");web_servers();
    assert(count==4&&!strcmp(choices[2].label,"fmovies")&&!strcmp(choices[3].label,"sflix"));
    count=0;web_servers();assert(count==2&&!strcmp(choices[0].label,"fmovies")); /* Flixer unavailable. */
    char key[80];assert(web_extract("var fz=`123abc`;","var fz=`",'`',key,sizeof(key))&&!strcmp(key,"123abc"));
    assert(!web_extract("var fz=`bad\nheader`;","var fz=`",'`',key,sizeof(key)));
    o=web_assignment("var Q = {\"ssr\":{\"servers\":[]},\"t\":\"public-reference\"}; next();","var Q = ");
    assert(o&&field(o,"ssr"));json_object_put(o);
    o=parse("{\"url\":\"https://media.example/master.m3u8\",\"tracks\":[{\"label\":\"English\",\"file\":\"https://media.example/en.vtt\"}]}");
    assert(web_source(o,"SFLIX")&&count==1&&strstr(choices[0].meta,"SFLIX")&&choices[0].subs_eng[0]);json_object_put(o);
    o=parse("{\"url\":\"file:///etc/passwd\"}");assert(!web_source(o,"FMOVIES"));json_object_put(o);
    puts("PASS: server deduplication, URL validation and compatible HLS quality choices");return 0;
}
