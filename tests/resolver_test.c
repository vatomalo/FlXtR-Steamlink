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
    count=1;
    char master[]="#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\"audio.m3u8\"\n"
        "#EXT-X-STREAM-INF:RESOLUTION=1280x720,CODECS=\"avc1.64001f\",AUDIO=\"audio\"\n720.m3u8\n"
        "#EXT-X-STREAM-INF:RESOLUTION=1920x1080,CODECS=\"avc1.640028\"\n1080.m3u8\n"
        "#EXT-X-STREAM-INF:RESOLUTION=3840x2160,CODECS=\"hvc1\"\n4k.m3u8\n"
        "#EXT-X-STREAM-INF:RESOLUTION=1280x720,CODECS=\"avc1\"\nduplicate.m3u8\n";
    add_variants(master,"https://example.org/master.m3u8","alpha");
    assert(count==3&&choices[1].height==720&&choices[2].height==1080);
    assert(!strcmp(choices[1].url,"https://example.org/master.m3u8")); /* Keep alternate audio group. */
    puts("PASS: server deduplication, URL validation and compatible HLS quality choices");return 0;
}
