#ifndef FLXTR_SUBTITLE_LANGUAGES_H
#define FLXTR_SUBTITLE_LANGUAGES_H
/* Subtitle languages shared by the resolver, the shell and the player. The
 * position in this table is the saved "subtitles" setting and the number in
 * a playback request: never reorder, only append. */
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef struct {const char *code,*two,*name;int latin;} SubtitleLanguage;
static const SubtitleLanguage subtitle_language_table[]={
    {"off","","OFF",1},{"auto","","AUTO",1},{"eng","en","ENGLISH",1},{"nor","no","NORWEGIAN",1},{"spa","es","SPANISH",1},
    {"ara","ar","ARABIC",0},{"bul","bg","BULGARIAN",0},{"cat","ca","CATALAN",1},{"zho","zh","CHINESE",0},{"hrv","hr","CROATIAN",1},
    {"ces","cs","CZECH",1},{"dan","da","DANISH",1},{"nld","nl","DUTCH",1},{"est","et","ESTONIAN",1},{"fin","fi","FINNISH",1},
    {"fra","fr","FRENCH",1},{"deu","de","GERMAN",1},{"ell","el","GREEK",0},{"heb","he","HEBREW",0},{"hin","hi","HINDI",0},
    {"hun","hu","HUNGARIAN",1},{"isl","is","ICELANDIC",1},{"ind","id","INDONESIAN",1},{"ita","it","ITALIAN",1},{"jpn","ja","JAPANESE",0},
    {"kor","ko","KOREAN",0},{"lav","lv","LATVIAN",1},{"lit","lt","LITHUANIAN",1},{"msa","ms","MALAY",1},{"fas","fa","PERSIAN",0},
    {"pol","pl","POLISH",1},{"por","pt","PORTUGUESE",1},{"ron","ro","ROMANIAN",1},{"rus","ru","RUSSIAN",0},{"srp","sr","SERBIAN",1},
    {"slk","sk","SLOVAK",1},{"slv","sl","SLOVENIAN",1},{"swe","sv","SWEDISH",1},{"tam","ta","TAMIL",0},{"tel","te","TELUGU",0},
    {"tha","th","THAI",0},{"tur","tr","TURKISH",1},{"ukr","uk","UKRAINIAN",0},{"urd","ur","URDU",0},{"vie","vi","VIETNAMESE",1},
    {"fil","tl","FILIPINO",1},{"eus","eu","BASQUE",1},{"glg","gl","GALICIAN",1},{"ben","bn","BENGALI",0},{"mkd","mk","MACEDONIAN",0},
    {"sqi","sq","ALBANIAN",1},{"bos","bs","BOSNIAN",1},{"cym","cy","WELSH",1},{"gle","ga","IRISH",1},{"swa","sw","SWAHILI",1},
};
#define SUBTITLE_LANGUAGES ((int)(sizeof(subtitle_language_table)/sizeof(subtitle_language_table[0])))
/* The table position of a provider's language ("en-US", "eng", "English SDH",
 * "Português (BR)", "nb", ...), or -1. */
static inline int subtitle_language_index(const char *text){
    if(!text)return -1;
    while(*text==' ')text++;
    if(!*text)return -1;
    static const struct {const char *alias;int index;} aliases[]={
        {"nb",3},{"nn",3},{"nob",3},{"nno",3},{"bokmal",3},{"nynorsk",3},{"norsk",3},{"english",2},{"castilian",4},{"espanol",4},
        {"espa\xc3\xb1ol",4},{"latino",4},{"chi",8},{"mandarin",8},{"cantonese",8},{"cze",10},{"dut",12},{"flemish",12},
        {"fre",15},{"fran\xc3\xa7""ais",15},{"ger",16},{"deutsch",16},{"gre",17},{"ice",21},{"may",28},{"per",29},{"farsi",29},
        {"pob",31},{"brazil",31},{"portugu\xc3\xaas",31},{"br_pt",31},{"rum",32},{"scc",34},{"slo",35},{"svenska",37},{"dansk",11},
        {"suomi",14},{"tagalog",45},{"fil",45},{"baq",46},{"mac",49},{"alb",50},{"wel",52},{"iw",18},{"italiano",23},{"polski",30},
        {"ze_en",8},{"simplified",8},{"traditional",8}};
    size_t word=0;
    while(text[word]&&text[word]!='-'&&text[word]!=' '&&text[word]!='('&&text[word]!='['&&text[word]!='/'&&text[word]!=','&&text[word]!='.')word++;
    for(size_t i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++)
        if(strlen(aliases[i].alias)==word&&!strncasecmp(text,aliases[i].alias,word))return aliases[i].index;
    for(int i=2;i<SUBTITLE_LANGUAGES;i++){
        const SubtitleLanguage *l=&subtitle_language_table[i];
        if((word==2&&!strncasecmp(text,l->two,2))||(word==3&&!strncasecmp(text,l->code,3))||
           (word==strlen(l->name)&&!strncasecmp(text,l->name,word)))return i;
    }
    return -1;
}
/* Where the tracks found for a media URL are kept until it is played. */
static inline void subtitle_tracks_path(const char *url,char *out,size_t size){
    unsigned long long h=1469598103934665603ULL;
    for(const unsigned char *p=(const unsigned char*)url;*p;p++){h^=*p;h*=1099511628211ULL;}
    snprintf(out,size,"subtitle-tracks/%016llx.tsv",h);
}
#endif
