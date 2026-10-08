#ifndef FLXTR_PSP_UPDATE_VALIDATION_H
#define FLXTR_PSP_UPDATE_VALIDATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PSP_UPDATE_MAX (4u*1024u*1024u)
static bool hex_string(const char *s,size_t n){
    if(!s||strlen(s)!=n)return false;
    for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return true;
}
static uint32_t read_le32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool valid_psp_pbp(const unsigned char *p,size_t n){
    if(n<92||n>PSP_UPDATE_MAX||memcmp(p,"\0PBP",4)||read_le32(p+4)!=0x10000)return false;
    uint32_t last=40;
    for(size_t i=8;i<40;i+=4){uint32_t at=read_le32(p+i);if(at<last||at>n)return false;last=at;}
    uint32_t elf=read_le32(p+32),end=read_le32(p+36);
    if(end<elf||end-elf<52)return false;
    const unsigned char *e=p+elf;
    return !memcmp(e,"\177ELF",4)&&e[4]==1&&e[5]==1&&e[18]==8&&e[19]==0;
}
#endif
