#ifndef FLXTR_ROM_CHEAT_RUNTIME_H
#define FLXTR_ROM_CHEAT_RUNTIME_H
/* RetroArch 1.7.6 auto-loads <cheat_database>/<library_name>/<ROM stem>.cht.
 * Ask the selected core for its real library name, without initializing it.
 * Each ROM has a private database root so identical filenames cannot collide. */
static int game_prepare_cheats(const char *core,const char *rom,const char *key,char *database,size_t cap){
    char source[96];snprintf(source,sizeof(source),"cheats/%s.cht",key);
    FILE *in=fopen(source,"rb");if(!in)return 0;
    void *library=SDL_LoadObject(core);if(!library){fclose(in);return -1;}
    void (*info_fn)(struct retro_system_info*)=(void (*)(struct retro_system_info*))SDL_LoadFunction(library,"retro_get_system_info");
    struct retro_system_info info={0};if(info_fn)info_fn(&info);
    char name[128]={0};
    if(info.library_name&&strlen(info.library_name)<sizeof(name)&&
       !strpbrk(info.library_name,"/\\\"\r\n")&&strcmp(info.library_name,".")&&strcmp(info.library_name,".."))
        snprintf(name,sizeof(name),"%s",info.library_name);
    SDL_UnloadObject(library);
    if(!name[0]){fclose(in);return -1;}
    char cwd[PATH_MAX],dir[PATH_MAX],target[PATH_MAX],stem[256];
    const char *file=strrchr(rom,'/');file=file?file+1:rom;
#ifdef _WIN32
    const char *backslash=strrchr(file,'\\');if(backslash)file=backslash+1;
#endif
    if(!getcwd(cwd,sizeof(cwd))||strlen(file)>=sizeof(stem)||strpbrk(cwd,"\"\r\n")){fclose(in);return -1;}
    snprintf(stem,sizeof(stem),"%s",file);char *dot=strrchr(stem,'.');if(dot)*dot=0;
    mkdir("cheats",0700);mkdir("cheats/runtime",0700);
    snprintf(dir,sizeof(dir),"cheats/runtime/%s",key);mkdir(dir,0700);
    int n=snprintf(database,cap,"%s/%s",cwd,dir);
    if(n<0||(size_t)n>=cap){fclose(in);return -1;}
    n=snprintf(dir,sizeof(dir),"cheats/runtime/%s/%s",key,name);
    if(n<0||(size_t)n>=sizeof(dir)||(mkdir(dir,0700)&&errno!=EEXIST)){fclose(in);return -1;}
    n=snprintf(target,sizeof(target),"%s/%s.cht.next",dir,stem);
    if(n<0||(size_t)n>=sizeof(target)){fclose(in);return -1;}
    FILE *out=fopen(target,"wb");if(!out){fclose(in);return -1;}
    char buf[4096];size_t count;int good=1;
    while((count=fread(buf,1,sizeof(buf),in)))if(fwrite(buf,1,count,out)!=count){good=0;break;}
    if(ferror(in))good=0;
    fclose(in);if(fclose(out))good=0;
    char final[PATH_MAX];snprintf(final,sizeof(final),"%s",target);final[strlen(final)-5]=0;
    if(!good||rename(target,final)){unlink(target);return -1;}
    return 1;
}
#endif
