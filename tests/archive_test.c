#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
int main(void){
    assert(archive_identifier("BigBuckBunny_124"));assert(!archive_identifier("../bad"));assert(!archive_identifier("a/b"));
    json_object *root=json_tokener_parse("{\"response\":{\"numFound\":2,\"docs\":[{\"identifier\":\"example\",\"title\":\"Video\"},{\"identifier\":\"../bad\"}]}}");
    assert(!archive_cards(root)&&used==1&&total==2&&!strcmp(entries[0].kind,"archive"));json_object_put(root);
    used=total=0;memset(entries,0,sizeof(entries));
    root=json_tokener_parse("{\"files\":[{\"name\":\"folder/a video.mp4\",\"format\":\"h.264\"},{\"name\":\"secret.mp4\",\"format\":\"h.264\",\"private\":\"true\"},{\"name\":\"audio.mp3\",\"format\":\"MP3\"},{\"name\":\"picture.jpg\",\"format\":\"JPEG\"},{\"name\":\"other.mp4\",\"format\":\"MPEG4\",\"private\":false}]}");
    assert(!archive_files_json(root,"example",1)&&used==2&&total==2);
    assert(!strcmp(entries[0].url,"https://archive.org/download/example/folder%2Fa%20video.mp4"));
    assert(!strcmp(entries[1].kind,"archive-file"));json_object_put(root);
    char temp[]="/tmp/flxtr-archive-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));assert(!mkdir("catalog-cache",0700));
    cache_identity("archive-files",1,"example",0,0);for(int i=0;i<used;i++)entries[i].poster[0]=0;cache_save();
    used=total=0;memset(entries,0,sizeof(entries));assert(cache_load()&&used==2&&strstr(entries[0].url,"a%20video.mp4"));
    unlink(cache_file);rmdir("catalog-cache");assert(!chdir("/tmp"));rmdir(temp);
    puts("PASS: Archive identifiers, video-only filtering, private files, URL encoding and cached direct URLs");return 0;
}
