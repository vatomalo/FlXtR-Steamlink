#ifndef FLXTR_PLAYBACK_STARTED_H
#define FLXTR_PLAYBACK_STARTED_H
/* Parent consumes a confirmed first-frame marker. Gated preloaders never write
 * it before release. A key prevents a late marker belonging to another title. */
static void playback_mark_started(double position){
    const char *key=getenv("FLXTR_BOOKMARK_KEY");
    if(!key||strlen(key)!=8||strspn(key,"0123456789abcdef")!=8||
       !(position>=0&&position<=86400))return;
    FILE *f=fopen("playback-started.next","w");if(!f)return;
    fprintf(f,"%s %.3f\n",key,position);
    if(!fclose(f))rename("playback-started.next","playback-started");
}
#endif
