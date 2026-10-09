/* Resolver sidecar is bound to the exact media URL, so stale results cannot leak. */
static int subtitle_language_match(const char *wanted,const char *label){
    if(!strcmp(wanted,"auto"))return 1;
    char lower[96];size_t n=0;
    for(;label[n]&&n+1<sizeof(lower);n++)lower[n]=(char)tolower((unsigned char)label[n]);
    lower[n]=0;
    if(!strcmp(wanted,"eng"))return !strcmp(lower,"eng")||!strcmp(lower,"en")||!strncmp(lower,"en-",3)||strstr(lower,"english")!=NULL;
    if(!strcmp(wanted,"nor"))return !strcmp(lower,"nor")||!strcmp(lower,"no")||!strcmp(lower,"nb")||!strcmp(lower,"nn")||!strcmp(lower,"nob")||strstr(lower,"norwegian")!=NULL;
    return !strcmp(wanted,lower);
}
static int subtitle_source(const char *media,const char *language,char *out,size_t cap){
    out[0]=0;if(!strcmp(language,"off"))return 0;
    FILE *f=fopen("playback-subtitles.tsv","r");if(!f)return 0;
    char line[8192];
    if(!fgets(line,sizeof(line),f)){fclose(f);return 0;}
    line[strcspn(line,"\r\n")]=0;
    if(strcmp(line,media)){fclose(f);return 0;}
    for(int i=0;i<64&&fgets(line,sizeof(line),f);i++){
        line[strcspn(line,"\r\n")]=0;char *tab=strchr(line,'\t');if(!tab)continue;*tab++=0;
        if(strncmp(tab,"https://",8)&&strncmp(tab,"http://",7))continue;
        if(strpbrk(tab,"\r\n\t ")||strlen(tab)>=cap||!subtitle_language_match(language,line))continue;
        strcpy(out,tab);break;
    }
    fclose(f);return *out!=0;
}
