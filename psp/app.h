#ifndef FLXTR_PSP_APP_H
#define FLXTR_PSP_APP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/select.h>
#include <curl/curl.h>
#include "tilefinch/media_mp4.h"
extern atomic_bool quitting;
extern char status_text[256];
void status(const char *fmt,...);
int connect_wifi(int profile);
bool network_cancelled(void);
int transfer(const char *url, const char *range, unsigned char *data, size_t cap,
             size_t *used, uint64_t *total);
typedef struct {
    char url[2048];
    unsigned char *buffer;
    uint64_t start,length;
    size_t used;
} Stream;
bool stream_open(Stream *s,const char *url);
bool stream_read(void *opaque,uint64_t offset,void *dest,size_t length);
void stream_close(Stream *s);
void play_url(const char *url);
int check_update(void);
#endif
