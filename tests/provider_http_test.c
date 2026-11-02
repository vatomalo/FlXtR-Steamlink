#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <libavutil/dict.h>
#include "../src/provider_http.h"
int main(void){
    AVDictionary *opts=NULL;setenv("FLXTR_MEDIA_PROVIDER","sflix",1);provider_http_options(&opts);
    assert(!strcmp(av_dict_get(opts,"referer",NULL,0)->value,"https://vidspark.to/"));
    assert(!strcmp(av_dict_get(opts,"allowed_extensions",NULL,0)->value,"ALL"));av_dict_free(&opts);
    setenv("FLXTR_MEDIA_PROVIDER","fmovies",1);provider_http_options(&opts);assert(!strcmp(av_dict_get(opts,"referer",NULL,0)->value,"https://videm.xyz/"));av_dict_free(&opts);
    unsetenv("FLXTR_MEDIA_PROVIDER");provider_http_options(&opts);assert(!opts);
    puts("PASS: fixed provider origins and playlist extensions; ordinary playback options unchanged");return 0;
}
