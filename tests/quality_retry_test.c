#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
 quality_setting=1;auto_count=2;auto_index=0;auto_quality_missing=1;
 assert(!auto_retry_quality());auto_index=2;auto_quality_missing=0;
 assert(!auto_retry_quality());auto_quality_missing=1;assert(auto_retry_quality());assert(auto_retry_height==1080&&auto_index==0&&!auto_quality_missing);
 auto_quality_missing=1;auto_index=2;assert(!auto_retry_quality());
 auto_retry_height=0;quality_setting=0;assert(auto_retry_quality()&&auto_retry_height==720);auto_quality_missing=1;auto_index=2;assert(auto_retry_quality()&&auto_retry_height==1080);
 auto_retry_height=0;auto_quality_missing=1;tv_active=1;assert(!auto_retry_quality());tv_active=0;auto_count=0;assert(!auto_retry_quality());
 puts("Quality retry: only after incompatibility, bounded 480/720/1080 rounds; TV unchanged");return 0;
}
