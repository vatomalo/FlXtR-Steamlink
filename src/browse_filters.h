#ifndef FLXTR_BROWSE_FILTERS_H
#define FLXTR_BROWSE_FILTERS_H
#define BROWSE_GENRES 12
#define BROWSE_ORDERS 4
static const char *const browse_genres[BROWSE_GENRES] __attribute__((unused))={"ALL GENRES","ACTION","ADVENTURE","COMEDY","DRAMA","FANTASY","HORROR","MYSTERY","ROMANCE","SCI-FI","THRILLER","ANIMATION"};
static const char *const kiss_genres[BROWSE_GENRES] __attribute__((unused))={"","action","adventure","comedy","drama","fantasy","horror","mystery","romance","sci-fi","thriller","kids"};
static const char *const browse_orders[BROWSE_ORDERS] __attribute__((unused))={"MOST POPULAR","TOP RATED","LATEST","TITLE A-Z"};
static const char *const kiss_orders[BROWSE_ORDERS] __attribute__((unused))={"popular","rating","latest","title"};
static const int movie_genres[BROWSE_GENRES] __attribute__((unused))={0,28,12,35,18,14,27,9648,10749,878,53,16};
static const int series_genres[BROWSE_GENRES] __attribute__((unused))={0,10759,10759,35,18,10765,-1,9648,-1,10765,-1,16};
#endif
