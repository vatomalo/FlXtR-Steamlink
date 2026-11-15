/* Headless libretro host for Secret of Mana Ultimate regression runs.
 *
 *   som-harness CORE.so ROM START SCRIPT OUTDIR
 *
 * CORE is the AI proxy (or any libretro core). START is a save state
 * (retro_unserialize) or, ending in .srm, battery SRAM copied into the core
 * before the first frame; "-" for neither. SCRIPT lines (# comments):
 *
 *   frames N                 run N frames, no buttons
 *   hold BTN[,BTN] N         hold buttons for N frames (B Y SELECT START UP
 *                            DOWN LEFT RIGHT A X L R L2 R2 L3 R3)
 *   cmd TEXT                 flxtr_som_ai_command(TEXT) (proxy only)
 *   shot NAME                write OUTDIR/NAME.ppm of the last frame
 *   peek                     print hero, monster slots and HUD fields
 *   oam                      print OAM entries in the bottom HUD band
 *   save FILE                serialize, write FILE, check it reloads
 *   unload                   retro_unload_game (exit-time hooks)
 *   expect u8|u16 ADDR OP V  WRAM (7E/7F offset, hex) compared with V
 *                            (OP: == != < <= > >=); a miss fails the run
 *   expect-moved MIN MAX     hero moved MIN..MAX px (Manhattan) since mark
 *   mark                     remember the hero position for expect-moved
 *   monster SLOT DX DY       move monster SLOT relative to the hero
 *
 * Exit status: 0 all expectations met, 1 an expectation failed, 2 usage or
 * load error. Every line of output starts with "[harness]" so the runner
 * can separate it from the proxy's "[som-ai]" log. No ROM data is written
 * anywhere but OUTDIR. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/vendor/libretro.h"
#ifndef RETRO_DEVICE_ID_JOYPAD_MASK
#define RETRO_DEVICE_ID_JOYPAD_MASK 256
#endif

static void *frame_copy;
static unsigned frame_w,frame_h,pixel_format;
static size_t frame_pitch;
static uint16_t buttons;
static const char *outdir=".";

static void video(const void *data,unsigned w,unsigned h,size_t pitch){
    if(!data||data==RETRO_HW_FRAME_BUFFER_VALID)return;
    void *copy=realloc(frame_copy,h*pitch);
    if(!copy)return;
    frame_copy=copy;memcpy(frame_copy,data,h*pitch);
    frame_w=w;frame_h=h;frame_pitch=pitch;
}
static void audio(int16_t l,int16_t r){(void)l;(void)r;}
static size_t audio_batch(const int16_t *d,size_t n){(void)d;return n;}
static void poll(void){}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id){
    if(port||index||(device&RETRO_DEVICE_MASK)!=RETRO_DEVICE_JOYPAD)return 0;
    if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)buttons;
    return id<16u&&(buttons&(1u<<id))?1:0;
}
static bool env(unsigned cmd,void *data){
    switch(cmd){
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:pixel_format=*(const unsigned*)data;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=outdir;return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool*)data=true;return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:return true;
    default:return false;
    }
}
static int shot(const char *name){
    if(!frame_copy){fprintf(stderr,"[harness] FAIL shot %s: no frame yet\n",name);return 0;}
    char path[512];snprintf(path,sizeof(path),"%s/%s.ppm",outdir,name);
    FILE *o=fopen(path,"wb");
    if(!o)return 0;
    fprintf(o,"P6\n%u %u\n255\n",frame_w,frame_h);
    for(unsigned y=0;y<frame_h;y++)for(unsigned x=0;x<frame_w;x++){
        const uint8_t *row=(const uint8_t*)frame_copy+y*frame_pitch;
        uint8_t rgb[3];
        if(pixel_format==RETRO_PIXEL_FORMAT_XRGB8888){
            const uint32_t p=((const uint32_t*)(const void*)row)[x];
            rgb[0]=(uint8_t)(p>>16);rgb[1]=(uint8_t)(p>>8);rgb[2]=(uint8_t)p;
        }else{
            const uint16_t p=((const uint16_t*)(const void*)row)[x];
            if(pixel_format==RETRO_PIXEL_FORMAT_RGB565){
                rgb[0]=(uint8_t)(((p>>11)&31u)<<3);rgb[1]=(uint8_t)(((p>>5)&63u)<<2);
                rgb[2]=(uint8_t)((p&31u)<<3);
            }else{
                rgb[0]=(uint8_t)(((p>>10)&31u)<<3);rgb[1]=(uint8_t)(((p>>5)&31u)<<3);
                rgb[2]=(uint8_t)((p&31u)<<3);
            }
        }
        fwrite(rgb,1,3,o);
    }
    fclose(o);
    fprintf(stderr,"[harness] shot %s %ux%u\n",path,frame_w,frame_h);
    return 1;
}
static unsigned button_mask(char *list){
    static const char *const names[16]={"B","Y","SELECT","START","UP","DOWN","LEFT","RIGHT",
                                        "A","X","L","R","L2","R2","L3","R3"};
    unsigned mask=0;
    for(char *t=strtok(list,",");t;t=strtok(NULL,",")){
        unsigned i=0;
        while(i<16u&&strcmp(t,names[i]))i++;
        if(i==16u){fprintf(stderr,"[harness] FAIL unknown button %s\n",t);return 0xFFFFFFFFu;}
        mask|=1u<<i;
    }
    return mask;
}
static void *need(void *h,const char *s){
    void *p=dlsym(h,s);
    if(!p){fprintf(stderr,"[harness] FAIL core lacks %s\n",s);exit(2);}
    return p;
}
static void *read_file(const char *path,size_t *size){
    FILE *f=fopen(path,"rb");
    if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    void *data=n>0?malloc((size_t)n):NULL;
    if(data&&fread(data,1,(size_t)n,f)!=(size_t)n){free(data);data=NULL;}
    fclose(f);
    *size=data?(size_t)n:0;
    return data;
}
static unsigned u16(const uint8_t *p){return (unsigned)(p[0]|p[1]<<8);}
static int compare(unsigned long a,const char *op,unsigned long b){
    if(!strcmp(op,"=="))return a==b;
    if(!strcmp(op,"!="))return a!=b;
    if(!strcmp(op,"<"))return a<b;
    if(!strcmp(op,"<="))return a<=b;
    if(!strcmp(op,">"))return a>b;
    if(!strcmp(op,">="))return a>=b;
    return -1;
}

int main(int argc,char **argv){
    if(argc!=6){
        fputs("[harness] usage: som-harness CORE.so ROM START|- SCRIPT OUTDIR\n",stderr);
        return 2;
    }
    outdir=argv[5];
    void *h=dlopen(argv[1],RTLD_NOW);
    if(!h){fprintf(stderr,"[harness] FAIL dlopen: %s\n",dlerror());return 2;}
    void(*set_env)(retro_environment_t)=need(h,"retro_set_environment");
    void(*set_video)(retro_video_refresh_t)=need(h,"retro_set_video_refresh");
    void(*set_audio)(retro_audio_sample_t)=need(h,"retro_set_audio_sample");
    void(*set_batch)(retro_audio_sample_batch_t)=need(h,"retro_set_audio_sample_batch");
    void(*set_poll)(retro_input_poll_t)=need(h,"retro_set_input_poll");
    void(*set_input)(retro_input_state_t)=need(h,"retro_set_input_state");
    void(*init)(void)=need(h,"retro_init");
    bool(*load_game)(const struct retro_game_info*)=need(h,"retro_load_game");
    void(*unload_game)(void)=need(h,"retro_unload_game");
    void(*run)(void)=need(h,"retro_run");
    bool(*serialize)(void*,size_t)=need(h,"retro_serialize");
    bool(*unserialize)(const void*,size_t)=need(h,"retro_unserialize");
    size_t(*serialize_size)(void)=need(h,"retro_serialize_size");
    void*(*mem)(unsigned)=need(h,"retro_get_memory_data");
    size_t(*mem_size)(unsigned)=need(h,"retro_get_memory_size");
    bool(*command)(const char*)=(bool(*)(const char*))dlsym(h,"flxtr_som_ai_command");
    set_env(env);init();
    set_video(video);set_audio(audio);set_batch(audio_batch);set_poll(poll);set_input(input);
    size_t rom_size=0;
    void *rom=read_file(argv[2],&rom_size);
    if(!rom){fprintf(stderr,"[harness] FAIL cannot read ROM %s\n",argv[2]);return 2;}
    struct retro_game_info info={argv[2],rom,rom_size,NULL};
    if(!load_game(&info)){fputs("[harness] FAIL retro_load_game refused the ROM\n",stderr);return 2;}
    if(strcmp(argv[3],"-")){
        size_t n=0;void *start=read_file(argv[3],&n);
        if(!start){fprintf(stderr,"[harness] FAIL cannot read %s\n",argv[3]);return 2;}
        if(strstr(argv[3],".srm")){
            const size_t cap=mem_size(RETRO_MEMORY_SAVE_RAM);
            if(!cap||!mem(RETRO_MEMORY_SAVE_RAM)){fputs("[harness] FAIL no SRAM\n",stderr);return 2;}
            memcpy(mem(RETRO_MEMORY_SAVE_RAM),start,n<cap?n:cap);
            fprintf(stderr,"[harness] SRAM %zu bytes\n",n);
        }else if(!unserialize(start,n)){
            fprintf(stderr,"[harness] FAIL state of %zu bytes refused (core wants %zu)\n",
                    n,serialize_size());
            return 2;
        }else fprintf(stderr,"[harness] state %zu bytes loaded\n",n);
        free(start);
    }
    FILE *script=fopen(argv[4],"r");
    if(!script){fprintf(stderr,"[harness] FAIL cannot read script %s\n",argv[4]);return 2;}
    char line[256];unsigned long frame=0,line_no=0;int failures=0,mark_x=0,mark_y=0;
    while(fgets(line,sizeof(line),script)){
        line_no++;
        line[strcspn(line,"\r\n")]=0;
        if(!line[0]||line[0]=='#')continue;
        uint8_t *ram=(uint8_t*)mem(RETRO_MEMORY_SYSTEM_RAM);
        char a[64]={0},b[200]={0},op[4]={0};unsigned long k=0,v=0,addr=0;
        if(sscanf(line,"frames %lu",&k)==1){for(unsigned long i=0;i<k;i++){run();frame++;}}
        else if(sscanf(line,"hold %63s %lu",a,&k)==2){
            const unsigned m=button_mask(a);
            if(m==0xFFFFFFFFu)return 2;
            buttons=(uint16_t)m;
            for(unsigned long i=0;i<k;i++){run();frame++;}
            buttons=0;
        }else if(!strncmp(line,"cmd ",4)){
            if(!command){fputs("[harness] FAIL core is not the AI proxy\n",stderr);return 2;}
            fprintf(stderr,"[harness] cmd %s -> %d\n",line+4,command(line+4));
        }else if(sscanf(line,"shot %199s",b)==1){if(!shot(b))failures++;}
        else if(sscanf(line,"save %199s",b)==1){
            const size_t z=serialize_size();void *buf=calloc(1,z?z:1);
            const int ok=z&&serialize(buf,z);
            FILE *o=ok?fopen(b,"wb"):NULL;
            if(o){fwrite(buf,1,z,o);fclose(o);}
            const int back=ok&&unserialize(buf,z);
            fprintf(stderr,"[harness] save %s %zu bytes serialize %d reload %d\n",b,z,ok,back);
            if(!ok||!back||!o)failures++;
            free(buf);
        }else if(!strcmp(line,"unload")){unload_game();fputs("[harness] unloaded\n",stderr);break;}
        else if(!strcmp(line,"mark")){mark_x=(int)u16(ram+0xE002);mark_y=(int)u16(ram+0xE004);}
        else if(sscanf(line,"expect-moved %lu %lu",&k,&v)==2){
            const unsigned long moved=(unsigned long)(abs((int)u16(ram+0xE002)-mark_x)+
                                                      abs((int)u16(ram+0xE004)-mark_y));
            const int ok=moved>=k&&moved<=v;
            fprintf(stderr,"[harness] %s line %lu: hero moved %lu px (want %lu..%lu)\n",
                    ok?"ok":"FAIL",line_no,moved,k,v);
            failures+=!ok;
        }else if(sscanf(line,"expect %3s %lx %3s %li",a,&addr,op,(long*)&v)==4){
            const int wide=!strcmp(a,"u16");
            if((strcmp(a,"u8")&&!wide)||addr+1u>=0x20000u){
                fprintf(stderr,"[harness] FAIL line %lu: bad expect\n",line_no);return 2;
            }
            const unsigned long got=wide?u16(ram+addr):ram[addr];
            const int ok=compare(got,op,v);
            if(ok<0){fprintf(stderr,"[harness] FAIL line %lu: bad operator %s\n",line_no,op);return 2;}
            fprintf(stderr,"[harness] %s line %lu: %s 7E:%04lX = %lu, want %s %lu\n",
                    ok?"ok":"FAIL",line_no,a,addr,got,op,v);
            failures+=!ok;
        }else if(!strcmp(line,"peek")){
            fprintf(stderr,"[harness] f%lu hero %u,%u face %02x pose %02x/%02x status %04x "
                    "hp %u gauge %u weapon %u map %u mode %u/%02x event %02x\n",
                    frame,u16(ram+0xE002),u16(ram+0xE004),ram[0xE010],ram[0xE01C],ram[0xE011],
                    u16(ram+0xE190),u16(ram+0xE182),ram[0xE1ED],ram[0xE1E4],u16(ram+0xDC),
                    ram[0xE4],ram[0xE6],ram[0xD0]);
            for(unsigned s=0;s<3u;s++){
                const uint8_t *o=ram+0xE600u+0x200u*s;
                fprintf(stderr,"[harness]   monster %u active %u id %02x at %u,%u hp %u pending %u\n",
                        s,o[0],o[0x180],u16(o+2),u16(o+4),u16(o+0x182),o[0x1F1]);
            }
        }else if(!strcmp(line,"oam")){
            for(unsigned i=0;i<128u;i++){
                const uint8_t *e=ram+0x0800u+4u*i;
                const unsigned hb=(ram[0x0A00u+i/4u]>>(2u*(i%4u)))&3u;
                if(e[1]>=176u&&e[1]<0xE0u)
                    fprintf(stderr,"[harness] oam %u x %u y %u tile %03x attr %02x large %u\n",
                            i,e[0]|((hb&1u)<<8),e[1],e[2]|((e[3]&1u)<<8),e[3],hb>>1);
            }
        }else if(sscanf(line,"monster %lu %63s %199s",&k,a,b)==3&&k<3u){
            uint8_t *o=ram+0xE600u+0x200u*k;
            const int x=(int)u16(ram+0xE002)+atoi(a),y=(int)u16(ram+0xE004)+atoi(b);
            o[2]=(uint8_t)x;o[3]=(uint8_t)(x>>8);o[4]=(uint8_t)y;o[5]=(uint8_t)(y>>8);
            fprintf(stderr,"[harness] monster %lu placed at %d,%d (active %u id %02x)\n",
                    k,x,y,o[0],o[0x180]);
        }else{fprintf(stderr,"[harness] FAIL line %lu: unknown command: %s\n",line_no,line);return 2;}
    }
    fclose(script);
    fprintf(stderr,"[harness] done at frame %lu, %d failure(s)\n",frame,failures);
    return failures?1:0;
}
