/* Minimal host bindings for the site's response decoder, compiled from WASM.
 * This implements the small set of imported values/callbacks, not a JS engine.
 * The upstream module and generated code are fetched at build time, not vendored.
 * Every worker has bounded object storage and dies after one lookup.
 */
#define _POSIX_C_SOURCE 200809L
#include "img_data.h"
#include "resolver_bridge.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct w2c_wbg {int unused;};
static struct w2c_wbg host;
static w2c_img__data module;
enum {UNDEFINED,STRING,WINDOW,DOCUMENT,SCREEN,NAVIGATOR,STORAGE,CANVAS,CONTEXT,ARRAY,DATE,PERFORMANCE,FN_GLOBAL,FN_QUEUE,FN_RESOLVE,FN_REJECT,CLOSURE,PROMISE};
typedef struct {int type;char *text;u32 a,b,c;int refs;} Object;
static Object objects[4096];static u32 object_count=132;
static char store_keys[16][96],store_values[16][256];static int store_count;
typedef struct {u32 function,argument,dependency,result;} Job;
static Job jobs[128];static int job_count;
static size_t string_bytes;
static jmp_buf failure;
static char error_text[160];
static void fail(const char *message){snprintf(error_text,sizeof(error_text),"%s",message);longjmp(failure,1);}
static Object *obj(u32 id){if(id>=object_count)fail("Invalid decoder object");return &objects[id];}
static u32 object(int type){if(object_count>=4096)fail("Decoder object limit");u32 i=object_count++;objects[i].type=type;objects[i].refs=1;return i;}
static unsigned char *memory(u32 p,u32 n){if((uint64_t)p+n>module.w2c_memory.size)fail("Decoder memory bounds");return module.w2c_memory.data+p;}
static char *copy_string(u32 p,u32 n){
    if(n>2*1024*1024||string_bytes+n>4*1024*1024)fail("Decoder string limit");
    char *s=malloc((size_t)n+1);if(!s)fail("Decoder allocation failed");memcpy(s,memory(p,n),n);s[n]=0;string_bytes+=n;return s;
}
static u32 string_object(u32 p,u32 n){u32 id=object(STRING);obj(id)->text=copy_string(p,n);return id;}
static u32 pass_string(const char *s){u32 n=(u32)strlen(s),p=w2c_img__data_0x5F_wbindgen_export_1(&module,n,1);memcpy(memory(p,n),s,n);return p;}
static void put32(u32 p,u32 n){unsigned char *m=memory(p,4);for(int i=0;i<4;i++)m[i]=(unsigned char)(n>>(8*i));}
static u32 get32(u32 p){unsigned char *m=memory(p,4);return (u32)m[0]|(u32)m[1]<<8|(u32)m[2]<<16|(u32)m[3]<<24;}
static void return_string(u32 p,const char *s){put32(p,s?pass_string(s):0);put32(p+4,s?(u32)strlen(s):0);}
static double clock_ms(int monotonic){struct timespec t;clock_gettime(monotonic?CLOCK_MONOTONIC:CLOCK_REALTIME,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0;}
static void storage_get(u32 out,u32 p,u32 n){
    for(int i=0;i<store_count;i++)if(strlen(store_keys[i])==n&&!memcmp(store_keys[i],memory(p,n),n)){return_string(out,store_values[i]);return;}
    return_string(out,NULL);
}
static void storage_set(u32 p,u32 n,u32 v,u32 len){
    if(n>=96||len>=256)fail("Decoder storage limit");
    int i;for(i=0;i<store_count;i++)if(strlen(store_keys[i])==n&&!memcmp(store_keys[i],memory(p,n),n))break;
    if(i==store_count){if(store_count==16)fail("Decoder storage limit");store_count++;}
    memcpy(store_keys[i],memory(p,n),n);store_keys[i][n]=0;memcpy(store_values[i],memory(v,len),len);store_values[i][len]=0;
}
static u32 promise(int state,u32 value){u32 id=object(PROMISE);obj(id)->a=state;obj(id)->b=value;return id;}
static void enqueue(u32 fn,u32 arg,u32 dependency,u32 result){if(job_count==128)fail("Decoder task limit");jobs[job_count++]=(Job){fn,arg,dependency,result};}
static void destroy_closure(u32 a,u32 b){
    wasm_rt_funcref_table_t *table=w2c_img__data_0x5F_wbindgen_export_3(&module);
    if(table->size<=36)fail("Decoder callback table");
    wasm_rt_funcref_t f=table->data[36];((void (*)(void*,u32,u32))f.func)(f.module_instance,a,b);
}
static u32 call(u32 fn,u32 arg){
    Object *o=obj(fn);
    if(o->type==FN_GLOBAL)return object(WINDOW);
    if(o->type==FN_QUEUE){enqueue(arg,128,0,0);return 128;}
    if(o->type==FN_RESOLVE||o->type==FN_REJECT){obj(o->a)->a=o->type==FN_RESOLVE?1:2;obj(o->a)->b=arg;return 128;}
    if(o->type==CLOSURE){u32 a=o->a;o->refs++;o->a=0;w2c_img__data_0x5F_wbindgen_export_5(&module,a,o->b,arg);if(--o->refs==0)destroy_closure(a,o->b);else o->a=a;return 128;}
    fail("Unsupported decoder callback");return 128;
}
static u32 new_promise(u32 a,u32 b){
    u32 p=promise(0,128),yes=object(FN_RESOLVE),no=object(FN_REJECT);obj(yes)->a=obj(no)->a=p;
    w2c_img__data_0x5F_wbindgen_export_6(&module,a,b,yes,no);return p;
}
static void pump(void){
    for(int turn=0;job_count&&turn<512;turn++){
        Job j=jobs[0];memmove(jobs,jobs+1,(size_t)(--job_count)*sizeof(Job));
        if(j.dependency){Object *p=obj(j.dependency);if(!p->a){enqueue(j.function,j.argument,j.dependency,j.result);continue;}if(p->a==2){if(j.result){obj(j.result)->a=2;obj(j.result)->b=p->b;}continue;}j.argument=p->b;}
        u32 value=call(j.function,j.argument);if(j.result){obj(j.result)->a=1;obj(j.result)->b=value;}
    }
    if(job_count)fail("Decoder tasks did not finish");
}
/* Generated function names/signatures only; all implementations are above or
 * small primitive bindings in tools/resolver_imports.py. Unknown imports fail. */
#include "resolver_imports.inc"
int resolver_init(void){
    memset(objects,0,sizeof(objects));object_count=132;objects[128].type=UNDEFINED;
    store_count=job_count=0;string_bytes=0;error_text[0]=0;
    wasm_rt_init();wasm2c_img__data_instantiate(&module,&host);return 0;
}
char *resolver_key(void){
    if(setjmp(failure))return NULL;
    u32 p=w2c_img__data_0x5F_wbindgen_add_to_stack_pointer(&module,(u32)-16);
    w2c_img__data_get_img_key(&module,p);
    u32 ptr=get32(p),n=get32(p+4),error=get32(p+8),bad=get32(p+12);
    w2c_img__data_0x5F_wbindgen_add_to_stack_pointer(&module,16);
    if(bad){Object *o=obj(error);fail(o->text?o->text:"Decoder key failed");}
    if(n!=64)fail("Unexpected decoder key length");
    char *key=copy_string(ptr,n);w2c_img__data_0x5F_wbindgen_export_4(&module,ptr,n,1);return key;
}
char *resolver_decode(const char *payload,const char *key){
    if(setjmp(failure))return NULL;
    u32 p=pass_string(payload),k=pass_string(key);
    u32 promise_id=w2c_img__data_process_img_data(&module,p,(u32)strlen(payload),k,(u32)strlen(key));pump();
    Object *result=obj(promise_id);
    if(result->type!=PROMISE||result->a==0)fail("Decoder did not return a result");
    Object *value=obj(result->b);
    if(result->a==2)fail(value->text?value->text:"Decoder rejected response");
    if(value->type!=STRING||!value->text)fail("Unexpected decoder response");
    return strdup(value->text);
}
const char *resolver_error(void){return error_text;}
void resolver_free(void){for(u32 i=132;i<object_count;i++)free(objects[i].text);wasm2c_img__data_free(&module);wasm_rt_free();}
