/* Secret of Mana song names, shown when the game starts a song (its music
 * is not synthesised on the Steam Link, see som_ai_proxy.c SOM_SKIP_MUSIC).
 *
 * The playing song id is 7E:1E05 (cidwel/som-decomp audio.md 8: $C3:01A4
 * compares a request with it; live on Turbo, map 130 = 0x0E as the decomp's
 * room list says). 64 ids / 59 unique sequences (song table $C3:3D39).
 * Built-in names are only
 * the 13 the decomp matched by audio correlation (audio.md "Ids matched");
 * the rest read "Song 0xNN" until named in SOM_SONG_FILE (lines
 * "<hex id><TAB><name>"). Pure lookup, no I/O except som_song_load. */
#ifndef FLXTR_SOM_SONGS_H
#define FLXTR_SOM_SONGS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOM_SONG_COUNT 0x40u
#define SOM_SONG_NAME_MAX 28u

static const struct { unsigned char id; const char *name; } som_song_known[] = {
    {0x04, "Danger"}, {0x05, "Distant Thunder"}, {0x07, "I Closed My Eyes"},
    {0x0C, "Dark Lich"}, {0x1B, "Ceremony"}, {0x29, "Fond Memories"},
    {0x31, "Leave Time for Love"}, {0x33, "The Curse"}, {0x36, "Morning Is Here"},
    {0x37, "One of Them Is Hope"}, {0x38, "A Conclusion"}, {0x39, "Meridian Dance"},
    {0x3A, "End"},
};

typedef struct { char name[SOM_SONG_COUNT][SOM_SONG_NAME_MAX + 1]; } SomSongNames;

static inline void som_song_defaults(SomSongNames *s) {
    memset(s, 0, sizeof(*s));
    for (size_t i = 0; i < sizeof(som_song_known) / sizeof(som_song_known[0]); i++)
        snprintf(s->name[som_song_known[i].id], SOM_SONG_NAME_MAX + 1, "%s", som_song_known[i].name);
}

/* Name overrides from a file; unreadable lines are skipped. */
static inline void som_song_load(SomSongNames *s, const char *path) {
    som_song_defaults(s);
    FILE *in = path ? fopen(path, "r") : NULL;
    if (!in) return;
    char line[96];
    while (fgets(line, sizeof(line), in)) {
        char *tab = strchr(line, '\t'), *end = NULL;
        if (!tab) continue;
        *tab = 0;
        const unsigned long id = strtoul(line, &end, 16);
        if (end == line || *end || id >= SOM_SONG_COUNT) continue;
        char *name = tab + 1;
        name[strcspn(name, "\r\n")] = 0;
        char clean[SOM_SONG_NAME_MAX + 1];
        size_t n = 0;
        for (size_t i = 0; name[i] && n < SOM_SONG_NAME_MAX; i++)
            if (name[i] >= 32 && name[i] <= 126 && name[i] != '|' && name[i] != '^') clean[n++] = name[i];
        clean[n] = 0;
        if (n) memcpy(s->name[id], clean, n + 1);
    }
    fclose(in);
}

/* "Danger", or "Song 0x0E" for an unnamed id. */
static inline const char *som_song_name(const SomSongNames *s, unsigned id, char fallback[16]) {
    if (id < SOM_SONG_COUNT && s->name[id][0]) return s->name[id];
    snprintf(fallback, 16, "Song 0x%02X", id & 0xFFu);
    return fallback;
}
#endif
