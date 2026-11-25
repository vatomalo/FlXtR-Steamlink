#ifndef FLXTR_SOM_PATHS_H
#define FLXTR_SOM_PATHS_H
/* Every art pack, setting and progress file has a fixed Steam Link path. On a
 * device without that folder (Android) FLXTR_SOM_DATA_DIR names a writable one
 * and the same file names are used there; unset, nothing changes. Include
 * after the system headers and before any file access. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define SOM_DATA_PREFIX "/home/apps/retroarch/cores/experimental/"
static inline const char *som_data_path(const char *path,char *out,size_t size){
    const char *dir=getenv("FLXTR_SOM_DATA_DIR");
    const size_t n=sizeof(SOM_DATA_PREFIX)-1u;
    if(!path||!dir||!*dir||strncmp(path,SOM_DATA_PREFIX,n))return path;
    const int len=snprintf(out,size,"%s/%s",dir,path+n);
    return len>0&&(size_t)len<size?out:path;
}
static inline FILE *som_data_fopen(const char *path,const char *mode){
    char b[1024];return fopen(som_data_path(path,b,sizeof(b)),mode);
}
static inline int som_data_access(const char *path,int mode){
    char b[1024];return access(som_data_path(path,b,sizeof(b)),mode);
}
static inline int som_data_unlink(const char *path){
    char b[1024];return unlink(som_data_path(path,b,sizeof(b)));
}
static inline int som_data_rename(const char *from,const char *to){
    char a[1024],b[1024];
    return rename(som_data_path(from,a,sizeof(a)),som_data_path(to,b,sizeof(b)));
}
static inline int som_data_mkdir(const char *path,mode_t mode){
    char b[1024];return mkdir(som_data_path(path,b,sizeof(b)),mode);
}
#define fopen som_data_fopen
#define access som_data_access
#define unlink som_data_unlink
#define rename som_data_rename
#define mkdir som_data_mkdir
#endif
