#ifndef FLXTR_PROVIDER_ORDER_H
#define FLXTR_PROVIDER_ORDER_H
/* Preferences may reorder Flixer mirrors, never the provider fallback order. */
static inline int provider_rank(const char *server){
    return !strcmp(server,"fmovies")?1:!strcmp(server,"sflix")?2:0;
}
static inline const char *source_provider(const char *meta){
    return !strncmp(meta,"SFLIX / ",8)?"sflix":!strncmp(meta,"FMOVIES / ",10)?"fmovies":"";
}
#endif
