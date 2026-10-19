#ifndef FLXTR_PLATFORM_PIPE_H
#define FLXTR_PLATFORM_PIPE_H
#include "platform_paths.h"
#include <inttypes.h>
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <time.h>
#endif
typedef intptr_t FlxtrPipeHandle;
#define FLXTR_INVALID_PIPE ((FlxtrPipeHandle)-1)
typedef struct {FlxtrPipeHandle reader,writer;} FlxtrPipe;
/* Synchronous, nonblocking byte pipes for the existing poll-driven player.
 * Read: positive count, 0 EOF, -1 with EAGAIN while empty.
 * Write: positive count (possibly partial), -1 EAGAIN while full, EPIPE if closed.
 * Windows uses private local named pipes in NOWAIT mode, not overlapped I/O. */
static inline void flxtr_pipe_close(FlxtrPipeHandle handle){
    if(handle<0)return;
#ifdef _WIN32
    CloseHandle((HANDLE)handle);
#else
    close((int)handle);
#endif
}
static inline int flxtr_pipe_nonblocking(FlxtrPipeHandle handle){
#ifdef _WIN32
    DWORD current=0;
    if(GetNamedPipeHandleStateW((HANDLE)handle,&current,NULL,NULL,NULL,NULL,0)&&(current&PIPE_NOWAIT))return 0;
    DWORD mode=PIPE_READMODE_BYTE|PIPE_NOWAIT;
    if(!SetNamedPipeHandleState((HANDLE)handle,&mode,NULL,NULL)){errno=EIO;return -1;}
    return 0;
#else
    int flags=fcntl((int)handle,F_GETFL);
    return flags<0?-1:fcntl((int)handle,F_SETFL,flags|O_NONBLOCK);
#endif
}
static inline int flxtr_pipe_create(FlxtrPipe *pipe_handle){
    if(!pipe_handle){errno=EINVAL;return -1;}
    pipe_handle->reader=pipe_handle->writer=FLXTR_INVALID_PIPE;
#ifdef _WIN32
    unsigned char nonce[16];if(flxtr_random_bytes(nonce,sizeof(nonce)))return -1;
    wchar_t name[128],hex[33];
    for(size_t i=0;i<16;i++)swprintf(hex+i*2,3,L"%02x",(unsigned)nonce[i]);
    swprintf(name,128,L"\\\\.\\pipe\\FlXtR-%lu-%ls",GetCurrentProcessId(),hex);
    SECURITY_ATTRIBUTES security;if(flxtr_private_security(&security))return -1;
    HANDLE reader=CreateNamedPipeW(name,PIPE_ACCESS_INBOUND|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,
        1,4096,4096,0,&security);
    LocalFree(security.lpSecurityDescriptor);
    if(reader==INVALID_HANDLE_VALUE){errno=EIO;return -1;}
    HANDLE writer=CreateFileW(name,GENERIC_WRITE|FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES,
        0,NULL,OPEN_EXISTING,0,NULL);
    if(writer==INVALID_HANDLE_VALUE){CloseHandle(reader);errno=EIO;return -1;}
    /* A client may connect before ConnectNamedPipe is called. */
    if(!ConnectNamedPipe(reader,NULL)&&GetLastError()!=ERROR_PIPE_CONNECTED){
        CloseHandle(writer);CloseHandle(reader);errno=EIO;return -1;
    }
    pipe_handle->reader=(intptr_t)reader;pipe_handle->writer=(intptr_t)writer;
#else
    int descriptors[2];if(pipe(descriptors))return -1;
    pipe_handle->reader=descriptors[0];pipe_handle->writer=descriptors[1];
    if(fcntl(descriptors[0],F_SETFD,FD_CLOEXEC)||fcntl(descriptors[1],F_SETFD,FD_CLOEXEC))goto fail;
#endif
    if(flxtr_pipe_nonblocking(pipe_handle->reader)||flxtr_pipe_nonblocking(pipe_handle->writer))goto fail;
    return 0;
fail:
    flxtr_pipe_close(pipe_handle->reader);flxtr_pipe_close(pipe_handle->writer);
    pipe_handle->reader=pipe_handle->writer=FLXTR_INVALID_PIPE;return -1;
}
/* Export a separate inheritable endpoint. Add it to FlxtrSpawnOptions.pass_handles,
 * pass its numeric value to the child, and close this duplicate after spawning.
 * The original endpoint stays non-inheritable. */
