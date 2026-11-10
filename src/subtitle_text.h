#include "display_text.h"
static void subtitle_text(char *out,size_t size,const char *in,int ass){
    if(ass){for(int i=0;i<8;i++){const char *comma=strchr(in,',');if(!comma){out[0]=0;return;}in=comma+1;}}
    size_t n=0;int tag=0;
    for(;*in&&n+1<size;){
        unsigned char c=(unsigned char)*in;
        if(c=='{'||c=='<'){tag=1;in++;continue;}if(c=='}'||c=='>'){tag=0;in++;continue;}if(tag){in++;continue;}
        if(c=='\\'&&(in[1]=='N'||in[1]=='n'||in[1]=='h')){out[n++]=in[1]=='h'?' ':'\n';in+=2;continue;}
        c=display_next(&in);
        if(c=='\n'||c>=32)out[n++]=(char)c;
    }out[n]=0;
}
