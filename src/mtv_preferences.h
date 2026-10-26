#ifndef FLXTR_MTV_PREFERENCES_H
#define FLXTR_MTV_PREFERENCES_H
enum { MTV_GENRES=16 };
enum { MTV_FUSION_CUSTOM=100 };
static int mtv_genre;
static const char *const mtv_genre_names[MTV_GENRES]={"CUSTOM / MIX","POP","ROCK","HIP HOP","ELECTRONIC","METAL","JAZZ","COUNTRY","R&B / SOUL","K-POP","ANIME MUSIC","VGA + GENRE","KARAOKE + GENRE","ENKA","CITY POP","LATIN"};
static const char *const mtv_genre_queries[MTV_GENRES]={"official music videos 80s 90s pop rock","official pop music videos","official rock music videos","official hip hop music videos","official electronic dance music videos","official metal music videos","official jazz music videos","official country music videos","official R&B soul music videos","official k-pop music videos","official anime music videos","video game anime soundtrack remix","karaoke instrumental backing track","official enka music videos","official city pop music videos","official latin salsa bachata reggaeton music videos"};
static inline int mtv_fusion_valid(int genre){return genre==MTV_FUSION_CUSTOM||(genre>=0&&genre<MTV_GENRES&&genre!=11&&genre!=12);}
static inline const char *mtv_fusion_name(int genre){
    return genre==MTV_FUSION_CUSTOM?"CUSTOM STYLE FILE":genre>0&&genre<MTV_GENRES?mtv_genre_names[genre]:"WEIGHTED MIX";
}
#endif
