#ifndef FLXTR_PLATFORM_PATHS_H
#define FLXTR_PLATFORM_PATHS_H
#include "platform_files.h"
#include <limits.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sddl.h>
/* Caller frees lpSecurityDescriptor with LocalFree. Explicit current-user DACL
 * keeps temporary key files and local IPC private even in shared directories. */
static inline int flxtr_private_security(SECURITY_ATTRIBUTES *security){
    HANDLE token=NULL;DWORD bytes=0;PTOKEN_USER user=NULL;LPWSTR sid=NULL;
    PSECURITY_DESCRIPTOR descriptor=NULL;int result=-1;
    memset(security,0,sizeof(*security));security->nLength=sizeof(*security);
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))goto done;
    GetTokenInformation(token,TokenUser,NULL,0,&bytes);
    user=(PTOKEN_USER)malloc(bytes);if(!user)goto done;
    if(!GetTokenInformation(token,TokenUser,user,bytes,&bytes)||
       !ConvertSidToStringSidW(user->User.Sid,&sid))goto done;
    size_t count=wcslen(sid)+32;
    wchar_t *sddl=(wchar_t *)malloc(count*sizeof(*sddl));if(!sddl)goto done;
    swprintf(sddl,count,L"D:P(A;;GA;;;%ls)",sid);
    if(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&descriptor,NULL)){
        security->lpSecurityDescriptor=descriptor;result=0;
    }
    free(sddl);
done:
    if(token)CloseHandle(token);
    if(sid)LocalFree(sid);
    free(user);
    if(result)errno=EACCES;
    return result;
}
static inline int flxtr_path_utf8(const wchar_t *wide,char *out,size_t size){
    if(!wide||!out||!size||size>INT_MAX){errno=EINVAL;return -1;}
    int count=WideCharToMultiByte(CP_UTF8,0,wide,-1,NULL,0,NULL,NULL);
    if(count<=0||(size_t)count>size){errno=ENAMETOOLONG;return -1;}
    return WideCharToMultiByte(CP_UTF8,0,wide,-1,out,(int)size,NULL,NULL)?0:-1;
}
#else
#include <sys/statvfs.h>
#include <unistd.h>
#endif
static inline FILE *flxtr_fopen(const char *path,const char *mode){
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(path),*how=flxtr_process_wide(mode);
    if(!wide||!how){free(wide);free(how);return NULL;}
    FILE *file=_wfopen(wide,how);free(wide);free(how);return file;
#else
    return fopen(path,mode);
#endif
}
static inline int flxtr_path_access(const char *path,int mode){
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(path);if(!wide)return -1;
    int result=_waccess(wide,mode);free(wide);return result;
#else
    return access(path,mode);
#endif
}
static inline int flxtr_path_unlink(const char *path){
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(path);if(!wide)return -1;
    int result=_wunlink(wide);free(wide);return result;
#else
    return unlink(path);
#endif
}
static inline int flxtr_path_mkdir(const char *path){
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(path);if(!wide)return -1;
    int result=_wmkdir(wide);free(wide);return result;
#else
    return mkdir(path,0700);
#endif
}
static inline int flxtr_disk_free(const char *directory,uint64_t *available){
    if(!directory||!available){errno=EINVAL;return -1;}
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(directory);if(!wide)return -1;
    ULARGE_INTEGER free_bytes;
    int ok=GetDiskFreeSpaceExW(wide,&free_bytes,NULL,NULL);free(wide);
    if(!ok){errno=EIO;return -1;}
    *available=free_bytes.QuadPart;
#else
    struct statvfs space;if(statvfs(directory,&space))return -1;
    *available=(uint64_t)space.f_bavail*space.f_frsize;
#endif
    return 0;
}
static inline int flxtr_fd_close(int fd){
#ifdef _WIN32
    return _close(fd);
#else
    return close(fd);
#endif
}
static inline int flxtr_fd_sync(int fd){
#ifdef _WIN32
    return _commit(fd);
#else
    return fsync(fd);
#endif
}
static inline ptrdiff_t flxtr_fd_write(int fd,const void *bytes,size_t size){
#ifdef _WIN32
    unsigned int count=size>INT_MAX?INT_MAX:(unsigned int)size;
    return _write(fd,bytes,count);
#else
    return write(fd,bytes,size);
#endif
}
/* template must end in XXXXXX; capacity includes the terminator.
 * Windows uses a longer random suffix. Both implementations use exclusive
 * creation, private permissions and non-inheritable binary file descriptors. */
