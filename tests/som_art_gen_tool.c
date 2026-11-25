/* som_art_gen_tool ROM MASKS.bin MOVES.bin: what the core makes from a ROM. */
#define _POSIX_C_SOURCE 200809L
#include "../src/som_art_gen.h"
#include <stdio.h>
#include <time.h>
static int save(const char *path,const SomArtOut *o){
    FILE *f=fopen(path,"wb");
    const int ok=f&&fwrite(o->data,1,o->n,f)==o->n;
    return f&&fclose(f)==0&&ok;
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    FILE *f=fopen(argv[1],"rb");
    uint8_t *rom=malloc(4194304u+512u);
    if(!f||!rom)return 2;
    size_t n=fread(rom,1,4194304u+512u,f);
    fclose(f);
    struct timespec a,b,c;
    SomArtOut masks,moves;
    clock_gettime(CLOCK_MONOTONIC,&a);
    const int one=som_art_weapon_masks(rom,n,&masks);
    clock_gettime(CLOCK_MONOTONIC,&b);
    const int two=som_art_hero_moves(rom,n,&moves);
    clock_gettime(CLOCK_MONOTONIC,&c);
    if(!one||!two){fprintf(stderr,"generation failed: masks %d moves %d\n",one,two);return 1;}
    printf("masks %u entries %lu bytes %ld ms, moves %u entries %lu bytes %ld ms\n",masks.entries,(unsigned long)masks.n,
        (long)((b.tv_sec-a.tv_sec)*1000+(b.tv_nsec-a.tv_nsec)/1000000),moves.entries,(unsigned long)moves.n,
        (long)((c.tv_sec-b.tv_sec)*1000+(c.tv_nsec-b.tv_nsec)/1000000));
    return save(argv[2],&masks)&&save(argv[3],&moves)?0:1;
}
