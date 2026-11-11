/* Six top-level libraries, with three lazy tabs in WATCH and FILES.
 * Switching the tab highlight does no catalogue I/O; A loads the chosen tab. */
#ifndef FLXTR_GROUPED_LIBRARY_TABS_H
#define FLXTR_GROUPED_LIBRARY_TABS_H
enum { LIB_GROUP_RECENTS,LIB_GROUP_TV,LIB_GROUP_FAVORITES,
       LIB_GROUP_WATCH,LIB_GROUP_GAMES,LIB_GROUP_FILES,LIB_GROUP_COUNT };
static const int library_group_defaults[LIB_GROUP_COUNT]={
    MEDIA_RECENTS_MODE,11,FAVORITES_MODE,1,21,30
};
static const int library_watch_modes[3]={1,2,7};
static const int library_files_modes[3]={30,OFFLINE_MODE,0};
static const char *const library_watch_labels[3]={"MOVIES","SERIES","ANIME"};
static const char *const library_files_labels[3]={"ARCHIVE","DOWNLOADS","LOCAL"};
static int library_watch_tab,library_files_tab;
static int library_tabs_focused,library_tab_cursor;
static int library_group_for_mode(int mode){
    if(mode==MEDIA_RECENTS_MODE)return LIB_GROUP_RECENTS;
    if(mode==11||mode==12||mode==13)return LIB_GROUP_TV;
    if(mode==FAVORITES_MODE)return LIB_GROUP_FAVORITES;
    if(mode==1||mode==2||mode==7||mode==3||mode==4||mode==5||
       mode==6||mode==8||mode==34)return LIB_GROUP_WATCH;
    if(mode==0||mode==30||mode==OFFLINE_MODE||mode==9||mode==10||mode==31)
        return LIB_GROUP_FILES;
    return LIB_GROUP_GAMES;
}
static int library_current_group(void){
    return library_group_for_mode(history_size?history[0].mode:browse.mode);
}
static int library_group_tab_mode(int group,int tab){
    if(tab<0||tab>2)return -1;
    return group==LIB_GROUP_WATCH?library_watch_modes[tab]:
           group==LIB_GROUP_FILES?library_files_modes[tab]:-1;
}
static int library_group_tab_index(int group,int mode){
    for(int i=0;i<3;i++)if(library_group_tab_mode(group,i)==mode)return i;
    return -1;
}
static int library_current_tab(void){
    int group=library_current_group();
    int index=library_group_tab_index(group,browse.mode);
    return index>=0?index:group==LIB_GROUP_WATCH?library_watch_tab:library_files_tab;
}
static int library_can_focus_tabs(void){
    int group=library_current_group();
    return !history_size&&(group==LIB_GROUP_WATCH||group==LIB_GROUP_FILES)&&
        library_group_tab_index(group,browse.mode)>=0;
}
static int library_group_entry_mode(int group){
    if(group==LIB_GROUP_WATCH)return library_watch_modes[library_watch_tab];
    if(group==LIB_GROUP_FILES)return library_files_modes[library_files_tab];
    return library_group_defaults[group];
}
static void library_tab_remember(int mode){
    int group=library_group_for_mode(mode);
    int tab=library_group_tab_index(group,mode);
    if(tab<0)return;
    if(group==LIB_GROUP_WATCH)library_watch_tab=tab;
    if(group==LIB_GROUP_FILES)library_files_tab=tab;
}
#endif
