#ifndef FLXTR_SETTINGS_SERVICES_H
#define FLXTR_SETTINGS_SERVICES_H
static int samba_password_on,samba_password_key,samba_password_lower;
static char samba_password[65],samba_message[161]="DOWNLOAD / SET PASSWORD / ENABLE LOCAL SHARES";
static int samba_installed(void){return !access("samba-runtime/usr/sbin/smbd",X_OK)&&!access("samba-runtime/flxtr-samba-paths.so",R_OK);}
static void samba_password_close(void){
    memset(samba_password,0,sizeof(samba_password));samba_password_on=0;SDL_StopTextInput();
}
static void samba_service(const char *operation){
#if defined(FLXTR_DESKTOP) || defined(_WIN32)
    (void)operation;snprintf(samba_message,sizeof(samba_message),"SAMBA ADD-ON IS FOR STEAM LINK");
#else
    if(samba_pid)return;
    unlink("samba-install-status.txt");
    if(!strcmp(operation,"install")){
        const char *args[]={"sh","scripts/music-runtime.sh","tools/samba_install.py",NULL};
        unlink("samba-install-status.txt");
        samba_pid=shell_spawn("/bin/sh",args,"samba-service.log","samba-service.log");
    }else if(!strcmp(operation,"configure")){
        /* Password travels through an anonymous pipe, never argv or disk. Fill
         * before fork: the bounded write cannot race an exiting reader. */
        int input[2];if(pipe(input)){snprintf(samba_message,sizeof(samba_message),"PASSWORD PIPE FAILED");return;}
        char secret[132];int n=snprintf(secret,sizeof(secret),"%s\n%s\n",samba_password,samba_password);
        ssize_t written=write(input[1],secret,(size_t)n);memset(secret,0,sizeof(secret));close(input[1]);
        if(written!=n){close(input[0]);return;}
        samba_pid=shell_fork();
        if(!samba_pid){
            setpgid(0,0);
            int log=open("samba-service.log",O_CREAT|O_WRONLY|O_TRUNC,0600);
            if(log<0||dup2(log,1)<0||dup2(log,2)<0||dup2(input[0],0)<0)_exit(126);
            if(log>2)close(log);
            if(input[0]>2)close(input[0]);
            execl("/bin/sh","sh","-c","sh ./samba-server.sh disable && sh ./samba-server.sh configure && sh ./samba-server.sh enable",(char*)NULL);
            _exit(127);
        }
        close(input[0]);
    }else{
        const char *args[]={"sh","./samba-server.sh",operation,NULL};
        samba_pid=shell_spawn("/bin/sh",args,"samba-service.log","samba-service.log");
    }
    if(samba_pid<0){samba_pid=0;snprintf(samba_message,sizeof(samba_message),"COULD NOT START SAMBA WORKER");return;}
    snprintf(samba_message,sizeof(samba_message),!strcmp(operation,"install")?"DOWNLOADING SAMBA...":"UPDATING SAMBA SERVICE...");
#endif
}
static void samba_password_open(void){
#if defined(FLXTR_DESKTOP) || defined(_WIN32)
    snprintf(samba_message,sizeof(samba_message),"SAMBA ADD-ON IS FOR STEAM LINK");
#else
    if(samba_pid)return;
    if(!samba_installed()){snprintf(samba_message,sizeof(samba_message),"DOWNLOAD SAMBA FIRST");return;}
    samba_password_on=1;samba_password_key=samba_password_lower=0;
    memset(samba_password,0,sizeof(samba_password));SDL_StartTextInput();
#endif
}
static void samba_password_text(const char *value){
    size_t n=strlen(samba_password);
    for(;*value&&n<64;value++)if((unsigned char)*value>=32&&(unsigned char)*value<=126)samba_password[n++]=*value;
    samba_password[n]=0;
}
static void samba_password_action(SDL_Keycode key){
    size_t n=strlen(samba_password);
    if(key==SDLK_ESCAPE){samba_password_close();return;}
    if(key==SDLK_BACKSPACE){if(n)samba_password[n-1]=0;return;}
    if(key==SDLK_F2){samba_password_lower=!samba_password_lower;return;}
    if(key==SDLK_RIGHT&&samba_password_key<39)samba_password_key++;
    if(key==SDLK_LEFT&&samba_password_key>0)samba_password_key--;
    if(key==SDLK_DOWN&&samba_password_key<30)samba_password_key+=10;
    if(key==SDLK_UP&&samba_password_key>=10)samba_password_key-=10;
    if(key==SDLK_F4){
        char c=search_keys[samba_password_key];
        if(c=='<'){if(n)samba_password[n-1]=0;}
        else if(c=='>')key=SDLK_RETURN;
        else{char value[2]={(char)(samba_password_lower?tolower((unsigned char)c):c),0};samba_password_text(value);}
    }
    if(key==SDLK_RETURN||key==SDLK_F3){
        if(strlen(samba_password)<8){snprintf(samba_message,sizeof(samba_message),"USE AT LEAST 8 CHARACTERS");return;}
        samba_service("configure");samba_password_close();
    }
}
static int samba_poll(void){
    if(!samba_pid)return 0;
    int result;pid_t done=waitpid(samba_pid,&result,WNOHANG);
    if(done==samba_pid){
        samba_pid=0;
        const char *message="SAMBA FAILED / SEE SAMBA-SERVICE.LOG";
        if(WIFEXITED(result)&&!WEXITSTATUS(result))
            message=!access("samba/.enabled",R_OK)?"SAMBA ON / ROMS AND MOVIES SHARES":
                "SAMBA INSTALLED / SET PASSWORD OR ENABLE SHARING";
        snprintf(samba_message,sizeof(samba_message),"%s",message);
        return 1;
    }
    static Uint32 refresh;Uint32 now=SDL_GetTicks();
    if((Uint32)(now-refresh)<500)return 0;
    refresh=now;
    FILE *f=fopen("samba-install-status.txt","r");
    if(f){if(!fgets(samba_message,sizeof(samba_message),f))samba_message[0]=0;fclose(f);samba_message[strcspn(samba_message,"\r\n")]=0;}
    return 1;
}
#endif
