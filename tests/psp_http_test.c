#include "../psp/app.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
atomic_bool quitting;
char status_text[256];
bool network_cancelled(void){return false;}
void status(const char *format,...){va_list ap;va_start(ap,format);vsnprintf(status_text,sizeof(status_text),format,ap);va_end(ap);}
int main(int argc,char **argv){
    assert(argc==3);curl_global_init(CURL_GLOBAL_DEFAULT);Stream stream;
    if(!strcmp(argv[2],"reject")){assert(!stream_open(&stream,argv[1]));puts("Rejected invalid HTTP range response");return 0;}
    assert(stream_open(&stream,argv[1]));assert(stream.length==700000);
    unsigned char data[2000];
    for(unsigned offset=0;offset<699000;offset+=130997){
        assert(stream_read(&stream,offset,data,sizeof(data)));
        for(unsigned i=0;i<sizeof(data);i++)assert(data[i]==(offset+i)%251);
    }
    assert(!stream_read(&stream,UINT64_MAX,data,1));
    assert(!stream_read(&stream,699999,data,2));
    stream_close(&stream);curl_global_cleanup();puts("PSP bounded HTTP range streaming passed");return 0;
}
