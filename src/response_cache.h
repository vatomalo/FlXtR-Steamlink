/* Bounded cache of public catalogue responses, shared by short-lived workers.
 * Never use for signed playback URLs, credentials or download responses. */
#define RESPONSE_CACHE_SLOTS 16
#define RESPONSE_CACHE_LIMIT (512*1024)
#define RESPONSE_CACHE_TTL (6*60*60)
static void response_cache_path(const char *url,char *path,size_t cap){
    unsigned long hash=2166136261UL;
    for(const unsigned char *p=(const unsigned char*)url;*p;p++)hash=((hash^*p)*16777619UL)&0xffffffffUL;
    snprintf(path,cap,"catalog-cache/response-%lu.json",hash%RESPONSE_CACHE_SLOTS);
}
static char *response_cache_load(const char *url){
    char path[80];response_cache_path(url,path,sizeof(path));struct stat st;
    time_t now=time(NULL);
    if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<2||st.st_size>RESPONSE_CACHE_LIMIT*2||st.st_mtime>now||now-st.st_mtime>RESPONSE_CACHE_TTL)return NULL;
    json_object *o=json_object_from_file(path),*key=NULL,*body=NULL;
    if(!o)return NULL;
    char *copy=NULL;
    if(json_object_object_get_ex(o,"url",&key)&&json_object_is_type(key,json_type_string)&&
       !strcmp(json_object_get_string(key),url)&&json_object_object_get_ex(o,"body",&body)&&json_object_is_type(body,json_type_string)){
        int n=json_object_get_string_len(body);const char *s=json_object_get_string(body);
        if(n>0&&n<=RESPONSE_CACHE_LIMIT&&strlen(s)==(size_t)n)copy=strdup(s);
    }
    json_object_put(o);return copy;
}
static void response_cache_store(const char *url,const char *body){
    if(!body||!*body||strlen(body)>RESPONSE_CACHE_LIMIT)return;
    mkdir("catalog-cache",0700);
    char path[80],temp[96],lock[96];response_cache_path(url,path,sizeof(path));
    snprintf(temp,sizeof(temp),"%s.tmp",path);snprintf(lock,sizeof(lock),"%s.lock",path);
    int guard=open(lock,O_WRONLY|O_CREAT,0600);if(guard<0)return;
    if(flock(guard,LOCK_EX|LOCK_NB)){close(guard);return;}
    int fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0){close(guard);return;}
    json_object *o=json_object_new_object();
    if(o){json_object_object_add(o,"url",json_object_new_string(url));json_object_object_add(o,"body",json_object_new_string(body));
        const char *data=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);size_t length=strlen(data),done=0;
        while(length<=RESPONSE_CACHE_LIMIT*2&&done<length){ssize_t n=write(fd,data+done,length-done);if(n<0&&errno==EINTR)continue;if(n<=0)break;done+=(size_t)n;}
        int ok=close(fd)==0;fd=-1;
        if(ok&&done==length)rename(temp,path);
        json_object_put(o);
    }
    if(fd>=0)close(fd);
    unlink(temp);close(guard);
}
