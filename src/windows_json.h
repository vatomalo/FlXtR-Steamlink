#ifndef FLXTR_WINDOWS_JSON_H
#define FLXTR_WINDOWS_JSON_H
#ifdef _WIN32
/* json-c's filename API uses the narrow CRT. Use our UTF-8 file adapter. */
static inline json_object *flxtr_json_from_file(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long length=ftell(f);if(length<0||length>8*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *data=(char *)malloc((size_t)length+1);if(!data){fclose(f);return NULL;}
    size_t got=fread(data,1,(size_t)length,f);int good=got==(size_t)length&&!ferror(f);fclose(f);
    data[got]=0;json_object *object=good?json_tokener_parse(data):NULL;free(data);return object;
}
static inline int flxtr_json_to_file(const char *path,json_object *object){
    const char *data=json_object_to_json_string_ext(object,JSON_C_TO_STRING_PLAIN);if(!data)return -1;
    FILE *f=fopen(path,"wb");if(!f)return -1;
    size_t size=strlen(data);int good=fwrite(data,1,size,f)==size;if(fclose(f))good=0;return good?0:-1;
}
#define json_object_from_file flxtr_json_from_file
#define json_object_to_file flxtr_json_to_file
#endif
#endif
