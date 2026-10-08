/* Public video metadata and direct files; no browser or account required. */
#define ARCHIVE "https://archive.org"
static int archive_identifier(const char *s){
    size_t n=strlen(s);if(!n||n>128)return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)s[i])&&s[i]!='_'&&s[i]!='-'&&s[i]!='.')return 0;
    return !strstr(s,"..");
}
static json_object *archive_json(const char *url){
    Buffer b={NULL,0,2*1024*1024};if(fetch(url,&b)){free(b.data);return NULL;}
    json_tokener *tok=json_tokener_new_ex(32);if(!tok){free(b.data);return NULL;}
    json_object *root=json_tokener_parse_ex(tok,b.data,(int)b.length);
    if(json_tokener_get_error(tok)!=json_tokener_success){json_object_put(root);root=NULL;}
    json_tokener_free(tok);free(b.data);return root;
}
static int archive_cards(json_object *root){
    json_object *response=field(root,"response"),*docs=field(response,"docs");
    if(!docs||!json_object_is_type(docs,json_type_array))return -1;
    total=number(response,"numFound");if(total<0)total=0;if(total>12000)total=12000;
    for(size_t i=0;i<(size_t)json_object_array_length(docs)&&used<PAGE_SIZE;i++){
        json_object *o=json_object_array_get_idx(docs,i);const char *id=string(o,"identifier");
        if(!archive_identifier(id))continue;
        Entry *e=&entries[used++];clean(e->title,sizeof(e->title),string(o,"title"));if(!e->title[0])clean(e->title,sizeof(e->title),id);
        strcpy(e->kind,"archive");strcpy(e->meta,"INTERNET ARCHIVE / VIDEO FILES");
        snprintf(e->url,sizeof(e->url),ARCHIVE "/details/%s",id);
        snprintf(e->poster,sizeof(e->poster),ARCHIVE "/services/img/%s",id);
    }return 0;
}
static int archive_list(int page,const char *query){
    char term[192];size_t n=0;
    for(size_t i=0;query[i]&&n+1<sizeof(term);i++)if(isalnum((unsigned char)query[i])||query[i]==' ')term[n++]=query[i];
    term[n]=0;
    char q[512];snprintf(q,sizeof(q),"mediatype:movies AND (format:\"h.264\" OR format:\"MPEG4\") AND -access-restricted-item:true%s%s",n?" AND ":"",term);
    char *escaped=curl_easy_escape(NULL,q,0);if(!escaped)return -1;
    char url[2048];snprintf(url,sizeof(url),ARCHIVE "/advancedsearch.php?q=%s&fl[]=identifier&fl[]=title&rows=6&page=%d&sort[]=downloads+desc&output=json",escaped,page);curl_free(escaped);
    json_object *root=archive_json(url);if(!root)return -1;
    int rc=archive_cards(root);json_object_put(root);return rc;
}
static int archive_video(json_object *f){
    const char *name=string(f,"name"),*format=string(f,"format");size_t n=strlen(name);
    json_object *private=field(f,"private");
    if(private&&strcmp(json_object_get_string(private),"false")&&strcmp(json_object_get_string(private),"0"))return 0;
    return n>4&&n<1024&&!strcasecmp(name+n-4,".mp4")&&(!strcasecmp(format,"h.264")||!strcasecmp(format,"MPEG4"));
}
static int archive_files_json(json_object *root,const char *id,int page){
    json_object *files=field(root,"files");if(!files||!json_object_is_type(files,json_type_array))return -1;
    for(size_t i=0;i<(size_t)json_object_array_length(files);i++){
        json_object *f=json_object_array_get_idx(files,i);if(!archive_video(f))continue;
        if(total++<(page-1)*PAGE_SIZE||used==PAGE_SIZE)continue;
        char *name=curl_easy_escape(NULL,string(f,"name"),0);if(!name)return -1;
        Entry *e=&entries[used];
        int length=snprintf(e->url,sizeof(e->url),ARCHIVE "/download/%s/%s",id,name);curl_free(name);
        if(length>=(int)sizeof(e->url))return -1;
        clean(e->title,sizeof(e->title),string(f,"name"));strcpy(e->kind,"archive-file");
        snprintf(e->meta,sizeof(e->meta),"MP4 / %.12s / A PLAY",string(f,"format"));
        snprintf(e->poster,sizeof(e->poster),ARCHIVE "/services/img/%s",id);used++;
    }return 0;
}
static int archive_files(int page,const char *id){
    if(!archive_identifier(id))return -1;
    char url[256];snprintf(url,sizeof(url),ARCHIVE "/metadata/%s",id);
    json_object *root=archive_json(url);if(!root)return -1;
    int rc=archive_files_json(root,id,page);json_object_put(root);return rc;
}
