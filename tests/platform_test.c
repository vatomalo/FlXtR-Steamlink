#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform_process.h"
#include "../src/platform_files.h"
static const char *arguments[]={"","spaces in a title","quote\"inside","tail\\",
    "slashes\\\\\"quote","literal $(echo wrong); & | >",
    "\xc3\xb8 \xe9\x9b\xaa \xf0\x9f\x9a\x80",NULL};
static void pause_ms(int ms){
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec delay={ms/1000,(ms%1000)*1000000L};
    while(nanosleep(&delay,&delay)&&errno==EINTR){}
#endif
}
static void check_file(const char *path,const char *expected){
    char text[256]={0};FILE *f=fopen(path,"rb");assert(f);
    size_t n=fread(text,1,sizeof(text)-1,f);assert(!ferror(f));fclose(f);
    assert(n==strlen(expected)&&!strcmp(text,expected));
}
static int run(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"--arguments")){
        assert(argc==10);
        for(int i=0;arguments[i];i++)assert(!strcmp(argv[i+2],arguments[i]));
        unsigned long long inherited=strtoull(argv[9],NULL,10);
#ifdef _WIN32
        DWORD flags;assert(!GetHandleInformation((HANDLE)(uintptr_t)inherited,&flags));
#else
        assert(fcntl((int)inherited,F_GETFD)==-1&&errno==EBADF);
#endif
        fputs("stdout exact",stdout);fflush(stdout);fputs("stderr exact",stderr);return 23;
    }
    if(argc>1&&!strcmp(argv[1],"--lock")){
        FlxtrFileLock lock={0};
        int result=flxtr_file_lock(&lock,"platform-test.lock");
        if(argc==3&&!strcmp(argv[2],"busy")){
            assert(result<0&&(errno==EWOULDBLOCK||errno==EAGAIN));return 0;
        }
        assert(result==0);flxtr_file_unlock(&lock);return 0;
    }
    if(argc>1&&!strcmp(argv[1],"--sleep")){pause_ms(5000);return 0;}
    if(argc>1&&!strcmp(argv[1],"--exit"))return 7;
#ifdef _WIN32
    puts("FlXtR Windows platform tests (port foundation; not the full application)");
    wchar_t module[32768];DWORD n=GetModuleFileNameW(NULL,module,32768);assert(n&&n<32768);
    char executable[131072];
    assert(WideCharToMultiByte(CP_UTF8,0,module,-1,executable,sizeof(executable),NULL,NULL)>0);
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};
    HANDLE inherited[128];for(size_t i=0;i<128;i++){inherited[i]=CreateEventW(&security,TRUE,FALSE,NULL);assert(inherited[i]);}
    unsigned long long sentinel=(unsigned long long)(uintptr_t)inherited[127];
#else
    puts("FlXtR POSIX platform tests");
    const char *executable=argv[0];
    int inherited=open("platform-parent-handle",O_CREAT|O_RDWR,0600);assert(inherited>=3);
    int original_flags=fcntl(inherited,F_GETFD);assert(original_flags>=0);
    unsigned long long sentinel=(unsigned long long)inherited;
#endif
    char handle[32];snprintf(handle,sizeof(handle),"%llu",sentinel);
    const char *args[11]={executable,"--arguments"};
    for(int i=0;i<7;i++)args[i+2]=arguments[i];
    args[9]=handle;args[10]=NULL;
    FlxtrProcess child={0};FlxtrSpawnOptions streams={.stdout_path="platform-stdout.txt",.stderr_path="platform-stderr.txt"};
    assert(flxtr_process_start(&child,executable,args,&streams)==0);
    assert(flxtr_process_start(&child,executable,args,&streams)<0&&errno==EBUSY);
    int code=-1;assert(flxtr_process_wait(&child,10000,&code)==1&&code==23);
    assert(flxtr_process_poll(&child,&code)==1&&code==23);
    check_file("platform-stdout.txt","stdout exact");check_file("platform-stderr.txt","stderr exact");
    FlxtrSpawnOptions combined={.stdout_path="platform-combined.txt",.stderr_path="platform-combined.txt"};
    assert(flxtr_process_start(&child,executable,args,&combined)==0);
    assert(flxtr_process_wait(&child,10000,&code)==1&&code==23);
    check_file("platform-combined.txt","stdout exactstderr exact");
#ifdef _WIN32
    for(size_t i=0;i<128;i++){DWORD flags;assert(GetHandleInformation(inherited[i],&flags));CloseHandle(inherited[i]);}