static inline FlxtrPipeHandle flxtr_pipe_export(FlxtrPipeHandle original){
    if(original<0){errno=EINVAL;return FLXTR_INVALID_PIPE;}
#ifdef _WIN32
    HANDLE copy=NULL;
    if(!DuplicateHandle(GetCurrentProcess(),(HANDLE)original,GetCurrentProcess(),&copy,
                        0,TRUE,DUPLICATE_SAME_ACCESS)){errno=EIO;return FLXTR_INVALID_PIPE;}
    return (intptr_t)copy;
#else
    return dup((int)original);
#endif
}
static inline ptrdiff_t flxtr_pipe_read(FlxtrPipeHandle handle,void *bytes,size_t size){
    if(handle<0||(!bytes&&size)){errno=EINVAL;return -1;}
    if(!size)return 0;
#ifdef _WIN32
    DWORD count=0,amount=size>INT_MAX?INT_MAX:(DWORD)size;
    if(ReadFile((HANDLE)handle,bytes,amount,&count,NULL))return count;
    DWORD error=GetLastError();
    if(error==ERROR_BROKEN_PIPE)return 0;
    errno=error==ERROR_NO_DATA||error==ERROR_PIPE_LISTENING?EAGAIN:EIO;return -1;
#else
    return read((int)handle,bytes,size);
#endif
}
static inline ptrdiff_t flxtr_pipe_write(FlxtrPipeHandle handle,const void *bytes,size_t size){
    if(handle<0||(!bytes&&size)){errno=EINVAL;return -1;}
    if(!size)return 0;
#ifdef _WIN32
    DWORD count=0,amount=size>INT_MAX?INT_MAX:(DWORD)size;
    if(WriteFile((HANDLE)handle,bytes,amount,&count,NULL)){
        if(!count){errno=EAGAIN;return -1;}
        return count;
    }
    DWORD error=GetLastError();
    errno=error==ERROR_NO_DATA||error==ERROR_BROKEN_PIPE?EPIPE:EIO;return -1;
#else
    /* Avoid process-wide SIGPIPE changes. Block it in this thread for write,
     * consuming only a newly generated SIGPIPE before restoring the mask. */
    sigset_t set,old,pending;sigemptyset(&set);sigaddset(&set,SIGPIPE);
    int error=pthread_sigmask(SIG_BLOCK,&set,&old);
    if(error){errno=error;return -1;}
    int already=sigpending(&pending)==0&&sigismember(&pending,SIGPIPE);
    ssize_t count=write((int)handle,bytes,size);int saved=errno;
    if(count<0&&saved==EPIPE&&!already){
        struct timespec zero={0,0};
        while(sigtimedwait(&set,NULL,&zero)<0&&errno==EINTR){}
    }
    pthread_sigmask(SIG_SETMASK,&old,NULL);errno=saved;return count;
#endif
}
static inline int flxtr_pipe_parse(const char *text,FlxtrPipeHandle *handle){
    if(!text||!*text||!handle){errno=EINVAL;return -1;}
    for(const char *p=text;*p;p++)if(*p<'0'||*p>'9'){errno=EINVAL;return -1;}
    errno=0;char *end;uintmax_t value=strtoumax(text,&end,10);
#ifdef _WIN32
    uintmax_t limit=INTPTR_MAX;
#else
    uintmax_t limit=INT_MAX;
#endif
    if(errno||*end||value>limit||value<3){errno=EINVAL;return -1;}
    *handle=(FlxtrPipeHandle)value;return 0;
}
#endif
