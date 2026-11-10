#ifndef FLXTR_ROM_STATE_RUNTIME_H
#define FLXTR_ROM_STATE_RUNTIME_H
static int game_prepare_state_shadows(const char *rom,const char *key,const char *core){
    json_object *session=json_object_new_object();if(!session)return -1;
    json_object_object_add(session,"rom",json_object_new_string(rom));
    json_object_object_add(session,"key",json_object_new_string(key));
    json_object_object_add(session,"core",json_object_new_string(core));
    json_object_object_add(session,"home",json_object_new_string(games_home()));
    int rc=json_object_to_file("state-session.json.next",session);json_object_put(session);
    if(rc||rename("state-session.json.next","state-session.json"))return -1;
    /* Older packages/test fixtures may not yet contain the bundled helper. */
    if(access("tools/state_guard.py",R_OK)||access("scripts/music-runtime.sh",R_OK))return 0;
#ifdef _WIN32
    char python[PATH_MAX];snprintf(python,sizeof(python),"%s/music-runtime/bin/python.exe",flxtr_win_binary_dir());
    const char *args[]={"python","-I","tools/state_guard.py","prepare",NULL};
    pid_t child=flxtr_win_spawn(python,args,"state-guard-prepare.log","state-guard-prepare.log",NULL,0,1);
#else
    pid_t child=fork();
    if(child==0){
        int fd=open("state-guard-prepare.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(fd>=0){dup2(fd,1);dup2(fd,2);close(fd);}
        execl("/bin/sh","sh","scripts/music-runtime.sh","tools/state_guard.py","prepare",(char*)NULL);_exit(127);
    }
#endif
    if(child<=0)return -1;
    int code;return waitpid(child,&code,0)==child&&WIFEXITED(code)&&!WEXITSTATUS(code)?0:-1;
}
#endif
