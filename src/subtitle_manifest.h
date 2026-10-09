/* Copy provider-supplied text tracks; never invent a subtitle URL. */
static void subtitle_manifest_rows(FILE *f,json_object *root){
    const char *keys[]={"subtitles","tracks","captions"};
    for(int k=0;k<3;k++){
        json_object *list=field(root,keys[k]);if(!list||!json_object_is_type(list,json_type_array))continue;
        size_t count=json_object_array_length(list);if(count>64)count=64;
        for(size_t i=0;i<count;i++){
            json_object *track=json_object_array_get_idx(list,i);
            const char *kind=string(track,"kind");if(*kind&&strcmp(kind,"captions")&&strcmp(kind,"subtitles"))continue;
            const char *url=string(track,"file");if(!*url)url=string(track,"url");if(!*url)url=string(track,"src");
            const char *language=string(track,"lang");if(!*language)language=string(track,"language");if(!*language)language=string(track,"label");
            if((strncmp(url,"https://",8)&&strncmp(url,"http://",7))||strlen(url)>4095||strpbrk(url,"\r\n\t "))continue;
            if(strlen(language)>95||strpbrk(language,"\r\n\t"))continue;
            fprintf(f,"%s\t%s\n",*language?language:"auto",url);
        }
    }
}
static void subtitle_manifest(json_object *root,json_object *source,const char *media){
    FILE *f=fopen("playback-subtitles.tsv.next","w");if(!f)return;
    fprintf(f,"%s\n",media);subtitle_manifest_rows(f,root);
    if(source&&source!=root)subtitle_manifest_rows(f,source);
    int error=fclose(f);if(error||rename("playback-subtitles.tsv.next","playback-subtitles.tsv"))remove("playback-subtitles.tsv.next");
}
