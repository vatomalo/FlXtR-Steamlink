#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>

int main(void) {
    VideoRect r;
    assert(!video_rect(VIEW_FIT,720,576,16,15,1920,1080,&r));
    assert(r.x==240&&r.y==0&&r.w==1440&&r.h==1080);
    assert(!video_rect(VIEW_STRETCH,640,480,1,1,1920,1080,&r));
    assert(r.x==0&&r.y==0&&r.w==1920&&r.h==1080);
    assert(!video_rect(VIEW_PIXEL,640,480,1,1,1920,1080,&r));
    assert(r.x==640&&r.y==300&&r.w==640&&r.h==480);
    assert(video_rect(VIEW_PIXEL,1920,1080,1,1,1280,720,&r)<0);
    assert(video_rect(VIEW_FIT,0,480,1,1,1920,1080,&r)<0);
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
    action(SDLK_v);assert(viewing==VIEW_STRETCH);
    action(SDLK_v);assert(viewing==VIEW_PIXEL);
    action(SDLK_v);assert(viewing==VIEW_FIT);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(open_ui()==0);selection=1;draw(0);
    assert(cached_page==0);
    close_ui();assert(!window&&!renderer&&cached_page==-1);
    assert(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO));
    assert(SDL_WasInit(SDL_INIT_TIMER)&SDL_INIT_TIMER);
    assert(open_ui()==0);assert(selection==1&&total==3);draw(0);
    close_ui();
    action(SDLK_ESCAPE);assert(!running);
    SDL_Quit();puts("PASS: catalog bounds, navigation, graphics release/restore and exit");return 0;
}
