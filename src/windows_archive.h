#ifndef FLXTR_WINDOWS_ARCHIVE_H
#define FLXTR_WINDOWS_ARCHIVE_H
#ifdef _WIN32
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
/* Read ZIP entries without invoking a shell or extracting arbitrary paths. */
static inline struct archive *flxtr_archive_open(const char *path){
    struct archive *a=archive_read_new();if(!a)return NULL;
    archive_read_support_format_zip(a);archive_read_support_filter_all(a);
    wchar_t *wide=flxtr_process_wide(path);
    int rc=wide?archive_read_open_filename_w(a,wide,65536):ARCHIVE_FATAL;free(wide);
    if(rc!=ARCHIVE_OK){archive_read_free(a);return NULL;}return a;
}
static inline FILE *flxtr_archive_listing(const char *path){
    struct archive *a=flxtr_archive_open(path);if(!a)return NULL;
    FILE *f=tmpfile();struct archive_entry *entry;int rc=ARCHIVE_FATAL;
    if(f)while((rc=archive_read_next_header(a,&entry))==ARCHIVE_OK){
        const char *name=archive_entry_pathname_utf8(entry);
        if(name&&archive_entry_filetype(entry)==AE_IFREG&&!strpbrk(name,"\r\n\t"))
            fprintf(f," %llu 2000-01-01 00:00 %s\n",(unsigned long long)archive_entry_size(entry),name);
        archive_read_data_skip(a);
    }
    archive_read_free(a);
    if(!f)return NULL;
    if(rc!=ARCHIVE_EOF||ferror(f)||fflush(f)||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    return f;
}
static inline int flxtr_archive_extract(const char *path,const char *member,int fd,size_t max){
    struct archive *a=flxtr_archive_open(path);if(!a)return -1;
    struct archive_entry *entry;int ok=-1;
    while(archive_read_next_header(a,&entry)==ARCHIVE_OK){
        const char *name=archive_entry_pathname_utf8(entry);
        if(!name||strcmp(name,member)){archive_read_data_skip(a);continue;}
        if(archive_entry_filetype(entry)!=AE_IFREG||archive_entry_size(entry)<0||
           (uint64_t)archive_entry_size(entry)>max)break;
        unsigned char buffer[65536];size_t total=0;la_ssize_t n;
        ok=0;
        while((n=archive_read_data(a,buffer,sizeof(buffer)))>0){
            if((size_t)n>max-total){ok=-1;break;}total+=(size_t)n;
            size_t written=0;
            while(written<(size_t)n){ptrdiff_t step=flxtr_fd_write(fd,buffer+written,(size_t)n-written);if(step<=0){ok=-1;break;}written+=(size_t)step;}
            if(ok)break;
        }
        if(n<0||total!=(size_t)archive_entry_size(entry))ok=-1;
        break;
    }
    archive_read_free(a);return ok;
}
static inline int flxtr_digest_matches(const char *path,const char *expected,const EVP_MD *algorithm){
    FILE *f=flxtr_fopen(path,"rb");if(!f)return 0;
    EVP_MD_CTX *ctx=EVP_MD_CTX_new();unsigned char data[65536],digest[EVP_MAX_MD_SIZE];unsigned length=0;size_t n;
    int ok=ctx&&EVP_DigestInit_ex(ctx,algorithm,NULL)==1;
    while(ok&&(n=fread(data,1,sizeof(data),f))>0)ok=EVP_DigestUpdate(ctx,data,n)==1;
    ok=ok&&!ferror(f)&&EVP_DigestFinal_ex(ctx,digest,&length)==1;
    fclose(f);EVP_MD_CTX_free(ctx);
    if(!ok||strlen(expected)!=length*2)return 0;
    char hex[EVP_MAX_MD_SIZE*2+1];
    for(unsigned i=0;i<length;i++)sprintf(hex+i*2,"%02x",digest[i]);
    return !strcasecmp(hex,expected);
}
#endif
#endif
