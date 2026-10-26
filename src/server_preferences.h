#ifndef FLXTR_SERVER_PREFERENCES_H
#define FLXTR_SERVER_PREFERENCES_H
#include <stdio.h>
#include <string.h>
#include <ctype.h>
static int server_group(const char *kind){
    return !strcmp(kind,"movie")||!strcmp(kind,"episode")?0:!strcmp(kind,"kiss-episode")?1:-1;
}
static int server_name_valid(const char *name){
    size_t n=strlen(name);if(!n||n>=32)return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)name[i])&&name[i]!='_'&&name[i]!='-')return 0;
    return 1;
}
static void server_preferences_read(char names[2][32]){
    memset(names,0,2*32);FILE *f=fopen("server-preferences.cfg","r");if(!f)return;
    char line[128],key[32],name[32],extra;
    while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"%31[^=]=%31s %c",key,name,&extra)!=2||!server_name_valid(name))continue;
        int group=!strcmp(key,"flixer")?0:!strcmp(key,"kiss")?1:-1;
        if(group>=0)strcpy(names[group],name);
    }fclose(f);
}
static void server_preference_read(const char *kind,char name[32]){
    char names[2][32];server_preferences_read(names);int group=server_group(kind);
    name[0]=0;if(group>=0)strcpy(name,names[group]);
}
static int server_preference_save(const char *kind,const char *name){
    int group=server_group(kind);if(group<0||!server_name_valid(name))return 0;
    char names[2][32];server_preferences_read(names);
    if(!strcmp(names[group],name))return 1;
    strcpy(names[group],name);FILE *f=fopen("server-preferences.cfg.next","w");if(!f)return 0;
    int ok=fprintf(f,"flixer=%s\nkiss=%s\n",names[0],names[1])>=0;
    if(fclose(f))ok=0;
    return ok&&rename("server-preferences.cfg.next","server-preferences.cfg")==0;
}
#endif
