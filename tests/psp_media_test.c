#include "tilefinch/media_mp4.h"
#include "../psp/update_validation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
static bool file_read(void *p,uint64_t offset,void *out,size_t n){
    return offset<=LONG_MAX && !fseek(p,(long)offset,SEEK_SET) && fread(out,1,n,p)==n;
}
static void write32(unsigned char *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
int main(int argc,char **argv){
    assert(argc==2);Budget b;budget_init(&b,2*1024*1024);
    assert(!budget_malloc_category(&b,0,SIZE_MAX));assert(!b.current);
    void *p=budget_calloc_category(&b,0,32,32);assert(p);budget_free(&b,p);assert(!b.current);
    BudgetReservation r={0};assert(budget_reservation_acquire(&r,&b,0,1000));assert(!budget_reservation_acquire(&r,&b,0,1000));
    budget_reservation_release(&r);budget_reservation_release(&r);assert(!b.current);
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);
    MediaRangeReader reader={.opaque=f,.length=(uint64_t)n,.read=file_read};char error[256];
    MediaMp4Demux *demux=media_mp4_open(&b,&reader,NULL,error,sizeof(error));
    if(!demux)fprintf(stderr,"%s\n",error);
    assert(demux);
    assert(media_mp4_track_count(demux)==2);MediaMp4Sample sample;int packets=0;
    while(media_mp4_next_sample(demux,&sample)){
        unsigned char *bytes=malloc(sample.size);assert(bytes);
        assert(media_mp4_read_sample(demux,&sample,bytes,sample.size));free(bytes);packets++;
    }assert(packets>20);media_mp4_close(demux);fclose(f);assert(!b.current);
    assert(hex_string("abcdef1234",10));assert(!hex_string("../bad",6));
    unsigned char pbp[92]={0};memcpy(pbp,"\0PBP",4);write32(pbp+4,0x10000);
    for(int i=8;i<36;i+=4)write32(pbp+i,40);
    write32(pbp+36,92);
    memcpy(pbp+40,"\177ELF",4);pbp[44]=1;pbp[45]=1;pbp[58]=8;assert(valid_psp_pbp(pbp,sizeof(pbp)));
    pbp[58]=40;assert(!valid_psp_pbp(pbp,sizeof(pbp)));pbp[58]=8;
    write32(pbp+32,UINT32_MAX);assert(!valid_psp_pbp(pbp,sizeof(pbp)));
    puts("PSP allocator, MP4 streaming demux and update validation passed");return 0;
}
