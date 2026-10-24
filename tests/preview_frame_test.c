#define main preview_decoder_main
#include "../src/preview_decoder.c"
#undef main
#include <assert.h>
#include <sys/stat.h>
int main(void){
    char temp[]="/tmp/flxtr-preview-frame-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));assert(!mkdir("music-cache",0700));
    AVFrame *frame=av_frame_alloc();assert(frame);frame->width=640;frame->height=480;frame->format=AV_PIX_FMT_YUV420P;
    assert(!av_frame_get_buffer(frame,32));
    for(int y=0;y<480;y++)memset(frame->data[0]+y*frame->linesize[0],100,640);
    for(int plane=1;plane<3;plane++)for(int y=0;y<240;y++)memset(frame->data[plane]+y*frame->linesize[plane],128,320);
    assert(!write_preview(frame));SDL_Surface *image=SDL_LoadBMP("music-cache/preview-frame.bmp");assert(image);
    assert(image->w==240&&image->h==180);
    Uint8 r,g,b;SDL_GetRGB(*(Uint32*)image->pixels,image->format,&r,&g,&b);assert(r==98&&g==98&&b==98);SDL_FreeSurface(image);
    frame->width=2048;assert(write_preview(frame)<0);frame->width=640;
    av_frame_free(&frame);unlink("music-cache/preview-frame.bmp");assert(!rmdir("music-cache"));assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: bounded aspect-preserving preview frames, YUV conversion and oversized-input rejection");
}
