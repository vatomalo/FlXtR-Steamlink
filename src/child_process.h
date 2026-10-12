#ifndef FLXTR_CHILD_PROCESS_H
#define FLXTR_CHILD_PROCESS_H
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

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
