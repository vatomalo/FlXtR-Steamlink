#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "font.h"

#define W 960
#define H 540
#define MAX_TITLES 128
#define VISIBLE 6
#define STARS 56
typedef struct { char title[80],meta[96],poster[192],url[2048]; } Title;
static Title titles[MAX_TITLES];
static int count, selection, ready_only, about, stars_on=1, running=1;
static int visible[MAX_TITLES], total, cached_page=-1;
static SDL_Renderer *renderer;
static SDL_Window *window;
static SDL_Texture *posters[VISIBLE];
static SDL_GameController *pads[4];
static pid_t player_pid;
static int stopping_player;
static Uint32 stop_time;
static char status[100]="SELECT A TITLE TO BEGIN";
static unsigned char star_x[STARS], star_y[STARS], star_speed[STARS];
static const SDL_Color green={116,255,132,255}, dim={66,126,77,255}, white={212,226,214,255};

static void color(SDL_Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
static void rect(int x,int y,int w,int h,SDL_Color c,int fill) {
    SDL_Rect r={x,y,w,h}; color(c);
    if(fill) SDL_RenderFillRect(renderer,&r); else SDL_RenderDrawRect(renderer,&r);
}
static void text(int x,int y,const char *str,int scale,SDL_Color c,int limit) {
    int n=0; color(c);
    for(;*str && n<limit; str++,n++,x+=6*scale) {
        unsigned char ch=(unsigned char)toupper((unsigned char)*str);
        size_t i;
        if(ch==' ') continue;
        for(i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==ch) break;
        if(i==sizeof(glyphs)/sizeof(glyphs[0])) i=sizeof(glyphs)/sizeof(glyphs[0])-5;
        for(int y1=0;y1<7;y1++) for(int x1=0;x1<5;x1++) if(glyphs[i].row[y1]&(16>>x1)) {
            SDL_Rect r={x+x1*scale,y+y1*scale,scale,scale}; SDL_RenderFillRect(renderer,&r);
        }
    }
}
static void wrap(int x,int y,const char *str,int cols,int lines,SDL_Color c) {
    for(int row=0;*str && row<lines;row++) {
        char line[81]; size_t len=strlen(str), n=len<(size_t)cols?len:(size_t)cols;
        if(len>n) { size_t k=n; while(k>0 && str[k]!=' ') k--; if(k>0)n=k; }
        memcpy(line,str,n); line[n]=0; text(x,y+row*20,line,2,c,cols);
        str+=n; while(*str==' ')str++;
    }
}
static void clear_posters(void) {
    for(int i=0;i<VISIBLE;i++) { if(posters[i])SDL_DestroyTexture(posters[i]); posters[i]=NULL; }
    cached_page=-1;
}
static void filter(void) {
    total=0; for(int i=0;i<count;i++)if(!ready_only||titles[i].url[0])visible[total++]=i;
    selection=0; clear_posters();
}
static int read_catalog(const char *path) {
    FILE *f=fopen(path,"r"); char line[2600];
    if(!f) { snprintf(status,sizeof(status),"CATALOG NOT FOUND - ADD CATALOG.TSV");return 0; }
    while(fgets(line,sizeof(line),f) && count<MAX_TITLES) {
        char *fields[4],*p=line; int valid=1;
        if(line[0]=='#'||line[0]=='\n')continue;
        if(!strchr(line,'\n')&&!feof(f)) { int ch; while((ch=fgetc(f))!=EOF&&ch!='\n'){} continue; }
        line[strcspn(line,"\r\n")]=0;
        for(int i=0;i<4;i++) { fields[i]=p; if(i<3) { char *tab=strchr(p,'\t'); if(!tab){valid=0;break;} *tab=0;p=tab+1; } }
        if(!valid||!fields[0][0]||strchr(fields[3],'\t'))continue;
        Title *t=&titles[count];
        if(strlen(fields[0])>=sizeof(t->title)||strlen(fields[1])>=sizeof(t->meta)||strlen(fields[2])>=sizeof(t->poster)||strlen(fields[3])>=sizeof(t->url))continue;
        if(fields[3][0] && strncmp(fields[3],"https://",8) && strncmp(fields[3],"http://",7))continue;
        strcpy(t->title,fields[0]);strcpy(t->meta,fields[1]);strcpy(t->poster,fields[2]);strcpy(t->url,fields[3]);count++;
    }
    fclose(f); filter();return count;
}
/* Inspect BMP header before decoding: disk and decoded sizes both stay bounded. */
static SDL_Texture *load_poster(const char *path) {
    struct stat s; unsigned char header[26]; FILE *f; SDL_Surface *image; SDL_Texture *texture;
    if(!path[0]||stat(path,&s)||s.st_size<54||s.st_size>192*288*4+4096)return NULL;
    f=fopen(path,"rb");if(!f)return NULL;
    size_t n=fread(header,1,sizeof(header),f);fclose(f);
    if(n!=sizeof(header)||header[0]!='B'||header[1]!='M'||header[14]<40)return NULL;
    uint32_t width=(uint32_t)header[18]|(uint32_t)header[19]<<8|(uint32_t)header[20]<<16|(uint32_t)header[21]<<24;
    int32_t height=(int32_t)((uint32_t)header[22]|(uint32_t)header[23]<<8|(uint32_t)header[24]<<16|(uint32_t)header[25]<<24);
    if(width<1||width>192||height==0||height>288||height< -288)return NULL;
    image=SDL_LoadBMP(path);if(!image)return NULL;
    texture=SDL_CreateTextureFromSurface(renderer,image);SDL_FreeSurface(image);return texture;
}
static void cache_page(int page) {
    if(cached_page==page)return;
    clear_posters();cached_page=page;
    for(int i=0;i<VISIBLE && page*VISIBLE+i<total;i++)posters[i]=load_poster(titles[visible[page*VISIBLE+i]].poster);
}
static void placeholder(int x,int y,int w,int h,int seed) {
    SDL_Color a={10,23,16,255}, b={23,57,35,255};rect(x,y,w,h,a,1);
    for(int i=0;i<6;i++)rect(x+8+i*9,y+h/2-i*7,w-16-i*18,3,b,1);
    text(x+10,y+12,"NO ART",1,dim,12);
    char code[16];snprintf(code,sizeof(code),"%03d",seed+1);text(x+10,y+h-28,code,2,green,5);
}
static void draw(Uint32 tick) {
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    if(stars_on)for(int i=0;i<STARS;i++) {
        int x=star_x[i]*W/256, y=(star_y[i]*H/256+(tick/100)*star_speed[i]/8)%H;
        SDL_Color c={145+(i%3)*40,145+(i%3)*40,145+(i%3)*40,255};rect(x,y,1+(i%9==0),1+(i%9==0),c,1);
    }
    text(30,24,"GREENLINK",4,green,12);text(251,38,"CINEMA / STEAM LINK",1,dim,24);
    text(737,32,"NATIVE / MINIMAL",2,dim,17);
    rect(30,65,900,1,dim,1);
    text(30,92,"LIBRARY",2,green,12);
    text(30,124,ready_only?"  ALL TITLES":"> ALL TITLES",1,ready_only?dim:green,20);
    text(30,145,ready_only?"> READY TO PLAY":"  READY TO PLAY",1,ready_only?green:dim,20);
    char num[64];snprintf(num,sizeof(num),"%03d TITLES",total);text(30,181,num,1,white,20);
    text(30,420,"[TAB] FILTER",1,dim,22);text(30,439,"[Y] STARS",1,dim,22);text(30,458,"[I] ABOUT",1,dim,22);
    if(about) {
        text(194,100,"SMALL BY DESIGN",3,green,35);
        wrap(194,155,"A QUIET LIBRARY FOR YOUR STEAM LINK. BLACK SPACE, GREEN PIXELS, AND A FEW DISTANT STARS.",53,4,white);
        wrap(194,260,"C + SDL2. SIX POSTERS IN MEMORY. NO BROWSER. NO WEB UI. DIRECT VIDEO PLAYBACK USES A SEPARATE PLAYER.",53,5,dim);
        text(194,419,"B / ESC TO RETURN",2,green,30);
    } else if(!total) {
        text(194,143,"NOTHING HERE YET",3,green,40);
        wrap(194,195,"ADD TITLES TO CATALOG.TSV OR PRESS TAB TO SHOW ALL TITLES.",48,4,white);
    } else {
        int page=selection/VISIBLE;cache_page(page);
        for(int slot=0;slot<VISIBLE && page*VISIBLE+slot<total;slot++) {
            int index=page*VISIBLE+slot,x=194+(slot%3)*150,y=94+(slot/3)*194;
            Title *t=&titles[visible[index]]; SDL_Rect dst={x+5,y+5,124,142};
            rect(x,y,134,152,index==selection?green:dim,0);
            if(posters[slot]) { int iw,ih;SDL_QueryTexture(posters[slot],NULL,NULL,&iw,&ih);
                double scale=124.0/iw;if(ih*scale>142)scale=142.0/ih;
                dst.w=(int)(iw*scale);dst.h=(int)(ih*scale);dst.x=x+5+(124-dst.w)/2;dst.y=y+5+(142-dst.h)/2;
                SDL_RenderCopy(renderer,posters[slot],NULL,&dst);
            } else placeholder(dst.x,dst.y,dst.w,dst.h,visible[index]);
            text(x,y+162,t->title,1,index==selection?green:white,22);
            text(x,y+177,t->url[0]?"DIRECT URL":"SOURCE NEEDED",1,dim,22);
        }
        Title *t=&titles[visible[selection]];
        rect(665,94,1,378,dim,1);text(690,97,"NOW SELECTED",1,dim,30);
        wrap(690,126,t->title,19,4,green);wrap(690,228,t->meta,19,5,white);
        text(690,354,t->url[0]?"[A] PLAY":"NO STREAM YET",2,t->url[0]?green:dim,20);
        snprintf(num,sizeof(num),"PAGE %02d / %02d",page+1,(total+5)/6);text(690,455,num,1,dim,25);
    }
    rect(30,489,900,1,dim,1);text(30,509,status,1,white,92);text(713,509,"DPAD MOVE / B BACK",1,green,30);
    SDL_RenderPresent(renderer);
}
static void play(void) {
    if(about||!total||player_pid)return;
    Title *t=&titles[visible[selection]];
    if(!t->url[0]) { snprintf(status,sizeof(status),"NO DIRECT STREAM - FLIXER RESOLUTION IS NOT CONNECTED YET");return; }
    if(access("./greenlink-player",X_OK)) { snprintf(status,sizeof(status),"PLAYER NOT BUILT - RUN SCRIPTS/BUILD-PLAYER.SH");return; }
    clear_posters();SDL_HideWindow(window);
    player_pid=fork();
    if(player_pid==0) {
        int log=open("player.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(log>=0){dup2(log,STDERR_FILENO);dup2(log,STDOUT_FILENO);close(log);}
        execl("./greenlink-player","greenlink-player",t->url,(char*)NULL);_exit(127);
    }
    if(player_pid<0){player_pid=0;SDL_ShowWindow(window);snprintf(status,sizeof(status),"COULD NOT START PLAYER");}
    else snprintf(status,sizeof(status),"PLAYING - B TO STOP");
}
static void action(SDL_Keycode key) {
    if(player_pid) {
        if((key==SDLK_ESCAPE||key==SDLK_BACKSPACE) && !stopping_player){kill(player_pid,SIGTERM);stopping_player=1;stop_time=SDL_GetTicks();}
        return;
    }
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE) { if(about)about=0;else running=0; }
    else if(key==SDLK_i)about=!about;
    else if(key==SDLK_y)stars_on=!stars_on;
    else if(key==SDLK_TAB){ready_only=!ready_only;filter();}
    else if(key==SDLK_RETURN||key==SDLK_SPACE)play();
    else if(!about&&total){
        if(key==SDLK_RIGHT && selection+1<total)selection++;
        if(key==SDLK_LEFT && selection>0)selection--;
        if(key==SDLK_DOWN && selection+3<total)selection+=3;
        if(key==SDLK_UP && selection>=3)selection-=3;
        snprintf(status,sizeof(status),"%s",titles[visible[selection]].title);
    }
}
static void add_pad(int device) {
    if(!SDL_IsGameController(device))return;
    SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(device);
    for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==id)return;
    for(int i=0;i<4;i++)if(!pads[i]){pads[i]=SDL_GameControllerOpen(device);break;}
}
int main(int argc,char **argv) {
    const char *catalog="catalog.tsv",*shot=NULL;int frames=0,seen=0;Uint32 start,last_draw=0;int dirty=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--catalog")&&i+1<argc)catalog=argv[++i];
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)shot=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--no-stars"))stars_on=0;
        else {fprintf(stderr,"Usage: %s [--catalog file] [--screenshot file.bmp] [--frames N] [--no-stars]\n",argv[0]);return 2;}
    }
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)) {fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    window=SDL_CreateWindow("Greenlink",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,SDL_WINDOW_SHOWN);
    if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){fprintf(stderr,"Renderer: %s\n",SDL_GetError());SDL_Quit();return 1;}
    SDL_RenderSetLogicalSize(renderer,W,H);SDL_ShowCursor(SDL_DISABLE);
    for(int i=0;i<SDL_NumJoysticks();i++)add_pad(i);
    uint32_t seed=0x31415926;
    for(int i=0;i<STARS;i++){seed=seed*1664525u+1013904223u;star_x[i]=seed>>24;star_y[i]=seed>>16;star_speed[i]=1+(seed%3);}
    read_catalog(catalog);start=SDL_GetTicks();
    while(running) {
        SDL_Event event;
        if(SDL_WaitEventTimeout(&event,stars_on?33:250)) do {
            dirty=1;
            if(event.type==SDL_QUIT)running=0;
            else if(event.type==SDL_KEYDOWN)action(event.key.keysym.sym);
            else if(event.type==SDL_CONTROLLERDEVICEADDED)add_pad(event.cdevice.which);
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED){for(int i=0;i<4;i++)if(pads[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i]))==event.cdevice.which){SDL_GameControllerClose(pads[i]);pads[i]=NULL;}}
            else if(event.type==SDL_CONTROLLERBUTTONDOWN) {
                switch(event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:action(SDLK_RETURN);break;
                    case SDL_CONTROLLER_BUTTON_B:action(SDLK_ESCAPE);break;
                    case SDL_CONTROLLER_BUTTON_X:action(SDLK_TAB);break;
                    case SDL_CONTROLLER_BUTTON_Y:action(SDLK_y);break;
                    case SDL_CONTROLLER_BUTTON_START:action(SDLK_i);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:action(SDLK_UP);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:action(SDLK_DOWN);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:action(SDLK_LEFT);break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:action(SDLK_RIGHT);break;
                }
            }
        } while(SDL_PollEvent(&event));
        if(player_pid){int code;pid_t p=waitpid(player_pid,&code,WNOHANG);
            if(p==player_pid){player_pid=0;stopping_player=0;SDL_ShowWindow(window);snprintf(status,sizeof(status),WIFEXITED(code)&&WEXITSTATUS(code)==0?"PLAYBACK FINISHED":"PLAYER STOPPED - SEE PLAYER.LOG");dirty=1;}
            else if(stopping_player&&SDL_GetTicks()-stop_time>3000)kill(player_pid,SIGKILL);
            SDL_Delay(30);continue;
        }
        Uint32 now=SDL_GetTicks();
        if(dirty||(stars_on&&now-last_draw>=33)||shot||frames){draw(now-start);last_draw=now;dirty=0;seen++;
            if(shot){int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
                if(!s||SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch)||SDL_SaveBMP(s,shot)){fprintf(stderr,"Screenshot: %s\n",SDL_GetError());SDL_FreeSurface(s);return 1;}SDL_FreeSurface(s);running=0;}
            if(frames>0&&seen>=frames)running=0;
        }
    }
    if(player_pid){kill(player_pid,SIGKILL);waitpid(player_pid,NULL,0);}
    clear_posters();for(int i=0;i<4;i++)if(pads[i])SDL_GameControllerClose(pads[i]);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
