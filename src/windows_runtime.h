#ifndef FLXTR_WINDOWS_RUNTIME_H
#define FLXTR_WINDOWS_RUNTIME_H
#ifdef _WIN32
/* Native adapters for the application's existing PID/file interfaces. There is
 * deliberately no fork emulation: every Windows child is started explicitly. */
#include "platform_net.h"
#include "platform_process.h"
#include "platform_paths.h"
#include "platform_pipe.h"
#include <shellapi.h>
#include <process.h>
#include <io.h>
#include <direct.h>
#include <stdarg.h>
#include <share.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#undef PATH_MAX
#define PATH_MAX 4096
#ifndef X_OK
#define X_OK 1
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef F_OK
#define F_OK 0
#endif
#define SIGKILL 9
#define SIGUSR1 10
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#define WNOHANG 1
#define WIFEXITED(status) (((status)&127)==0)
#define WEXITSTATUS(status) (((status)>>8)&255)
#define WIFSIGNALED(status) (((status)&127)!=0)
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#define FLXTR_CORE_EXT ".dll"
#define FLXTR_CA_BUNDLE flxtr_win_resource("certs/cacert.pem")
static inline char *flxtr_strcasestr(const char *text,const char *needle){
    size_t n=strlen(needle);for(;*text;text++)if(!strncasecmp(text,needle,n))return (char *)text;
    return n?NULL:(char *)text;
}
static inline struct tm *flxtr_localtime_r(const time_t *value,struct tm *out){return localtime_s(out,value)?NULL:out;}
static inline struct tm *flxtr_gmtime_r(const time_t *value,struct tm *out){return gmtime_s(out,value)?NULL:out;}
#define strcasestr flxtr_strcasestr
#define localtime_r flxtr_localtime_r
#define gmtime_r flxtr_gmtime_r
static char flxtr_binary_dir[PATH_MAX];
static HANDLE flxtr_application_job;
static FlxtrProcess flxtr_children[64];
static inline const char *flxtr_win_binary_dir(void){
    if(!flxtr_binary_dir[0]){
        wchar_t wide[32768];DWORD n=GetModuleFileNameW(NULL,wide,32768);
        if(!n||n>=32768||flxtr_path_utf8(wide,flxtr_binary_dir,sizeof(flxtr_binary_dir)))return ".";
        char *slash=strrchr(flxtr_binary_dir,'\\');if(slash)*slash=0;
        for(char *p=flxtr_binary_dir;*p;p++)if(*p=='\\')*p='/';
    }
    return flxtr_binary_dir;
}
static inline int flxtr_win_resource_name(const char *path){
    while(path[0]=='.'&&path[1]=='/')path+=2;
    return !strncmp(path,"assets/",7)||!strncmp(path,"certs/",6)||
           !strncmp(path,"tools/",6)||!strncmp(path,"scripts/",8)||
           !strncmp(path,"music-runtime/",14)||!strcmp(path,"catalog.tsv")||
           !strcmp(path,"linux-controls.lua")||!strcmp(path,"player-menu-v1");
}
static inline const char *flxtr_win_resource(const char *path){
    static __thread char full[PATH_MAX];
    if(!path||!flxtr_win_resource_name(path)||!flxtr_path_access(path,0))return path;
    int n=snprintf(full,sizeof(full),"%s/%s",flxtr_win_binary_dir(),path);
    return n>0&&n<(int)sizeof(full)?full:path;
}
static inline const char *flxtr_win_executable(const char *name){
    static __thread char path[PATH_MAX];
    name=flxtr_win_resource(name);
    const char *base=name;
    if(!strncmp(name,"./",2))base=name+2;
    int local=!strchr(base,'/')&&!strchr(base,'\\')&&!strchr(base,':');
    const char *prefix=local?flxtr_win_binary_dir():NULL;
    int n=snprintf(path,sizeof(path),"%s%s%s%s",prefix?prefix:"",prefix?"/":"",base,
                   strlen(base)>=4&&!strcasecmp(base+strlen(base)-4,".exe")?"":".exe");
    if(n<=0||n>=(int)sizeof(path)){errno=ENAMETOOLONG;return NULL;}
    if(flxtr_path_access(path,0)&&local){
        wchar_t *wide=flxtr_process_wide(base),found[32768];if(!wide)return NULL;
        DWORD count=SearchPathW(NULL,wide,L".exe",32768,found,NULL);free(wide);
        if(count&&count<32768&&!flxtr_path_utf8(found,path,sizeof(path)))return path;
    }
    return path;
}
static inline pid_t flxtr_win_spawn(const char *file,const char *const args[],
                                    const char *out,const char *err,
                                    const uintptr_t *pass,size_t count,int tree){
    size_t slot;
    for(slot=0;slot<64;slot++)if(!flxtr_children[slot].id)break;
    if(slot==64){errno=EAGAIN;return -1;}
    const char *resolved=flxtr_win_executable(file);if(!resolved)return -1;
    if(out&&!strcmp(out,"/dev/null"))out="NUL";
    if(err&&!strcmp(err,"/dev/null"))err="NUL";
    FlxtrSpawnOptions options={.stdout_path=out,.stderr_path=err,.pass_handles=pass,.pass_count=count,.process_tree=tree};
    if(flxtr_process_start(&flxtr_children[slot],resolved,args,&options))return -1;
    return (pid_t)flxtr_children[slot].id;
}
static inline pid_t flxtr_win_waitpid(pid_t id,int *status,int options){
    for(size_t i=0;i<64;i++)if(flxtr_children[i].id==(unsigned long)id){
        int code,result=options&WNOHANG?flxtr_process_poll(&flxtr_children[i],&code):
                                      flxtr_process_wait(&flxtr_children[i],-1,&code);
        if(result<=0)return result;
        if(status)*status=(code&255)<<8;
        memset(&flxtr_children[i],0,sizeof(flxtr_children[i]));return id;
    }
    errno=ECHILD;return -1;
}
static inline int flxtr_win_kill(pid_t id,int signal_number){
    if(id<0)id=-id;
    for(size_t i=0;i<64;i++)if(flxtr_children[i].id==(unsigned long)id){
        if(signal_number==SIGUSR1){
            wchar_t name[80];swprintf(name,80,L"Local\\FlXtR-audio-pause-%lu",(unsigned long)id);
            HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name);
            if(!event){errno=ESRCH;return -1;}
            int ok=SetEvent(event);CloseHandle(event);return ok?0:-1;
        }
        return flxtr_process_stop(&flxtr_children[i],signal_number==SIGKILL);
    }
    errno=ESRCH;return -1;
}
static inline int flxtr_win_run(const char *file,const char *const args[],const char *out,const char *err){
    pid_t id=flxtr_win_spawn(file,args,out,err,NULL,0,0);if(id<0)return -1;
    int status=0;return flxtr_win_waitpid(id,&status,0)==id?WEXITSTATUS(status):-1;
}
static inline int flxtr_win_detach(const char *file,const char *const args[]){
    const char *resolved=flxtr_win_executable(file);if(!resolved)return -1;
    FlxtrProcess child={0};FlxtrSpawnOptions options={.stdout_path="NUL",.stderr_path="NUL"};
    if(flxtr_process_start(&child,resolved,args,&options))return -1;
    CloseHandle((HANDLE)child.native);return 0;
}
static inline int flxtr_win_execl(const char *file,const char *arg,...){
    const char *args[64];size_t used=0;args[used++]=arg;
    va_list ap;va_start(ap,arg);
    while(used<63&&(args[used]=va_arg(ap,const char *))!=NULL)used++;
    va_end(ap);if(used==63){errno=E2BIG;return -1;}args[used]=NULL;
    int code=flxtr_win_run(file,args,NULL,NULL);if(code<0)return -1;
    exit(code);
}
static inline int flxtr_win_execv(const char *file,char *const args[]){
    int code=flxtr_win_run(file,(const char *const *)args,NULL,NULL);if(code<0)return -1;
    exit(code);
}
static inline int flxtr_win_access(const char *path,int mode){
    if(mode==X_OK){
        const char *executable=flxtr_win_executable(path);
        return executable?flxtr_path_access(executable,0):-1;
    }
    return flxtr_path_access(flxtr_win_resource(path),mode);
}
static inline FILE *flxtr_win_fopen(const char *path,const char *mode){
    return flxtr_fopen(mode[0]=='r'&&!strchr(mode,'+')?flxtr_win_resource(path):path,mode);
}
static inline int flxtr_win_open(const char *path,int flags,...){
    int mode=_S_IREAD|_S_IWRITE;
    if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);(void)mode;}
    wchar_t *wide=flxtr_process_wide((flags&(O_WRONLY|O_RDWR|O_CREAT))?path:flxtr_win_resource(path));
    if(!wide)return -1;
    int fd=_wopen(wide,flags|_O_BINARY|_O_NOINHERIT,_S_IREAD|_S_IWRITE);free(wide);return fd;
}
static inline int flxtr_win_flock(int fd,int operation){
    HANDLE file=(HANDLE)_get_osfhandle(fd);OVERLAPPED offset;memset(&offset,0,sizeof(offset));
    if(operation&LOCK_UN)return UnlockFileEx(file,0,1,0,&offset)?0:-1;
    DWORD flags=(operation&LOCK_EX)?LOCKFILE_EXCLUSIVE_LOCK:0;
    if(operation&LOCK_NB)flags|=LOCKFILE_FAIL_IMMEDIATELY;
    if(LockFileEx(file,flags,0,1,0,&offset))return 0;
    errno=GetLastError()==ERROR_LOCK_VIOLATION?EWOULDBLOCK:EIO;return -1;
}
static inline int flxtr_win_mkstemp(char *path){
    size_t n=strlen(path);if(n<6||strcmp(path+n-6,"XXXXXX")){errno=EINVAL;return -1;}
    for(int retry=0;retry<128;retry++){
        unsigned char bytes[6];if(flxtr_random_bytes(bytes,sizeof(bytes)))return -1;
        for(int i=0;i<6;i++)path[n-6+i]="abcdefghijklmnopqrstuvwxyz0123456789"[bytes[i]%36];
        int fd=flxtr_win_open(path,O_RDWR|O_CREAT|O_EXCL,0600);
        if(fd>=0||errno!=EEXIST)return fd;
    }
    errno=EEXIST;return -1;
}
static inline const char *flxtr_win_getenv(const char *name){
    static __thread char values[8][32768];static __thread unsigned slot;
    wchar_t *key=flxtr_process_wide(name),wide[32768];if(!key)return NULL;
    SetLastError(ERROR_SUCCESS);DWORD count=GetEnvironmentVariableW(key,wide,32768);free(key);
    if(count>=32768||(!count&&GetLastError()==ERROR_ENVVAR_NOT_FOUND))return NULL;
    char *value=values[(slot++)%8];
    return flxtr_path_utf8(wide,value,32768)?NULL:value;
}
static inline int flxtr_win_setenv(const char *name,const char *value,int overwrite){
    if(!overwrite&&flxtr_win_getenv(name))return 0;
    wchar_t *key=flxtr_process_wide(name),*wide=flxtr_process_wide(value);
    if(!key||!wide){free(key);free(wide);return -1;}
    int result=_wputenv_s(key,wide);free(key);free(wide);return result;
}
static inline int flxtr_win_unsetenv(const char *name){return flxtr_win_setenv(name,"",1);}
static inline char *flxtr_win_getcwd(char *out,size_t size){
    wchar_t wide[32768];DWORD count=GetCurrentDirectoryW(32768,wide);
    if(!count||count>=32768||flxtr_path_utf8(wide,out,size))return NULL;
    for(char *p=out;*p;p++)if(*p=='\\')*p='/';
    return out;
}
static inline int flxtr_win_stat(const char *path,struct _stat64 *st){
    wchar_t *wide=flxtr_process_wide(flxtr_win_resource(path));if(!wide)return -1;
    int result=_wstat64(wide,st);free(wide);return result;
}
static inline int flxtr_win_lstat(const char *path,struct _stat64 *st){
    wchar_t *wide=flxtr_process_wide(path);if(!wide)return -1;
    DWORD attributes=GetFileAttributesW(wide);free(wide);
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){
        memset(st,0,sizeof(*st));st->st_mode=0120000;return 0;
    }
    return flxtr_win_stat(path,st);
}
static inline char *flxtr_win_realpath(const char *path,char *out){
    wchar_t *wide=flxtr_process_wide(path),full[32768];if(!wide)return NULL;
    DWORD count=GetFullPathNameW(wide,32768,full,NULL);free(wide);
    if(!count||count>=32768||flxtr_path_utf8(full,out,PATH_MAX))return NULL;
    for(char *p=out;*p;p++)if(*p=='\\')*p='/';
    return out;
}
static inline int flxtr_win_chdir(const char *path){
    wchar_t *wide=flxtr_process_wide(path);if(!wide)return -1;
    int result=_wchdir(wide);free(wide);return result;
}
typedef struct FlxtrWinDirent {char d_name[1024];} FlxtrWinDirent;
typedef struct {HANDLE search;WIN32_FIND_DATAW found;int first;FlxtrWinDirent entry;} FlxtrWinDir;
static inline FlxtrWinDir *flxtr_win_opendir(const char *path){
    char pattern[PATH_MAX];if(snprintf(pattern,sizeof(pattern),"%s/*",flxtr_win_resource(path))>=(int)sizeof(pattern))return NULL;
    wchar_t *wide=flxtr_process_wide(pattern);if(!wide)return NULL;
    FlxtrWinDir *dir=(FlxtrWinDir *)calloc(1,sizeof(*dir));if(!dir){free(wide);return NULL;}
    dir->search=FindFirstFileW(wide,&dir->found);free(wide);
    if(dir->search==INVALID_HANDLE_VALUE){free(dir);return NULL;}
    dir->first=1;return dir;
}
static inline FlxtrWinDirent *flxtr_win_readdir(FlxtrWinDir *dir){
    if(!dir)return NULL;
    if(!dir->first&&!FindNextFileW(dir->search,&dir->found))return NULL;
    dir->first=0;
    if(flxtr_path_utf8(dir->found.cFileName,dir->entry.d_name,sizeof(dir->entry.d_name)))return NULL;
    return &dir->entry;
}
static inline int flxtr_win_closedir(FlxtrWinDir *dir){
    if(!dir)return -1;
    int result=FindClose(dir->search)?0:-1;free(dir);return result;
}
static inline int flxtr_win_entry(int (*entry)(int,char **),int argc,wchar_t **wide,int application){
    char **argv=(char **)calloc((size_t)argc+1,sizeof(*argv));if(!argv)return 126;
    for(int i=0;i<argc;i++){
        int count=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,NULL,0,NULL,NULL);
        argv[i]=(char *)malloc((size_t)count);
        if(!argv[i]||!WideCharToMultiByte(CP_UTF8,0,wide[i],-1,argv[i],count,NULL,NULL))return 126;
    }
    if(application){
        char data[PATH_MAX];const char *override=flxtr_win_getenv("FLXTR_DATA_DIR");
        if(override){if(strlen(override)>=sizeof(data))return 126;strcpy(data,override);}
        else if(flxtr_data_directory(data,sizeof(data)))return 126;
        flxtr_path_mkdir(data);
        wchar_t *cwd=flxtr_process_wide(data);if(!cwd||!SetCurrentDirectoryW(cwd)){free(cwd);return 126;}free(cwd);
        flxtr_application_job=CreateJobObjectW(NULL,NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;memset(&limits,0,sizeof(limits));
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(flxtr_application_job&&(!SetInformationJobObject(flxtr_application_job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||
           !AssignProcessToJobObject(flxtr_application_job,GetCurrentProcess()))){
            CloseHandle(flxtr_application_job);flxtr_application_job=NULL;
        }
        /* Keep the job handle until process exit, when descendants are stopped. */
    }
    int result=entry(argc,argv);
    for(int i=0;i<argc;i++)free(argv[i]);
    free(argv);return result;
}
#define FLXTR_WINDOWS_CONSOLE_ENTRY(function) int wmain(int argc,wchar_t **argv){return flxtr_win_entry(function,argc,argv,0);}
#define DIR FlxtrWinDir
#define dirent FlxtrWinDirent
#define opendir flxtr_win_opendir
#define readdir flxtr_win_readdir
#define closedir flxtr_win_closedir
#define chdir flxtr_win_chdir
#define access flxtr_win_access
#define fopen flxtr_win_fopen
#define mkdir(path,mode) flxtr_path_mkdir(path)
#define rename flxtr_replace_file
#define unlink flxtr_path_unlink
#define open flxtr_win_open
#define flock flxtr_win_flock
#define mkstemp flxtr_win_mkstemp
#define fsync _commit
#define fchmod(fd,mode) (0)
#define ftruncate(fd,length) _chsize_s(fd,length)
#define getcwd flxtr_win_getcwd
#define realpath flxtr_win_realpath
#define getenv flxtr_win_getenv
#define setenv flxtr_win_setenv
#define unsetenv flxtr_win_unsetenv
#define waitpid flxtr_win_waitpid
#define kill flxtr_win_kill
#define execl flxtr_win_execl
#define execlp flxtr_win_execl
#define execv flxtr_win_execv
#define stat _stat64
#define _stat64(path,st) flxtr_win_stat(path,st)
#define lstat flxtr_win_lstat
#define link(source,target) flxtr_install_new(source,target)
#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode)&0170000)==0120000)
#endif
#else
#define FLXTR_CORE_EXT ".so"
#define FLXTR_CA_BUNDLE (access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem")
#endif
#endif
