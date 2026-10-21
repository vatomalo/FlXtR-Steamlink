#define FLXTR_DESKTOP
#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char temp[]="/tmp/flxtr-settings-tabs-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    settings_on=1;settings_tab=SETTINGS_MOVIES;settings_row=-1;
    action(SDLK_RIGHT);assert(settings_tab==SETTINGS_SERIES&&settings_row==-1);
    action(SDLK_DOWN);assert(settings_id()==17);
    int previous=series_autoplay;action(SDLK_RETURN);assert(series_autoplay!=previous);
    action(SDLK_UP);assert(settings_row==-1);
    action(SDLK_TAB);assert(settings_tab==SETTINGS_MUSIC);
    action(SDLK_DOWN);assert(settings_id()==20);
    action(SDLK_LEFT);assert(!music_autoplay);
    music_autoplay=1;load_settings();assert(!music_autoplay);
    action(SDLK_m);assert(settings_tab==SETTINGS_ROMS&&settings_row==-1);
    action(SDLK_DOWN);assert(settings_id()==19);
    game_output=0;action(SDLK_RIGHT);assert(game_output==240);
    settings_tab=SETTINGS_SERVICES;settings_row=0;
    action(SDLK_LEFT);assert(!samba_pid); /* action rows require Enter */
    action(SDLK_RETURN);assert(!samba_pid&&strstr(samba_message,"STEAM LINK"));
    settings_row=2;action(SDLK_RETURN);assert(!samba_password_on);
    settings_tab=SETTINGS_GENERAL;settings_row=-1;action(SDLK_TAB);assert(settings_tab==SETTINGS_MOVIES);
    settings_row=0;action(SDLK_ESCAPE);assert(!settings_on);
    samba_password_text("Test password 123");assert(!strcmp(samba_password,"Test password 123"));
    samba_password_on=1;samba_password_action(SDLK_BACKSPACE);assert(!strcmp(samba_password,"Test password 12"));
    samba_password_action(SDLK_ESCAPE);assert(!samba_password_on&&!samba_password[0]);
    unlink("settings.cfg");assert(!chdir("/tmp"));rmdir(temp);
    puts("PASS: tab/row focus, keyboard and shoulder navigation, settings persistence, 240p, desktop service guard and password input");
    return 0;
}