#else
    assert(fcntl(inherited,F_GETFD)==original_flags);close(inherited);remove("platform-parent-handle");
#endif
    puts("PASS: empty/quoted/Unicode argv, no shell parsing, descriptor isolation and exit status");
    const char *missing[]={"missing",NULL};
    assert(flxtr_process_start(&child,"./flxtr-no-such-executable",missing,NULL)<0&&errno==ENOENT);
    assert(!child.running);
#ifdef _WIN32
    DWORD before_handles=0,after_handles=0;
    assert(GetProcessHandleCount(GetCurrentProcess(),&before_handles));
#endif
    const char *exit_args[]={executable,"--exit",NULL};
    for(int i=0;i<20;i++){
        assert(flxtr_process_start(&child,executable,exit_args,NULL)==0);
        assert(flxtr_process_wait(&child,5000,&code)==1&&code==7);
        assert(flxtr_process_start(&child,"./flxtr-no-such-executable",missing,NULL)<0);
    }
#ifdef _WIN32
    char *large=(char *)malloc(40000);assert(large);memset(large,'x',39999);large[39999]=0;
    const char *long_args[]={executable,large,NULL};
    assert(flxtr_process_start(&child,executable,long_args,NULL)<0&&errno==E2BIG);free(large);
    assert(GetProcessHandleCount(GetCurrentProcess(),&after_handles));
    assert(before_handles==after_handles);
#endif
    const char *sleep_args[]={executable,"--sleep",NULL};
    assert(flxtr_process_start(&child,executable,sleep_args,NULL)==0);
    assert(flxtr_process_wait(&child,20,NULL)==0);
    assert(flxtr_process_stop(&child,1)==0);
    assert(flxtr_process_wait(&child,5000,&code)==1&&code>=128);
    assert(flxtr_process_stop(&child,1)==0);
    assert(flxtr_process_wait(&child,-2,NULL)<0&&errno==EINVAL);
    puts("PASS: missing executable, bounded wait, forced stop and process reuse");
    unsigned char first[32],second[32];
    assert(flxtr_random_bytes(first,sizeof(first))==0);
    assert(flxtr_random_bytes(second,sizeof(second))==0);
    assert(memcmp(first,second,sizeof(first)));
    assert(flxtr_random_bytes(NULL,0)==0);
    assert(flxtr_random_bytes(NULL,1)<0&&errno==EINVAL);
    puts("PASS: cryptographic session randomness");
    FILE *f=fopen("platform-test.lock","wb");assert(f);fputs("preserve lock contents",f);fclose(f);
    FlxtrFileLock lock={0};assert(flxtr_file_lock(&lock,"platform-test.lock")==0);
    const char *busy[]={executable,"--lock","busy",NULL};
    assert(flxtr_process_start(&child,executable,busy,NULL)==0);
    assert(flxtr_process_wait(&child,5000,&code)==1&&code==0);
    flxtr_file_unlock(&lock);flxtr_file_unlock(&lock);
    const char *available[]={executable,"--lock",NULL};
    assert(flxtr_process_start(&child,executable,available,NULL)==0);
    assert(flxtr_process_wait(&child,5000,&code)==1&&code==0);
    check_file("platform-test.lock","preserve lock contents");
    f=fopen("platform old.txt","wb");assert(f);fputs("new value",f);fclose(f);
    f=fopen("platform new.txt","wb");assert(f);fputs("old value",f);fclose(f);
    assert(flxtr_replace_file("platform old.txt","platform new.txt")==0);
    check_file("platform new.txt","new value");
    assert(flxtr_replace_file("platform missing.txt","platform new.txt")<0);
    check_file("platform new.txt","new value");
    puts("PASS: cross-process locks and replace-existing file behavior");
    remove("platform-combined.txt");remove("platform-stdout.txt");remove("platform-stderr.txt");remove("platform-test.lock");remove("platform new.txt");
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t **wide){
    char **argv=(char **)calloc((size_t)argc+1,sizeof(*argv));assert(argv);
    for(int i=0;i<argc;i++){
        int n=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,NULL,0,NULL,NULL);assert(n>0);
        argv[i]=(char *)malloc((size_t)n);assert(argv[i]);
        assert(WideCharToMultiByte(CP_UTF8,0,wide[i],-1,argv[i],n,NULL,NULL)==n);
    }
    int result=run(argc,argv);
    for(int i=0;i<argc;i++)free(argv[i]);
    free(argv);return result;
}
#else
int main(int argc,char **argv){return run(argc,argv);}
#endif
