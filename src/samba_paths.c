/* Relocate exported Samba dynamic paths within the isolated musl runtime.
 * No firmware libc dependency or system-library modifications. */
extern char *getenv(const char *);
extern void *dlsym(void *,const char *);
extern int snprintf(char *,__SIZE_TYPE__,const char *,...);
extern long write(int,const void *,__SIZE_TYPE__);
extern void _exit(int);
__attribute__((constructor)) static void samba_paths(void){
    const char *root=getenv("FLXTR_SAMBA_RUNTIME"),*helpers=getenv("FLXTR_SAMBA_HELPERS");
    if(!root||root[0]!='/'||!helpers||helpers[0]!='/')_exit(126);
    const char *symbols[]={"set_dyn_MODULESDIR","set_dyn_LIBDIR","set_dyn_CODEPAGEDIR",
        "set_dyn_SAMBA_LIBEXECDIR","set_dyn_SBINDIR","set_dyn_BINDIR"};
    const char *suffixes[]={"/usr/lib/samba","/usr/lib","/usr/share/samba","","",""};
    for(unsigned i=0;i<6;i++){
        char path[4096];const char *base=i>=3?helpers:root;
        int n=snprintf(path,sizeof(path),"%s%s",base,suffixes[i]);
        const char *(*set)(const char *)=(const char *(*)(const char *))dlsym((void *)0,symbols[i]);
        if(n<1||n>=(int)sizeof(path)||!set||!set(path)){
            const char msg[]="Samba runtime path relocation failed\n";
            write(2,msg,sizeof(msg)-1);_exit(126);
        }
    }
}

