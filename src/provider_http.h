#ifndef FLXTR_PROVIDER_HTTP_H
#define FLXTR_PROVIDER_HTTP_H
/* Use fixed provider origins, never arbitrary headers from catalogue rows. */
static void provider_http_options(AVDictionary **options){
    const char *provider=getenv("FLXTR_MEDIA_PROVIDER");
    if(provider&&!strcmp(provider,"sflix")){
        av_dict_set(options,"referer","https://vidspark.to/",0);
        av_dict_set(options,"user_agent","Mozilla/5.0",0);
        /* This CDN uses .js/.json playlists and extensionless segment URLs.
         * The player's remote protocol whitelist still excludes local files. */
        av_dict_set(options,"allowed_extensions","ALL",0);
        av_dict_set(options,"allowed_segment_extensions","ALL",0);
        av_dict_set(options,"extension_picky","0",0);
    }else if(provider&&!strcmp(provider,"fmovies")){
        av_dict_set(options,"referer","https://videm.buzz/",0);
        /* VIDEM rejects the resolved HLS URL with HTTP 403 when discovery's
         * browser identity is dropped before native playback. */
        av_dict_set(options,"user_agent","Mozilla/5.0",0);
        av_dict_set(options,"allowed_extensions","ALL",0);
        av_dict_set(options,"allowed_segment_extensions","ALL",0);
        av_dict_set(options,"extension_picky","0",0);
    }
}
#endif
