/* Small single-owner allocator for the extracted media stack, without JS/DOM. */
#include "tilefinch/budget.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef union { max_align_t alignment; size_t size; } Header;
void budget_init(Budget *b, size_t limit) { memset(b, 0, sizeof(*b)); b->limit=limit; }
static bool charge(Budget *b,size_t n) {
    if(b->current>b->limit || n>b->limit-b->current) { b->failure_count++; return false; }
    b->current+=n; if(b->current>b->peak)b->peak=b->current; return true;
}
void *budget_malloc_category(Budget *b,BudgetCategory c,size_t n) {
    (void)c;
    if(n>SIZE_MAX-sizeof(Header) || !charge(b,n+sizeof(Header))) return NULL;
    Header *h=malloc(n+sizeof(*h));
    if(!h){b->current-=n+sizeof(*h);return NULL;}
    h->size=n; return h+1;
}
void budget_free(Budget *b,void *p){if(p){Header *h=(Header*)p-1;b->current-=h->size+sizeof(*h);free(h);}}
void *budget_calloc_category(Budget *b,BudgetCategory c,size_t n,size_t s){
    if(s && n>SIZE_MAX/s)return NULL;
    void *p=budget_malloc_category(b,c,n*s);if(p)memset(p,0,n*s);return p;
}
void *budget_realloc_category(Budget *b,BudgetCategory c,void *p,size_t n){
    if(!p)return budget_malloc_category(b,c,n);
    if(!n){budget_free(b,p);return NULL;}
    Header *h=(Header*)p-1; size_t old=h->size;
    void *q=budget_malloc_category(b,c,n);
    if(q){memcpy(q,p,n<old?n:old);budget_free(b,p);}return q;
}
bool budget_reservation_acquire(BudgetReservation *r,Budget *b,BudgetCategory c,size_t n){
    (void)c;
    if(r->budget || n>SIZE_MAX-sizeof(Header) || !charge(b,n+sizeof(Header)))return false;
    Header *h=malloc(sizeof(*h));
    if(!h){b->current-=n+sizeof(*h);return false;}
    h->size=n;r->budget=b;r->token=h;return true;
}
void budget_reservation_release(BudgetReservation *r){
    if(r->budget){Header *h=r->token;r->budget->current-=h->size+sizeof(*h);free(h);memset(r,0,sizeof(*r));}
}
void tilefinch_platform_log_message(const char *s){
    FILE *f=fopen("media.log","a"); if(f){fprintf(f,"%s\n",s);fclose(f);}
}
/* No optional software decoder is installed in this hardware-only build. */
bool media_psp_swdec_backend_quarantined(void){return false;}
