#ifndef FLXTR_CHILD_PROCESS_H
#define FLXTR_CHILD_PROCESS_H
#ifdef _WIN32
#include "windows_runtime.h"
static inline pid_t shell_spawn(const char *file,const char *const args[],const char *out,const char *err){
    return flxtr_win_spawn(file,args,out,err,NULL,0,0);
}
#else
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "platform_process.h"

/* Transitional POSIX adapter for callers still using pid_t/waitpid.
 * New code should own a FlxtrProcess rather than a bare process ID. */
static inline pid_t shell_spawn(const char *executable,const char *const argv[],
                                const char *out,const char *err){
    FlxtrProcess child={0};FlxtrSpawnOptions options={.stdout_path=out,.stderr_path=err};
    if(flxtr_process_start(&child,executable,argv,&options))return -1;
    return (pid_t)child.id;
}

/* Vendor graphics descriptors are not CLOEXEC. A catalog worker must not
 * retain the shell's media session after exec (or pass it to its workers).
 * Preserve descriptors until exec so child-side dup2 piping still works. */
static pid_t shell_fork(void){
    DIR *fds=opendir("/proc/self/fd");
    if(fds){
        struct dirent *entry;
        while((entry=readdir(fds))){
            char *end;long fd=strtol(entry->d_name,&end,10);
            if(*end||fd<3)continue;
            int flags=fcntl((int)fd,F_GETFD);
            if(flags>=0)fcntl((int)fd,F_SETFD,flags|FD_CLOEXEC);
        }
        closedir(fds);
    }else{
        long limit=sysconf(_SC_OPEN_MAX);
        if(limit<0)return -1;
        for(int fd=3;fd<limit;fd++){
            int flags=fcntl(fd,F_GETFD);
            if(flags>=0)fcntl(fd,F_SETFD,flags|FD_CLOEXEC);
        }
    }
    return fork();
}
#endif
#endif
