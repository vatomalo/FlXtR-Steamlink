/* Song names: built-in decomp names, file overrides, fallback ids. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "../src/som_songs.h"

int main(void) {
    SomSongNames s;
    char fb[16];
    som_song_load(&s, "/nonexistent/flxtr-songs");
    assert(!strcmp(som_song_name(&s, 0x04, fb), "Danger"));
    assert(!strcmp(som_song_name(&s, 0x0E, fb), "Song 0x0E"));
    char path[] = "/tmp/flxtr-songs-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    const char text[] = "0e\tPotos Forest\n3B\ttoo big\nzz\tbad\n04\tDan|ger^\n11\t\n";
    assert(write(fd, text, sizeof(text) - 1) == (ssize_t)(sizeof(text) - 1));
    close(fd);
    som_song_load(&s, path);
    unlink(path);
    assert(!strcmp(som_song_name(&s, 0x0E, fb), "Potos Forest"));
    assert(!strcmp(som_song_name(&s, 0x04, fb), "Danger"));   /* overlay markers dropped */
    assert(!strcmp(som_song_name(&s, 0x11, fb), "Song 0x11")); /* empty name ignored */
    assert(!strcmp(som_song_name(&s, 0x3A, fb), "End"));
    puts("PASS: song names");
    return 0;
}
