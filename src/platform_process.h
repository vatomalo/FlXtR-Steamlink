#ifndef FLXTR_PLATFORM_PROCESS_H
#define FLXTR_PLATFORM_PROCESS_H
/* UTF-8 argv, no command shell. Initialize FlxtrProcess with {0}.
 * poll/wait: 1 = exited, 0 = running/timeout, -1 = error.
 * Successful polling reaps/closes the child and preserves its exit status.
 * A live process must be waited before this object is discarded or reused. */
#include <errno.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#endif
typedef struct {
    uintptr_t native,job_native;
    unsigned long id;
    int running, started, exit_code;
} FlxtrProcess;
typedef struct {
    const char *stdout_path; /* NULL: inherit; file: truncate and redirect. */
    const char *stderr_path; /* Same path as stdout shares its file offset. */
    const uintptr_t *pass_handles; /* Explicit inheritable pipe endpoint copies. */
    size_t pass_count; /* At most 16. Caller retains ownership. */
    int process_tree; /* Windows: own a kill-on-close Job Object. */
} FlxtrSpawnOptions;
#ifdef _WIN32
static inline wchar_t *flxtr_process_wide(const char *text){
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
    if(!count){errno=EINVAL;return NULL;}
    wchar_t *out=(wchar_t *)malloc((size_t)count*sizeof(*out));
    if(!out){errno=ENOMEM;return NULL;}
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,out,count)){
        free(out);errno=EINVAL;return NULL;
    }
    return out;
}
static inline int flxtr_command_char(wchar_t *out,size_t *used,wchar_t ch){
    if(*used>=32766){errno=E2BIG;return -1;}
    out[(*used)++]=ch;return 0;
}
/* Always quote each argument, including empty strings. Escape according to
 * the Microsoft C runtime argv rules (quotes and trailing backslashes). */
