#ifndef FLXTR_SETTINGS_TABS_H
#define FLXTR_SETTINGS_TABS_H
enum { SETTINGS_MOVIES,SETTINGS_SERIES,SETTINGS_MUSIC,SETTINGS_ROMS,
       SETTINGS_TV,SETTINGS_SERVICES,SETTINGS_GENERAL,SETTINGS_TABS };
typedef struct {const char *name;int count,ids[8];} SettingsTab;
static const SettingsTab settings_tabs[]={
    {"MOVIES",6,{0,1,2,4,5,6}},
    {"SERIES",5,{17,0,4,5,6}},
    {"MUSIC",2,{20,21}},
    {"ROMS",4,{22,19,10,11}},
    {"TV",5,{7,8,12,13,14}},
    {"SERVICES",5,{23,24,25,15,18}},
    {"GENERAL",3,{3,9,16}}
};
static int settings_tab,settings_row=-1;
static void settings_switch(int step){
    settings_tab=(settings_tab+step+SETTINGS_TABS)%SETTINGS_TABS;settings_row=-1;
}
static int settings_id(void){
    return settings_row>=0&&settings_row<settings_tabs[settings_tab].count?
        settings_tabs[settings_tab].ids[settings_row]:-1;
}
#endif
