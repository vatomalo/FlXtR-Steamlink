#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/subtitle_text.h"
int main(void){
    char text[80];
    subtitle_text(text,sizeof(text),"0,0,Default,,0,0,0,,{\\i1}Hello\\Nworld{\\i0}",1);
    assert(!strcmp(text,"Hello\nworld"));
    subtitle_text(text,sizeof(text),"<b>Hello</b> there",0);assert(!strcmp(text,"Hello there"));
    subtitle_text(text,sizeof(text),"broken ASS",1);assert(!*text);
    subtitle_text(text,5,"long subtitle",0);assert(!strcmp(text,"long"));
    puts("PASS: ASS/WebVTT markup stripping, line breaks, malformed input and text bounds");
    return 0;
}
