/* Data-only custom themes; no scripts, downloads or external paths. */
#ifndef FLXTR_DESKTOP_THEME_H
#define FLXTR_DESKTOP_THEME_H
static const char *const menu_theme_names[MENU_THEMES]={"WHITE METAL","CLASSIC MONOCHROME","CRIMSON SKULLS","MIDNIGHT BLUE","AMBER CRT","CUSTOM"};
static SDL_Color theme_canvas={0,0,0,255},theme_panel={255,255,255,255};
static SDL_Color theme_pane(Uint8 alpha){SDL_Color c=theme_panel;c.a=alpha;return c;}
static int theme_rgb(const char *s,SDL_Color *out){
    if(strlen(s)!=7||s[0]!='#')return -1;
    for(int i=1;i<7;i++)if(!isxdigit((unsigned char)s[i]))return -1;
    unsigned value=(unsigned)strtoul(s+1,NULL,16);
    *out=(SDL_Color){(value>>16)&255,(value>>8)&255,value&255,255};return 0;
}
static void theme_custom(void){
    struct stat st;
    if(stat("themes/custom/theme.ini",&st)||st.st_size>8192)return;
    FILE *f=fopen("themes/custom/theme.ini","r");if(!f)return;
    char line[128];int rows=0;
    while(rows++<64&&fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;char *v=strchr(line,'=');
        if(!v)continue;
        *v++=0;
        SDL_Color *color=!strcmp(line,"accent")?&green:!strcmp(line,"text")?&white:!strcmp(line,"muted")?&dim:!strcmp(line,"canvas")?&theme_canvas:!strcmp(line,"panel")?&theme_panel:NULL;
        if(color)(void)theme_rgb(v,color);
    }fclose(f);
}
static void theme_create_custom(void){
    mkdir("themes",0700);mkdir("themes/custom",0700);
    if(!access("themes/custom/theme.ini",F_OK))return;
    FILE *f=fopen("themes/custom/theme.ini","wx");
    if(f){fputs("# FlXtR custom theme / RGB hex colors\naccent=#65C9FF\ntext=#E7EDF5\nmuted=#8B9BAF\ncanvas=#09111F\npanel=#111E33\n",f);fclose(f);}
}
#endif
