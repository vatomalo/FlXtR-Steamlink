#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>

int main(void) {
    assert(SDL_Init(SDL_INIT_TIMER)==0);
    FILE *f=fopen("build/test-catalog.tsv","w");assert(f);
    fputs("# comment\nONE\tDEMO\t\thttps://example.org/one.m3u8\nTWO\tDEMO\t\t\n",f);
    fputs("BAD\tDEMO\t\tfile:///etc/passwd\nMISSING FIELDS\n",f);
    for(int i=0;i<3500;i++)fputc('A',f);
    fputs("\nTHREE\tDEMO\t\thttps://example.org/three.mp4\n",f);fclose(f);
    assert(read_catalog("build/test-catalog.tsv")==3);
    assert(total==3&&selection==0);
    action(SDLK_RIGHT);assert(selection==1);
    action(SDLK_DOWN);assert(selection==1); /* no move to nonexistent row */
    action(SDLK_RIGHT);action(SDLK_RIGHT);assert(selection==2);
    action(SDLK_TAB);assert(total==2&&selection==0&&visible[1]==2);
    titles[0].url[0]=titles[2].url[0]=0;filter();assert(total==0);
    action(SDLK_RIGHT);action(SDLK_RETURN);assert(selection==0);
    action(SDLK_TAB);assert(total==3);
    action(SDLK_i);assert(about);action(SDLK_ESCAPE);assert(!about&&running);
    action(SDLK_y);assert(!stars_on);
    action(SDLK_ESCAPE);assert(!running);
    SDL_Quit();puts("PASS: catalog bounds, URL policy, navigation, filters and exit");return 0;
}
