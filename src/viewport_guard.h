#ifndef FLXTR_VIEWPORT_GUARD_H
#define FLXTR_VIEWPORT_GUARD_H
#include <stdint.h>
/* Submission is asynchronous: keep checking through decoder startup, rather
 * than treating the first successful frame submission as stable geometry. */
typedef struct {int64_t until,next;int active,pending;unsigned attempts,repairs;} ViewportGuard;
static inline void viewport_guard_arm(ViewportGuard *guard,int64_t now,int result){
    *guard=(ViewportGuard){now+10000000,now+250000,result!=-2,result!=0,0,0};
}
static inline int viewport_guard_poll(ViewportGuard *guard,int64_t now,
                                     int (*matches)(void *),int (*apply)(void *),void *context){
    if(!guard->active||now<guard->next)return 0;
    if(now>guard->until){guard->active=0;return 0;}
    guard->next=now+250000;
    int check=guard->pending?0:matches(context);
    if(check==1)return 0;
    if(check<0){guard->pending=1;return 0;}
    int result=apply(context);guard->attempts++;
    guard->pending=result!=0;
    if(result==-2)guard->active=0;
    if(!result){guard->repairs++;return 1;}
    return -1;
}
#endif
