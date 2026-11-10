#ifndef FLXTR_PLATFORM_FILES_H
#define FLXTR_PLATFORM_FILES_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32
#include "platform_process.h"
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
typedef struct {
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
    int held;
} FlxtrFileLock;
/* Cryptographic randomness for phone pairing/session tokens. Never rand(). */
static inline int flxtr_random_bytes(void *buffer,size_t size){
    if(!buffer&&size){errno=EINVAL;return -1;}
#ifdef _WIN32
    unsigned char *out=(unsigned char *)buffer;
    while(size){
        ULONG chunk=size>0xffffffffUL?0xffffffffUL:(ULONG)size;
        if(BCryptGenRandom(NULL,out,chunk,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){errno=EIO;return -1;}
        out+=chunk;size-=chunk;
    }
    return 0;
#else
    if(!size)return 0;
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(fd<0)return -1;
    unsigned char *out=(unsigned char *)buffer;size_t used=0;
    while(used<size){
        ssize_t count=read(fd,out+used,size-used);
        if(count<0&&errno==EINTR)continue;
        if(count<=0){int saved=count<0?errno:EIO;close(fd);errno=saved;return -1;}
        used+=(size_t)count;
    }
    close(fd);return 0;
#endif
}
/* Replace a file on the same filesystem. A failure leaves the source in place.
 * Windows sharing violations are reported; they must not cause delete+rename. */
static inline int flxtr_replace_file(const char *source,const char *destination){
    if(!source||!destination){errno=EINVAL;return -1;}
#ifdef _WIN32
    wchar_t *src=flxtr_process_wide(source),*dst=flxtr_process_wide(destination);
    if(!src||!dst){free(src);free(dst);return -1;}
    int ok=MoveFileExW(src,dst,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    free(src);free(dst);
    if(!ok){errno=EIO;return -1;}
    return 0;
#else
    return rename(source,destination);
#endif
}
/* A nonblocking exclusive lock on a persistent lock file.
 * Initialize with {0}; do not unlink the lock file while it is in use. */
static inline int flxtr_file_lock(FlxtrFileLock *lock,const char *path){
    if(!lock||!path){errno=EINVAL;return -1;}
    if(lock->held){errno=EBUSY;return -1;}
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(path);
    if(!wide)return -1;
    HANDLE handle=CreateFileW(wide,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                              NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    free(wide);
    if(handle==INVALID_HANDLE_VALUE){errno=EIO;return -1;}
    OVERLAPPED offset;memset(&offset,0,sizeof(offset));
    if(!LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&offset)){
        DWORD error=GetLastError();CloseHandle(handle);
        errno=error==ERROR_LOCK_VIOLATION?EWOULDBLOCK:EIO;return -1;
    }
    lock->handle=handle;
#else
    int fd=open(path,O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(fd<0)return -1;
    if(flock(fd,LOCK_EX|LOCK_NB)){int saved=errno;close(fd);errno=saved;return -1;}
    lock->fd=fd;
#endif
    lock->held=1;return 0;
}
static inline void flxtr_file_unlock(FlxtrFileLock *lock){
    if(!lock||!lock->held)return;
#ifdef _WIN32
    OVERLAPPED offset;memset(&offset,0,sizeof(offset));
    UnlockFileEx(lock->handle,0,1,0,&offset);CloseHandle(lock->handle);lock->handle=NULL;
#else
    flock(lock->fd,LOCK_UN);close(lock->fd);lock->fd=-1;
#endif
    lock->held=0;
}
#endif
