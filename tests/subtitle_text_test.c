#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/subtitle_text.h"
#include "../src/font.h"
int main(void){
    char text[80];
    subtitle_text(text,sizeof(text),"0,0,Default,,0,0,0,,{\\i1}Hello\\Nworld{\\i0}",1);
    assert(!strcmp(text,"Hello\nworld"));
    subtitle_text(text,sizeof(text),"<b>Hello</b> there",0);assert(!strcmp(text,"Hello there"));
    subtitle_text(text,sizeof(text),"broken ASS",1);assert(!*text);
    subtitle_text(text,5,"long subtitle",0);assert(!strcmp(text,"long"));
    subtitle_text(text,sizeof(text),"What's that? That's John's game.",0);assert(!strcmp(text,"What's that? That's John's game."));
    subtitle_text(text,sizeof(text),"What\xe2\x80\x99s \xe2\x80\x9cthat\xe2\x80\x9d?",0);assert(!strcmp(text,"What's \"that\"?"));
    subtitle_text(text,sizeof(text),"That&#8217;s &apos;fine&apos; &amp; &#x2019;OK&#39;",0);assert(!strcmp(text,"That's 'fine' & 'OK'"));
    subtitle_text(text,sizeof(text),"What\x92s",0);assert(!strcmp(text,"What's"));
    int apostrophe=0,quote=0;
    for(size_t i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++){if(glyphs[i].c=='\'')apostrophe=1;if(glyphs[i].c=='"')quote=1;}
    assert(apostrophe&&quote);
    puts("PASS: ASS/WebVTT markup stripping, line breaks, malformed input and text bounds");
    return 0;
}