static inline wchar_t *flxtr_process_command(const char *const argv[]){
    wchar_t *command=(wchar_t *)calloc(32767,sizeof(*command));size_t used=0;
    if(!command){errno=ENOMEM;return NULL;}
    for(size_t n=0;argv[n];n++){
        wchar_t *arg=flxtr_process_wide(argv[n]);
        if(!arg){free(command);return NULL;}
        int bad=n&&flxtr_command_char(command,&used,L' ');
        if(!bad)bad=flxtr_command_char(command,&used,L'"');
        for(size_t i=0;!bad;){
            size_t slashes=0;
            while(arg[i]==L'\\'){slashes++;i++;}
            size_t emit=(arg[i]==L'"'||!arg[i])?slashes*2:slashes;
            if(arg[i]==L'"')emit++;
            while(emit--&&!bad)bad=flxtr_command_char(command,&used,L'\\');
            if(bad||!arg[i])break;
            bad=flxtr_command_char(command,&used,arg[i++]);
        }
        if(!bad)bad=flxtr_command_char(command,&used,L'"');
        free(arg);
        if(bad){free(command);return NULL;}
    }
    return command;
}
static inline HANDLE flxtr_process_stream(const char *path,DWORD standard,int input){
    HANDLE original=NULL,copy=NULL;
    if(path){
        wchar_t *wide=flxtr_process_wide(path);
        if(!wide)return INVALID_HANDLE_VALUE;
        original=CreateFileW(wide,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                             NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        free(wide);
        if(original==INVALID_HANDLE_VALUE){errno=EIO;return INVALID_HANDLE_VALUE;}
    }else{
        original=GetStdHandle(standard);
        if(!original||original==INVALID_HANDLE_VALUE){
            original=CreateFileW(L"NUL",input?GENERIC_READ:GENERIC_WRITE,
                                 FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
            if(original==INVALID_HANDLE_VALUE){errno=EIO;return INVALID_HANDLE_VALUE;}
            if(!DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&copy,0,TRUE,DUPLICATE_SAME_ACCESS))
                copy=INVALID_HANDLE_VALUE;
            CloseHandle(original);
            return copy;
        }
    }
    if(!DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&copy,0,TRUE,DUPLICATE_SAME_ACCESS))
        copy=INVALID_HANDLE_VALUE;
    if(path)CloseHandle(original);
    return copy;
}
#endif
static inline int flxtr_process_start(FlxtrProcess *process,const char *executable,
                                      const char *const argv[],const FlxtrSpawnOptions *options){
    if(!process||!executable||!executable[0]||!argv||!argv[0]){errno=EINVAL;return -1;}
    if(process->running){errno=EBUSY;return -1;}
    size_t pass_count=options?options->pass_count:0;
    const uintptr_t *pass=options?options->pass_handles:NULL;
    if(pass_count>16||(pass_count&&!pass)){errno=EINVAL;return -1;}
    for(size_t i=0;i<pass_count;i++){
        if(pass[i]<3){errno=EINVAL;return -1;}
        for(size_t j=0;j<i;j++)if(pass[i]==pass[j]){errno=EINVAL;return -1;}
#ifdef _WIN32
        DWORD flags;
        if(!GetHandleInformation((HANDLE)pass[i],&flags)||!(flags&HANDLE_FLAG_INHERIT)){errno=EINVAL;return -1;}
#else
        if(pass[i]>INT_MAX||fcntl((int)pass[i],F_GETFD)<0){errno=EINVAL;return -1;}
#endif
    }
    const char *out=options?options->stdout_path:NULL,*err=options?options->stderr_path:NULL;
#ifdef _WIN32
    wchar_t *wide=flxtr_process_wide(executable),*command=flxtr_process_command(argv);
    if(!wide||!command){free(wide);free(command);return -1;}
    HANDLE handles[19]={INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
    for(size_t i=0;i<pass_count;i++)handles[3+i]=(HANDLE)pass[i];
    handles[0]=flxtr_process_stream(NULL,STD_INPUT_HANDLE,1);
    handles[1]=flxtr_process_stream(out,STD_OUTPUT_HANDLE,0);
    if(out&&err&&!strcmp(out,err)&&handles[1]!=INVALID_HANDLE_VALUE){
        if(!DuplicateHandle(GetCurrentProcess(),handles[1],GetCurrentProcess(),&handles[2],0,TRUE,DUPLICATE_SAME_ACCESS))
            handles[2]=INVALID_HANDLE_VALUE;
    }else handles[2]=flxtr_process_stream(err,STD_ERROR_HANDLE,0);
    int result=-1;SIZE_T bytes=0;HANDLE job=NULL;
    STARTUPINFOEXW startup;PROCESS_INFORMATION info;
    memset(&startup,0,sizeof(startup));memset(&info,0,sizeof(info));
    startup.StartupInfo.cb=sizeof(startup);
    if(handles[0]==INVALID_HANDLE_VALUE||handles[1]==INVALID_HANDLE_VALUE||handles[2]==INVALID_HANDLE_VALUE)goto cleanup;
    InitializeProcThreadAttributeList(NULL,1,0,&bytes);
    startup.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)malloc(bytes);
    if(!startup.lpAttributeList){errno=ENOMEM;goto cleanup;}
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)){
        free(startup.lpAttributeList);startup.lpAttributeList=NULL;goto cleanup;
    }
    if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 handles,(3+pass_count)*sizeof(HANDLE),NULL,NULL))goto cleanup;
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=handles[0];startup.StartupInfo.hStdOutput=handles[1];startup.StartupInfo.hStdError=handles[2];
    if(options&&options->process_tree){
        job=CreateJobObjectW(NULL,NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;memset(&limits,0,sizeof(limits));
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){errno=EIO;goto cleanup;}
    }
    if(!CreateProcessW(wide,command,NULL,NULL,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|(job?CREATE_SUSPENDED:0),
                       NULL,NULL,&startup.StartupInfo,&info)){
        DWORD error=GetLastError();
        errno=(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)?ENOENT:EIO;
        goto cleanup;
    }
    if(job&&(!AssignProcessToJobObject(job,info.hProcess)||ResumeThread(info.hThread)==(DWORD)-1)){
        TerminateProcess(info.hProcess,126);WaitForSingleObject(info.hProcess,5000);
        CloseHandle(info.hThread);CloseHandle(info.hProcess);errno=EIO;goto cleanup;
    }
    CloseHandle(info.hThread);process->job_native=(uintptr_t)job;job=NULL;
    process->native=(uintptr_t)info.hProcess;process->id=info.dwProcessId;
    process->started=process->running=1;process->exit_code=0;result=0;
cleanup:
    if(job)CloseHandle(job);
    if(startup.lpAttributeList){DeleteProcThreadAttributeList(startup.lpAttributeList);free(startup.lpAttributeList);}
    for(size_t i=0;i<3;i++)if(handles[i]!=INVALID_HANDLE_VALUE)CloseHandle(handles[i]);
    free(wide);free(command);return result;
