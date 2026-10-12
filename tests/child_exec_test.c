#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include "../src/child_process.h"
int main(int argc,char **argv){
    if(argc==3){
        int fd=atoi(argv[2]);
        assert(fcntl(fd,F_GETFD)==-1&&errno==EBADF);
        assert(write(STDOUT_FILENO,"ok",2)==2);
        return 0;
    }
    int hardware=open("/dev/null",O_RDWR),pipes[2];
    assert(hardware>=3&&pipe(pipes)==0);
    char number[20];snprintf(number,sizeof(number),"%d",hardware);
    pid_t child=shell_fork();assert(child>=0);
    if(!child){
        assert(dup2(pipes[1],STDOUT_FILENO)==STDOUT_FILENO);
        execl(argv[0],argv[0],"--child",number,(char*)NULL);_exit(127);
    }
    close(pipes[1]);char reply[3]={0};assert(read(pipes[0],reply,2)==2);
    int status;assert(waitpid(child,&status,0)==child);
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==0&&!strcmp(reply,"ok"));
    assert(fcntl(hardware,F_GETFD)>=0);close(hardware);close(pipes[0]);
    puts("PASS: exec drops inherited hardware handles, preserves redirected stdout and parent handles");
    return 0;
}