static inline int flxtr_temp_file(char *path,size_t capacity){
    if(!path){errno=EINVAL;return -1;}
    size_t size=strlen(path);
    if(size<6||strcmp(path+size-6,"XXXXXX")){errno=EINVAL;return -1;}
#ifdef _WIN32
    if(size+27>capacity){errno=ENAMETOOLONG;return -1;}
    SECURITY_ATTRIBUTES security;if(flxtr_private_security(&security))return -1;
    int fd=-1;
    for(int attempt=0;attempt<128;attempt++){
        unsigned char nonce[16];if(flxtr_random_bytes(nonce,sizeof(nonce)))break;
        for(size_t i=0;i<16;i++)snprintf(path+size-6+i*2,3,"%02x",nonce[i]);
        wchar_t *wide=flxtr_process_wide(path);if(!wide)break;
        HANDLE file=CreateFileW(wide,GENERIC_READ|GENERIC_WRITE,0,&security,CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL,NULL);
        DWORD error=GetLastError();free(wide);
        if(file==INVALID_HANDLE_VALUE){
            if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)continue;
            errno=EACCES;break;
        }
        fd=_open_osfhandle((intptr_t)file,_O_RDWR|_O_BINARY|_O_NOINHERIT);
        if(fd<0){CloseHandle(file);flxtr_path_unlink(path);}
        break;
    }
    LocalFree(security.lpSecurityDescriptor);return fd;
#else
    if(size>=capacity){errno=ENAMETOOLONG;return -1;}
    int fd=mkstemp(path);if(fd<0)return -1;
    if(fchmod(fd,0600)||fcntl(fd,F_SETFD,FD_CLOEXEC)){
        int saved=errno;close(fd);unlink(path);errno=saved;return -1;
    }
    return fd;
#endif
}
/* Publish an upload without replacing an existing target. Both paths must be
 * on the same filesystem; failure retains the temporary source for cleanup. */
static inline int flxtr_install_new(const char *source,const char *destination){
#ifdef _WIN32
    wchar_t *src=flxtr_process_wide(source),*dst=flxtr_process_wide(destination);
    if(!src||!dst){free(src);free(dst);return -1;}
    int ok=MoveFileExW(src,dst,MOVEFILE_WRITE_THROUGH);
    DWORD error=GetLastError();free(src);free(dst);
    if(!ok){errno=(error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS)?EEXIST:EIO;return -1;}
    return 0;
#else
    if(link(source,destination))return -1;
    /* Target is installed even if temporary-name cleanup must be retried. */
    unlink(source);return 0;
#endif
}
static inline int flxtr_regular_file_nofollow(const char *path){
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(path);if(!wide)return 0;
    DWORD attributes=GetFileAttributesW(wide);free(wide);
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;return !lstat(path,&st)&&S_ISREG(st.st_mode);
#endif
}
static inline int flxtr_data_directory(char *out,size_t size){
    if(!out||!size){errno=EINVAL;return -1;}
#ifdef _WIN32
    wchar_t base[32768];DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);
    if(!count||count>=32768){errno=ENOENT;return -1;}
    size_t used=wcslen(base);
    if(used+8>=32768){errno=ENAMETOOLONG;return -1;}
    wcscat(base,L"\\FlXtR");return flxtr_path_utf8(base,out,size);
#else
    const char *base=getenv("XDG_DATA_HOME");int count;
    if(base&&base[0]=='/')count=snprintf(out,size,"%s/flxtr",base);
    else{
        base=getenv("HOME");if(!base||base[0]!='/'){errno=ENOENT;return -1;}
        count=snprintf(out,size,"%s/.local/share/flxtr",base);
    }
    if(count<0||(size_t)count>=size){errno=ENAMETOOLONG;return -1;}
    return 0;
#endif
}
/* Reject Windows device names on every platform, including CON.anything.mp4.
 * Upload basenames must also pass the caller's character/extension policy. */
static inline int flxtr_reserved_basename(const char *name){
    char stem[8]={0};size_t i=0;
    while(name[i]&&name[i]!='.'&&i<sizeof(stem)-1){
        unsigned char ch=(unsigned char)name[i];stem[i]=(char)(ch>='a'&&ch<='z'?ch-32:ch);i++;
    }
    if(name[i]&&name[i]!='.')return 0;
    return !strcmp(stem,"CON")||!strcmp(stem,"PRN")||!strcmp(stem,"AUX")||
           !strcmp(stem,"NUL")||((!strncmp(stem,"COM",3)||!strncmp(stem,"LPT",3))&&
           stem[3]>='1'&&stem[3]<='9'&&!stem[4]);
}
#endif
