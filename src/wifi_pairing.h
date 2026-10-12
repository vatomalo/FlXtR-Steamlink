#ifndef FLXTR_WIFI_PAIRING_H
#define FLXTR_WIFI_PAIRING_H
#include <poll.h>
#include <signal.h>
/* Read only the active ConnMan Wi-Fi service. Credentials never leave the TV. */
static int wifi_escape(char *out,size_t size,const char *input){
    size_t n=0;
    for(const char *p=input;*p;p++){
        if(n+3>=size)return -1;
        if(strchr("\\;,:\"",*p))out[n++]='\\';
        out[n++]=*p;
    }out[n]=0;return 0;
}
static int wifi_ini_decode(char *value){
    char *out=value;
    for(char *p=value;*p;p++){
        if(*p=='\\'){
            p++;if(!*p)return -1;
            *out++=*p=='s'?' ':*p=='n'?'\n':*p=='r'?'\r':*p=='t'?'\t':*p;
        }else *out++=*p;
    }*out=0;return 0;
}
static int wifi_payload(char *payload,size_t size){
    int pipes[2];if(pipe(pipes))return -1;
    pid_t child=shell_fork();
    if(child<0){close(pipes[0]);close(pipes[1]);return -1;}
    if(!child){
        dup2(pipes[1],STDOUT_FILENO);close(pipes[0]);close(pipes[1]);
        int quiet=open("/dev/null",O_WRONLY);if(quiet>=0){dup2(quiet,STDERR_FILENO);close(quiet);}
        execlp("connmanctl","connmanctl","services",(char*)NULL);_exit(127);
    }
    close(pipes[1]);char listing[4096]={0},line[512],service[180]="";size_t used=0;
    for(int tries=0;tries<20&&used<sizeof(listing)-1;tries++){
        struct pollfd fd={pipes[0],POLLIN,0};int ready=poll(&fd,1,100);
        if(ready<0)break;
        if(ready&&fd.revents&POLLIN){ssize_t n=read(pipes[0],listing+used,sizeof(listing)-1-used);if(n<=0)break;used+=(size_t)n;}
        else if(ready)break;
    }
    close(pipes[0]);if(waitpid(child,NULL,WNOHANG)==0){kill(child,SIGKILL);waitpid(child,NULL,0);}
    char *save=NULL;
    for(char *row=strtok_r(listing,"\n",&save);row;row=strtok_r(NULL,"\n",&save)){
        snprintf(line,sizeof(line),"%.511s",row);
        if(strncmp(line,"*AO",3)&&strncmp(line,"*AR",3))continue;
        char *id=strstr(line,"wifi_");if(!id)continue;id[strcspn(id," \t\r\n")]=0;
        size_t n=strlen(id);int valid=n>0&&n<sizeof(service);
        for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)id[i])&&id[i]!='_')valid=0;
        if(valid)snprintf(service,sizeof(service),"%s",id);
    }
    if(!service[0])return -1;
    int open_wifi=strstr(service,"_managed_none")!=NULL;
    if(!open_wifi&&!strstr(service,"_managed_psk"))return -1;
    char path[256];snprintf(path,sizeof(path),"/var/lib/connman/%s/settings",service);
    FILE *f=fopen(path,"r");if(!f)return -1;
    char ssid[129]="",password[257]="";
    while(fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"Name=",5))snprintf(ssid,sizeof(ssid),"%.128s",line+5);
        if(!strncmp(line,"Passphrase=",11))snprintf(password,sizeof(password),"%.256s",line+11);
    }fclose(f);
    int ok=ssid[0]&&(open_wifi||password[0])&&!wifi_ini_decode(ssid)&&!wifi_ini_decode(password);
    char escaped_ssid[260],escaped_password[520];
    if(ok)ok=!wifi_escape(escaped_ssid,sizeof(escaped_ssid),ssid)&&!wifi_escape(escaped_password,sizeof(escaped_password),password);
    if(ok){int n=snprintf(payload,size,"WIFI:T:%s;S:%s;P:%s;;",open_wifi?"nopass":"WPA",escaped_ssid,escaped_password);ok=n>0&&(size_t)n<size;}
    memset(password,0,sizeof(password));memset(escaped_password,0,sizeof(escaped_password));memset(line,0,sizeof(line));
    return ok?0:-1;
}
#endif
