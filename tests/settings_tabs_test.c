#define FLXTR_DESKTOP
#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char temp[]="/tmp/flxtr-settings-tabs-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    settings_on=1;settings_tab=SETTINGS_VIDEO;settings_row=-1;
    action(SDLK_RIGHT);assert(settings_tab==SETTINGS_MUSIC&&settings_row==-1);
    action(SDLK_LEFT);assert(settings_tab==SETTINGS_VIDEO&&settings_row==-1);
    action(SDLK_DOWN);assert(settings_id()==0);
    for(int row=0;row<7;row++)action(SDLK_DOWN);
    assert(settings_id()==17);
    int previous=series_autoplay;action(SDLK_RETURN);assert(series_autoplay!=previous);
    for(int row=0;row<8;row++)action(SDLK_UP);
    assert(settings_row==-1);
    action(SDLK_TAB);assert(settings_tab==SETTINGS_MUSIC);
    action(SDLK_DOWN);assert(settings_id()==20);
    action(SDLK_LEFT);assert(!music_autoplay);
    music_autoplay=1;load_settings();assert(!music_autoplay);
    action(SDLK_m);assert(settings_tab==SETTINGS_ROMS&&settings_row==-1);
    action(SDLK_DOWN);assert(settings_id()==22);
    action(SDLK_DOWN);assert(settings_id()==19);
    game_output=0;action(SDLK_RIGHT);assert(game_output==240);
    settings_tab=SETTINGS_SERVICES;settings_row=0;
    action(SDLK_LEFT);assert(!samba_pid); /* action rows require Enter */
    action(SDLK_RETURN);assert(!samba_pid&&strstr(samba_message,"STEAM LINK"));
    settings_row=2;action(SDLK_RETURN);assert(!samba_password_on);
    settings_tab=SETTINGS_GENERAL;settings_row=-1;action(SDLK_TAB);assert(settings_tab==SETTINGS_VIDEO);
    /* Exercise the receiving-device settings while retaining desktop guards. */
    settings_tab=SETTINGS_STREAMING;settings_row=-1;
    action(SDLK_DOWN);assert(settings_id()==35);
    action(SDLK_RIGHT);assert(!strcmp(stream_audio,"hdmi"));
    action(SDLK_DOWN);assert(settings_id()==36);
    action(SDLK_RETURN);assert(!stream_audio_test); /* Desktop cannot take the Steam Link exit path. */
    settings_tab=SETTINGS_GENERAL;action(SDLK_TAB);assert(settings_tab==SETTINGS_VIDEO&&settings_row==-1);
    settings_row=0;action(SDLK_ESCAPE);assert(!settings_on);
    samba_password_text("Test password 123");assert(!strcmp(samba_password,"Test password 123"));
    samba_password_on=1;samba_password_action(SDLK_BACKSPACE);assert(!strcmp(samba_password,"Test password 12"));
    samba_password_action(SDLK_ESCAPE);assert(!samba_password_on&&!samba_password[0]);
    unlink("settings.cfg");unlink("streaming.cfg");unlink("streaming-audio.txt");unlink("tv-gambits.cfg");assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: tab/row focus, keyboard and shoulder navigation, settings persistence, 240p, desktop service guard and password input");
    return 0;
}