#else
    posix_spawn_file_actions_t actions;int result=posix_spawn_file_actions_init(&actions);
    if(result){errno=result;return -1;}
    if(out)result=posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,out,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(!result&&err){
        if(out&&!strcmp(out,err))result=posix_spawn_file_actions_adddup2(&actions,STDOUT_FILENO,STDERR_FILENO);
        else result=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,err,O_WRONLY|O_CREAT|O_TRUNC,0600);
    }
    for(size_t i=0;!result&&i<pass_count;i++)
        result=posix_spawn_file_actions_adddup2(&actions,(int)pass[i],(int)pass[i]);
    /* Close only in the child. The parent's graphics/socket descriptor flags
     * remain unchanged, and unrelated descriptors never survive exec. */
    DIR *fds=opendir("/proc/self/fd");
    if(!fds)fds=opendir("/dev/fd");
    if(fds){
        struct dirent *entry;int own=dirfd(fds);
        while(!result&&(entry=readdir(fds))){
            char *end;long fd=strtol(entry->d_name,&end,10);
            if(*end||fd<3||fd==own)continue;
            int keep=0;for(size_t i=0;i<pass_count;i++)if(pass[i]==(uintptr_t)fd)keep=1;
            if(keep)continue;
            result=posix_spawn_file_actions_addclose(&actions,(int)fd);
        }
        closedir(fds);
    }else if(!result){
        long limit=sysconf(_SC_OPEN_MAX);
        if(limit<0)result=EINVAL;
        for(int fd=3;!result&&fd<limit;fd++){
            int keep=0;for(size_t i=0;i<pass_count;i++)if(pass[i]==(uintptr_t)fd)keep=1;
            if(!keep&&fcntl(fd,F_GETFD)>=0)result=posix_spawn_file_actions_addclose(&actions,fd);
        }
    }
    pid_t child=0;
    if(!result)result=posix_spawn(&child,executable,&actions,NULL,(char *const *)argv,environ);
    posix_spawn_file_actions_destroy(&actions);
    if(result){errno=result;return -1;}
    process->native=0;process->id=(unsigned long)child;
    process->started=process->running=1;process->exit_code=0;return 0;
#endif
}
static inline int flxtr_process_poll(FlxtrProcess *process,int *exit_code){
    if(!process||!process->started){errno=EINVAL;return -1;}
    if(process->running){
#ifdef _WIN32
        HANDLE handle=(HANDLE)process->native;
        DWORD result=WaitForSingleObject(handle,0),code=0;
        if(result==WAIT_TIMEOUT)return 0;
        if(result!=WAIT_OBJECT_0||!GetExitCodeProcess(handle,&code)){errno=EIO;return -1;}
        process->exit_code=(int)code;
        CloseHandle(handle);process->native=0;
        if(process->job_native){CloseHandle((HANDLE)process->job_native);process->job_native=0;}
#else
        int status;pid_t result;
        do{result=waitpid((pid_t)process->id,&status,WNOHANG);}while(result<0&&errno==EINTR);
        if(result==0)return 0;
        if(result<0)return -1;
        process->exit_code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
#endif
        process->running=0;
    }
    if(exit_code)*exit_code=process->exit_code;
    return 1;
}
static inline int flxtr_process_wait(FlxtrProcess *process,int timeout_ms,int *exit_code){
    if(timeout_ms< -1){errno=EINVAL;return -1;}
    int elapsed=0;
    for(;;){
        int result=flxtr_process_poll(process,exit_code);
        if(result||elapsed==timeout_ms)return result;
        int step=timeout_ms<0||timeout_ms-elapsed>10?10:timeout_ms-elapsed;
#ifdef _WIN32
        Sleep((DWORD)step);
#else
        struct timespec pause_time={0,step*1000000L};
        while(nanosleep(&pause_time,&pause_time)&&errno==EINTR){}
#endif
        if(timeout_ms>=0)elapsed+=step;
    }
}
/* Windows has no SIGTERM equivalent for these background helpers: both modes
 * terminate this child directly. POSIX retains TERM versus KILL semantics.
 * This API owns one child, not a shell or an entire descendant process tree. */
static inline int flxtr_process_stop(FlxtrProcess *process,int force){
    if(!process||!process->started){errno=EINVAL;return -1;}
    int result=flxtr_process_poll(process,NULL);
    if(result)return result<0?-1:0;
#ifdef _WIN32
    BOOL stopped=process->job_native?TerminateJobObject((HANDLE)process->job_native,force?137:143):TerminateProcess((HANDLE)process->native,force?137:143);
    if(!stopped){
        if(flxtr_process_poll(process,NULL)==1)return 0;
        errno=EIO;return -1;
    }
    return 0;
#else
    return kill((pid_t)process->id,force?SIGKILL:SIGTERM);
#endif
}
#endif
