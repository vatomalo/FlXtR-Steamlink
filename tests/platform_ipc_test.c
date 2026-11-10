#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#ifdef _WIN32
#include <aclapi.h>
#endif
#include <stdio.h>
#include <string.h>
#include "../src/platform_pipe.h"
#include "../src/platform_process.h"
static void wait_a_bit(void){
#ifdef _WIN32
    Sleep(5);
#else
    struct timespec t={0,5000000};nanosleep(&t,NULL);
#endif
}
static ptrdiff_t read_eventually(FlxtrPipeHandle handle,void *bytes,size_t size){
    for(int i=0;i<1000;i++){
        ptrdiff_t n=flxtr_pipe_read(handle,bytes,size);
        if(n>=0||errno!=EAGAIN)return n;
        wait_a_bit();
    }
    assert(!"pipe read timed out");return -1;
}
#ifdef _WIN32
static void verify_private_file(const char *path){
    wchar_t *wide=flxtr_process_wide(path);assert(wide);
    PACL acl=NULL;PSECURITY_DESCRIPTOR descriptor=NULL;
    assert(GetNamedSecurityInfoW(wide,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
           NULL,NULL,&acl,NULL,&descriptor)==ERROR_SUCCESS);free(wide);
    assert(acl&&acl->AceCount==1);
    ACCESS_ALLOWED_ACE *ace=NULL;assert(GetAce(acl,0,(void **)&ace));
    assert(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE);
    HANDLE token;assert(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));
    DWORD bytes=0;GetTokenInformation(token,TokenUser,NULL,0,&bytes);
    PTOKEN_USER user=(PTOKEN_USER)malloc(bytes);assert(user);
    assert(GetTokenInformation(token,TokenUser,user,bytes,&bytes));
    assert(EqualSid(&ace->SidStart,user->User.Sid));
    free(user);CloseHandle(token);LocalFree(descriptor);
}
#endif
int main(int argc,char **argv){
    if(argc==5&&!strcmp(argv[1],"--pipe-child")){
        FlxtrPipeHandle controls,gate,reply;
        assert(!flxtr_pipe_parse(argv[2],&controls)&&!flxtr_pipe_parse(argv[3],&gate)&&!flxtr_pipe_parse(argv[4],&reply));
        char go=0;assert(read_eventually(gate,&go,1)==1&&go=='G');flxtr_pipe_close(gate);
        char command[4];size_t used=0;
        while(used<sizeof(command)){
            ptrdiff_t count=read_eventually(controls,command+used,sizeof(command)-used);
            assert(count>0);used+=(size_t)count;
        }
        assert(!memcmp(command,"mPFR",4));
        assert(read_eventually(controls,&go,1)==0);flxtr_pipe_close(controls);
        assert(flxtr_pipe_write(reply,command,4)==4);flxtr_pipe_close(reply);return 0;
    }
#ifdef _WIN32
    puts("FlXtR Windows step 2: pipes and paths (not the full application)");
    DWORD handles_before,handles_after;
    wchar_t wide[32768];assert(GetModuleFileNameW(NULL,wide,32768));
    char executable[131072];assert(!flxtr_path_utf8(wide,executable,sizeof(executable)));
#else
    puts("FlXtR POSIX step 2: pipes and paths");
    const char *executable=argv[0];
#endif
    FlxtrPipe controls,gate,reply;
    assert(!flxtr_pipe_create(&controls)&&!flxtr_pipe_create(&gate)&&!flxtr_pipe_create(&reply));
#ifdef _WIN32
    DWORD flags;assert(GetHandleInformation((HANDLE)controls.reader,&flags)&&!(flags&HANDLE_FLAG_INHERIT));
    assert(GetHandleInformation((HANDLE)controls.writer,&flags)&&!(flags&HANDLE_FLAG_INHERIT));
#else
    assert(fcntl((int)controls.reader,F_GETFD)&FD_CLOEXEC);
    assert(fcntl((int)controls.writer,F_GETFD)&FD_CLOEXEC);
#endif
    FlxtrPipeHandle exported[3]={flxtr_pipe_export(controls.reader),flxtr_pipe_export(gate.reader),flxtr_pipe_export(reply.writer)};
    for(int i=0;i<3;i++)assert(exported[i]!=FLXTR_INVALID_PIPE);
    char numbers[3][32];uintptr_t handles[3];
    for(int i=0;i<3;i++){handles[i]=(uintptr_t)exported[i];snprintf(numbers[i],sizeof(numbers[i]),"%" PRIuPTR,handles[i]);}
    const char *args[]={executable,"--pipe-child",numbers[0],numbers[1],numbers[2],NULL};
    FlxtrSpawnOptions options={.pass_handles=handles,.pass_count=3};FlxtrProcess child={0};
    assert(!flxtr_process_start(&child,executable,args,&options));
    for(int i=0;i<3;i++)flxtr_pipe_close(exported[i]);
    flxtr_pipe_close(controls.reader);flxtr_pipe_close(gate.reader);flxtr_pipe_close(reply.writer);
    assert(flxtr_process_wait(&child,30,NULL)==0);
    char output[4];assert(flxtr_pipe_read(reply.reader,output,sizeof(output))<0&&errno==EAGAIN);
    assert(flxtr_pipe_write(controls.writer,"mPFR",4)==4);flxtr_pipe_close(controls.writer);
    assert(flxtr_pipe_write(gate.writer,"G",1)==1);flxtr_pipe_close(gate.writer);
    int code;assert(flxtr_process_wait(&child,10000,&code)==1&&code==0);
    assert(read_eventually(reply.reader,output,sizeof(output))==4&&!memcmp(output,"mPFR",4));
    assert(read_eventually(reply.reader,output,sizeof(output))==0);flxtr_pipe_close(reply.reader);
    puts("PASS: explicit inherited endpoints, preload gate, control bytes and peer EOF");
    FlxtrPipe pressure;assert(!flxtr_pipe_create(&pressure));char block[512]={0};
    size_t written=0;ptrdiff_t count;
    while((count=flxtr_pipe_write(pressure.writer,block,sizeof(block)))>0){
        written+=(size_t)count;assert(written<1024*1024);
    }
    assert(written>0&&errno==EAGAIN);
    size_t drained=0;
    while((count=flxtr_pipe_read(pressure.reader,block,sizeof(block)))>0)drained+=(size_t)count;
    assert(drained==written&&errno==EAGAIN);
    flxtr_pipe_close(pressure.reader);
    assert(flxtr_pipe_write(pressure.writer,"x",1)<0&&errno==EPIPE);flxtr_pipe_close(pressure.writer);
    FlxtrPipeHandle invalid;assert(flxtr_pipe_parse("-1",&invalid)<0);
    assert(flxtr_pipe_parse(" 4",&invalid)<0&&flxtr_pipe_parse("4junk",&invalid)<0);
    assert(flxtr_pipe_parse("18446744073709551616",&invalid)<0);
    puts("PASS: bounded nonblocking backpressure, broken peer and strict handle parsing");
    char path[256]="platform-\xc3\xb8-\xe9\x9b\xaa-XXXXXX";
    const char destination[]="installed-\xc3\xb8-\xe9\x9b\xaa.bin";
    int fd=flxtr_temp_file(path,sizeof(path));assert(fd>=0);
    const unsigned char bytes[]={0,'A','\r','\n',255,26};
    assert(flxtr_fd_write(fd,bytes,sizeof(bytes))==(ptrdiff_t)sizeof(bytes));
    assert(!flxtr_fd_sync(fd)&&!flxtr_fd_close(fd));
#ifdef _WIN32
    verify_private_file(path);
#else
    struct stat st;assert(!stat(path,&st)&&(st.st_mode&0777)==0600);
#endif
    assert(!flxtr_install_new(path,destination));
    unsigned char actual[sizeof(bytes)];FILE *file=flxtr_fopen(destination,"rb");assert(file);
    assert(fread(actual,1,sizeof(actual),file)==sizeof(actual));fclose(file);assert(!memcmp(bytes,actual,sizeof(bytes)));
    strcpy(path,"platform-again-XXXXXX");fd=flxtr_temp_file(path,sizeof(path));assert(fd>=0);
    assert(!flxtr_fd_close(fd));assert(flxtr_install_new(path,destination)<0&&errno==EEXIST);
    assert(!flxtr_path_access(path,0));assert(!flxtr_path_unlink(path)&&!flxtr_path_unlink(destination));
    uint64_t available;assert(!flxtr_disk_free(".",&available)&&available>0);
    char data[2048];
#ifdef _WIN32
    assert(!_wputenv_s(L"LOCALAPPDATA",L"C:\\Users\\Name \x00f8"));
    assert(!flxtr_data_directory(data,sizeof(data))&&!strcmp(data,"C:\\Users\\Name \xc3\xb8\\FlXtR"));
#else
    assert(!setenv("XDG_DATA_HOME","/tmp/name \xc3\xb8",1));
    assert(!flxtr_data_directory(data,sizeof(data))&&!strcmp(data,"/tmp/name \xc3\xb8/flxtr"));
#endif
    assert(flxtr_data_directory(data,2)<0&&errno==ENAMETOOLONG);
    assert(flxtr_reserved_basename("CON.mp4")&&flxtr_reserved_basename("nul.srt")&&flxtr_reserved_basename("COM9.extra.mp4"));
    assert(!flxtr_reserved_basename("console.mp4")&&!flxtr_reserved_basename("COM10.mp4"));
    puts("PASS: Unicode paths, private binary temp files, no-overwrite uploads and native data paths");
#ifdef _WIN32
    /* Windows crypto/security providers retain process-wide handles on first
     * use. Check repeated operations after those providers have initialized. */
    assert(GetProcessHandleCount(GetCurrentProcess(),&handles_before));
    for(int i=0;i<32;i++){
        FlxtrPipe again;assert(!flxtr_pipe_create(&again));
        FlxtrPipeHandle copy=flxtr_pipe_export(again.reader);assert(copy!=FLXTR_INVALID_PIPE);
        flxtr_pipe_close(copy);flxtr_pipe_close(again.reader);flxtr_pipe_close(again.writer);
        strcpy(path,"repeat-private-XXXXXX");fd=flxtr_temp_file(path,sizeof(path));assert(fd>=0);
        assert(!flxtr_fd_close(fd));verify_private_file(path);assert(!flxtr_path_unlink(path));
    }
    assert(GetProcessHandleCount(GetCurrentProcess(),&handles_after)&&handles_before==handles_after);
    puts("PASS: repeated pipe/private-file operations release owned Windows handles");
#endif
    return 0;
}
