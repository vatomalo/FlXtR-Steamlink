#ifndef FLXTR_DISPLAY_TEXT_H
#define FLXTR_DISPLAY_TEXT_H
/* Presentation folding for the compact pixel font, never search tokenization. */
static unsigned char display_ascii(unsigned long code){
    if(code==0x2018||code==0x2019||code==0x201a||code==0x201b||code==0x91||code==0x92)return '\'';
    if(code==0x201c||code==0x201d||code==0x201e||code==0x201f||code==0x93||code==0x94)return '"';
    if(code==0x2013||code==0x2014||code==0x2212||code==0x96||code==0x97)return '-';
    if(code==0xa0)return ' ';
    if(code==0x2026)return '.';
    return code<128?(unsigned char)code:'?';
}
static unsigned char display_next(const char **input){
    const unsigned char *p=(const unsigned char *)*input;unsigned long code=*p;
    if(*p=='&'){
        const struct {const char *name;unsigned char value;} entities[]={{"&apos;",'\''},{"&rsquo;",'\''},{"&lsquo;",'\''},{"&quot;",'"'},{"&amp;",'&'},{"&nbsp;",' '},{"&lt;",'<'},{"&gt;",'>'}};
        for(size_t i=0;i<sizeof(entities)/sizeof(entities[0]);i++){
            size_t n=strlen(entities[i].name);
            if(!strncmp((const char *)p,entities[i].name,n)){*input+=(int)n;return entities[i].value;}
        }
        if(p[1]=='#'){
            size_t i=2;int base=10,digits=0;code=0;
            if(p[i]=='x'||p[i]=='X'){base=16;i++;}
            for(;i<12&&p[i]&&p[i]!=';';i++){
                int value=p[i]>='0'&&p[i]<='9'?p[i]-'0':base==16&&p[i]>='a'&&p[i]<='f'?p[i]-'a'+10:base==16&&p[i]>='A'&&p[i]<='F'?p[i]-'A'+10:-1;
                if(value<0)break;
                code=code*(unsigned)base+(unsigned)value;digits++;
            }
            if(digits&&p[i]==';'&&code<=0x10ffff){*input+=(int)i+1;return display_ascii(code);}
        }
        code='&';
    }
    size_t length=1;
    if(p[0]>=0xc2&&p[0]<=0xf4){
        size_t want=p[0]<0xe0?2:p[0]<0xf0?3:4;
        code=p[0]&((1u<<(7-want))-1u);
        for(size_t i=1;i<want;i++){
            if(!p[i]||(p[i]&0xc0)!=0x80){code='?';break;}
            code=(code<<6)|(p[i]&63);length++;
        }
        if(length!=want)code='?';
    }
    *input+=(int)length;return display_ascii(code);
}
#endif
