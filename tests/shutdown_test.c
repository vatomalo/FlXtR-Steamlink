#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    assert(SDL_Init(SDL_INIT_TIMER)==0);
    /* Graceful children must get TERM; wedged children must not block launch. */
    int ready[2],closed[2];assert(!pipe(ready)&&!pipe(closed));
    pid_t child=fork();assert(child>=0);
    if(!child){
        close(ready[0]);close(closed[0]);
        sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGTERM);
        sigprocmask(SIG_BLOCK,&mask,NULL);assert(write(ready[1],"R",1)==1);
        int sig;sigwait(&mask,&sig);assert(write(closed[1],"C",1)==1);_exit(0);
    }
    close(ready[1]);close(closed[1]);char byte;assert(read(ready[0],&byte,1)==1);
    pid_t children[]={child};shutdown_children(children,1);
    assert(children[0]==0&&read(closed[0],&byte,1)==1&&byte=='C');
    close(ready[0]);close(closed[0]);
    assert(!pipe(ready));child=fork();assert(child>=0);
    if(!child){close(ready[0]);signal(SIGTERM,SIG_IGN);assert(write(ready[1],"R",1)==1);for(;;)pause();}
    close(ready[1]);assert(read(ready[0],&byte,1)==1);close(ready[0]);
    children[0]=child;Uint32 started=SDL_GetTicks();shutdown_children(children,1);
    assert(!children[0]&&SDL_GetTicks()-started<4000);
    SDL_Quit();puts("PASS: graceful hardware cleanup and bounded forced exit");return 0;
}
