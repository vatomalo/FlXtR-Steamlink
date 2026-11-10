#ifndef FLXTR_GAME_TITLE_SEARCH_H
#define FLXTR_GAME_TITLE_SEARCH_H
#define PC_TITLE_WORDS 20
typedef struct {char word[PC_TITLE_WORDS][48];int count;} PcTitleWords;
static int pc_title_noise(const char *word){
    const char *noise[]={"p2p","repack","tenoke","codex","rune","flt","win64","win32","shipping"};
    for(size_t i=0;i<sizeof(noise)/sizeof(noise[0]);i++)if(!strcmp(word,noise[i]))return 1;
    return 0;
}
static void pc_title_words(const char *title,PcTitleWords *result){
    memset(result,0,sizeof(*result));char cleaned[512];size_t n=0,word_start=0;
    unsigned char previous=0;
    for(const unsigned char *p=(const unsigned char *)title;*p&&n+2<sizeof(cleaned);p++){
        unsigned char c=*p;
        if(c=='\''||c=='`')continue;
        if(c==0xe2&&p[1]==0x80&&(p[2]==0x98||p[2]==0x99)){p+=2;continue;}
        if(!isalnum(c)){if(n&&cleaned[n-1]!=' ')cleaned[n++]=' ';word_start=n;previous=0;continue;}
        int camel=islower(previous)&&isupper(c)&&(islower(p[1])||(strchr("IVX",c)&&(!p[1]||!isalpha(p[1])||strchr("IVX",p[1]))));
        int acronym=isupper(previous)&&isupper(c)&&p[1]&&islower(p[1]);
        int number=(isalpha(previous)&&isdigit(c)&&n-word_start>1)||(isdigit(previous)&&isalpha(c)&&!(n-word_start==2&&(cleaned[word_start]=='p'||cleaned[word_start]=='f')));
        int version=(c=='v'||c=='V')&&isalpha(previous)&&p[1]&&isdigit(p[1])&&p[2]&&isdigit(p[2])&&p[3]&&isdigit(p[3])&&p[4]&&isdigit(p[4]);
        int boundary=n&&cleaned[n-1]!=' '&&(camel||acronym||number||version);
        if(boundary){cleaned[n++]=' ';word_start=n;}
        cleaned[n++]=(char)tolower(c);previous=c;
    }
    cleaned[n]=0;char *save=NULL;
    for(char *word=strtok_r(cleaned," ",&save);word&&result->count<PC_TITLE_WORDS;word=strtok_r(NULL," ",&save)){
        size_t length=strlen(word);
        if(result->count&&((word[0]=='v'&&length>=4&&isdigit((unsigned char)word[1]))||!strcmp(word,"build")||!strcmp(word,"version")||!strcmp(word,"ver")))break;
        if(result->count>=2&&pc_title_noise(word))break;
        if(result->count&&length>=8&&strspn(word,"0123456789")==length)break;
        snprintf(result->word[result->count++],48,"%.47s",word);
    }
    if(result->count==1){
        const char *word=result->word[0];
        if(!strncmp(word,"ff",2)&&word[2]&&strspn(word+2,"ivx0123456789")==strlen(word+2)){
            char number[48];snprintf(number,sizeof(number),"%s",word+2);
            strcpy(result->word[0],"final");strcpy(result->word[1],"fantasy");strcpy(result->word[2],number);result->count=3;
        }else if(!strcmp(word,"p4g")){strcpy(result->word[0],"persona");strcpy(result->word[1],"4");strcpy(result->word[2],"golden");result->count=3;}
    }
}
static void pc_title_phrase(const PcTitleWords *words,const int *order,char *out,size_t cap){
    size_t used=0;out[0]=0;
    for(int i=0;i<words->count;i++){
        const char *word=words->word[order?order[i]:i];size_t n=strlen(word);
        if(used+n+(used?1:0)+1>cap)break;
        if(used)out[used++]=' ';
        memcpy(out+used,word,n+1);used+=n;
    }
}
static int pc_title_stop(const char *word){return !strcmp(word,"the")||!strcmp(word,"of")||!strcmp(word,"and")||!strcmp(word,"a");}
static int pc_title_extra(const char *word){
    const char *extra[]={"remastered","remaster","edition","deluxe","definitive","complete","ultimate","enhanced","intergrade","goty","golden","royal","directors","cut","collection","anniversary","hd","windows","pc","steam","marvels","tom","clancys","sid","meiers"};
    for(size_t i=0;i<sizeof(extra)/sizeof(extra[0]);i++)if(!strcmp(word,extra[i]))return 1;
    return pc_title_stop(word);
}
static int pc_title_number(const char *word){
    const char *roman[]={"i","ii","iii","iv","v","vi","vii","viii","ix","x","xi","xii","xiii","xiv","xv","xvi","xvii","xviii","xix","xx"};
    for(int i=0;i<20;i++)if(!strcmp(word,roman[i]))return i+1;
    return word[0]&&strlen(word)<=6&&strspn(word,"0123456789")==strlen(word)?atoi(word):0;
}
static int pc_title_word_equal(const char *a,const char *b){int x=pc_title_number(a),y=pc_title_number(b);return !strcmp(a,b)||(x>0&&x==y);}
static int pc_title_score(const char *wanted,const char *candidate){
    PcTitleWords a,b;pc_title_words(wanted,&a);pc_title_words(candidate,&b);
    if(!a.count||!b.count)return 10000;
    char joined_a[512]="",joined_b[512]="";
    for(int i=0;i<a.count;i++)strncat(joined_a,a.word[i],sizeof(joined_a)-strlen(joined_a)-1);
    for(int i=0;i<b.count;i++)strncat(joined_b,b.word[i],sizeof(joined_b)-strlen(joined_b)-1);
    if(!strcmp(joined_a,joined_b))return 0;
    int matched[PC_TITLE_WORDS]={0},required=0,extras=0;
    for(int i=0;i<a.count;i++){
        if(pc_title_stop(a.word[i]))continue;
        required++;int found=0;
        for(int j=0;j<b.count;j++)if(!matched[j]&&pc_title_word_equal(a.word[i],b.word[j])){matched[j]=1;found=1;break;}
        if(!found)return 10000;
    }
    if(!required)return 10000;
    for(int j=0;j<b.count;j++)if(!matched[j]){if(!pc_title_extra(b.word[j]))return 10000;extras++;}
    return 5+extras*2;
}
static int pc_title_queries(const char *title,char queries[8][256]){
    PcTitleWords words;pc_title_words(title,&words);int count=0,order[PC_TITLE_WORDS];
    for(int variant=0;variant<8&&words.count;variant++){
        for(int i=0;i<words.count;i++)order[i]=i;
        if(variant>=1&&variant<=4){for(int i=0;i<words.count;i++)order[i]=(i+variant)%words.count;}
        if(variant==5){for(int i=0;i<words.count;i++)order[i]=words.count-1-i;}
        if(variant==6){for(int i=0;i<words.count;i++)for(int j=i+1;j<words.count;j++)if(strcmp(words.word[order[i]],words.word[order[j]])>0){int tmp=order[i];order[i]=order[j];order[j]=tmp;}}
        if(variant==7&&words.count>1){order[0]=1;order[1]=0;}
        char query[256];pc_title_phrase(&words,order,query,sizeof(query));int duplicate=0;
        for(int i=0;i<count;i++)if(!strcmp(queries[i],query))duplicate=1;
        if(!duplicate)snprintf(queries[count++],256,"%s",query);
    }
    return count;
}
#endif
