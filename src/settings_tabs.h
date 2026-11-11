#ifndef FLXTR_SETTINGS_TABS_H
#define FLXTR_SETTINGS_TABS_H
enum { SETTINGS_VIDEO,SETTINGS_MUSIC,SETTINGS_ROMS,
       SETTINGS_TV,SETTINGS_SERVICES,SETTINGS_GENERAL,SETTINGS_STREAMING,
#if defined(FLXTR_DESKTOP) || defined(_WIN32) || defined(__ANDROID__)
       SETTINGS_TABS=SETTINGS_STREAMING };
#else
       SETTINGS_TABS };
#endif
typedef struct {const char *name;int count,ids[8];} SettingsTab;
static const SettingsTab settings_tabs[]={
    {"VIDEO",8,{0,1,2,4,5,6,26,17}},
    {"MUSIC",5,{20,21,29,31,32}},
    {"ROMS",6,{22,19,28,10,11,27}},
    {"TV",5,{7,8,12,13,14}},
    {"SERVICES",8,{23,24,25,15,18,30,33,34}},
    {"GENERAL",7,{3,9,16,40,41,42,35}},
    {"STREAM",4,{36,37,38,39}}
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
