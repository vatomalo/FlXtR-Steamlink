#ifndef FLXTR_SOM_ART_GEN_H
#define FLXTR_SOM_ART_GEN_H
/* The weapon masks and hero move frames, made from the running ROM: a port of
 * tools/som_hero_gfx.py, som_katana_art.py, som_weapon_mask.py (--kusarigama)
 * and som_cartwheel.py, byte for byte (tools/test_som_art_gen.py). Change both
 * sides together. No game art is stored here or shipped. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __clang__
#pragma STDC FP_CONTRACT OFF
#endif
typedef struct {const uint8_t *rom;size_t size;int bad;} SomArtRom;
typedef struct {uint8_t *data;size_t n,cap;unsigned entries;int bad;} SomArtOut;
typedef struct {int x,y,hflip,vflip;int32_t tile[4];} SomArtPiece;   /* tile -1 = empty */
typedef struct {uint8_t flags,frame,ticks;} SomArtStep;
typedef struct {uint32_t lists,pool,palette,ordinary;} SomArtActor;
typedef struct {int w,h,dx,dy;} SomArtBox;
#define SOM_ART_CELL 96
#define SOM_ART_CANVAS 224
#define SOM_ART_HEIGHT 14
#define SOM_ART_POSES 0x37CCu
#define SOM_ART_NO_WEAPON (-1)
#define SOM_ART_SWORD 1
#define SOM_ART_WHIP 4

static unsigned som_art_8(SomArtRom *g,uint32_t at){
    if(at>=g->size){g->bad=1;return 0;}
    return g->rom[at];
}
static unsigned som_art_16(SomArtRom *g,uint32_t at){return som_art_8(g,at)|som_art_8(g,at+1u)<<8;}
static uint32_t som_art_off(unsigned bank,uint32_t addr){return (uint32_t)(bank-0xC0u)<<16|(addr&0xFFFFu);}
static unsigned som_art_w(SomArtRom *g,unsigned bank,uint32_t addr){return som_art_16(g,som_art_off(bank,addr));}
static int som_art_sext7(unsigned v){return v&0x40u?(int)v-128:(int)v;}

static void som_art_put(SomArtOut *o,const void *data,size_t n){
    if(o->bad||!n)return;
    if(o->n+n>o->cap){
        size_t cap=o->cap?o->cap:65536u;
        while(cap<o->n+n)cap*=2u;
        uint8_t *grown=realloc(o->data,cap);
        if(!grown){o->bad=1;return;}
        o->data=grown;o->cap=cap;
    }
    memcpy(o->data+o->n,data,n);o->n+=n;
}
static void som_art_start(SomArtOut *o){memset(o,0,sizeof(*o));som_art_put(o,"SOMWORK1\0\0",10u);}
static int som_art_finish(SomArtOut *o,const SomArtRom *g){
    if(o->bad||g->bad||!o->entries||o->entries>65535u){free(o->data);memset(o,0,sizeof(*o));return 0;}
    o->data[8]=(uint8_t)o->entries;o->data[9]=(uint8_t)(o->entries>>8);
    return 1;
}
static void som_art_entry(SomArtOut *o,unsigned kind,unsigned weapon,unsigned facing,unsigned frames,
                          const uint16_t *colours){
    uint8_t h[44]={(uint8_t)kind,(uint8_t)weapon,(uint8_t)facing,(uint8_t)frames};
    for(unsigned i=0;i<20u;i++){h[4u+2u*i]=(uint8_t)colours[i];h[5u+2u*i]=(uint8_t)(colours[i]>>8);}
    som_art_put(o,h,44u);o->entries++;
}
/* ---- ROM tables ---- */
static SomArtActor som_art_actor(SomArtRom *g,unsigned kind){
    SomArtActor a;
    if(kind<3u){                                  /* boy, girl, sprite */
        a.lists=kind*0x200u;a.pool=0x150000u+0x8000u*kind;
        a.palette=0x81F00u+0x1Eu*kind;a.ordinary=SOM_ART_POSES;
    }else{
        const uint32_t e=0x100000u+16u*kind;
        a.lists=som_art_16(g,e+2u);
        a.pool=(0x15u+(som_art_8(g,e+1u)>>2&7u))<<16|(som_art_16(g,e)&0x3FFu)<<6;
        a.palette=0x81000u+30u*kind;a.ordinary=som_art_16(g,e+5u);
    }
    return a;
}
static unsigned som_art_script(SomArtRom *g,uint32_t table,unsigned anim,unsigned dir,SomArtStep *out){
    uint32_t p=som_art_w(g,0xD1,table+(anim*3u+dir)*2u);
    unsigned n=0;
    for(unsigned k=0;k<256u;k++){
        const unsigned b=som_art_8(g,som_art_off(0xD1,p));
        if(b&0x80u){
            if(b==0xFFu)break;
            if(b==0xF1u){
                const uint32_t to=som_art_w(g,0xD1,p+1u);
                if(to<=p)break;
                p=to;continue;
            }
            p+=b==0xF8u?4u:(b==0xF9u||(b>=0xF0u&&b<=0xF7u))?3u:1u;
            continue;
        }
        out[n].flags=(uint8_t)b;out[n].frame=(uint8_t)som_art_8(g,som_art_off(0xD1,p+1u));
        out[n++].ticks=(uint8_t)((b&7u)<4u?(b&7u)+1u:1u);
        p+=2u;
    }
    return n;
}
static uint32_t som_art_attacks(int weapon){return 0x3040u+240u*(unsigned)weapon;}
static unsigned som_art_pieces(SomArtRom *g,unsigned kind,int weapon,unsigned frame,unsigned flags,SomArtPiece *out){
    const SomArtActor a=som_art_actor(g,kind);
    const unsigned base=som_art_16(g,weapon<0?0x10FFF0u:0x10FFD0u+2u*(unsigned)weapon);
    const unsigned desc=som_art_w(g,0xD1,base+2u*frame);
    const unsigned flips=((desc&0xFFu)^(flags<<1))&0xC0u;
    const uint32_t at=som_art_w(g,0xD2,a.lists+2u*(desc>>8));
    unsigned count=som_art_8(g,som_art_off(0xD2,at))&15u,n=0;
    if(!count)count=1;
    uint32_t src[80],c=at+1u;
    while(n<4u*count){
        const unsigned w=som_art_w(g,0xD2,c),start=(w<<1)&0x7FE0u;
        c+=2u;
        if(w&0x8000u)for(unsigned k=0,run=(w&15u)+1u;k<run;k++)src[n++]=w&0x4000u?start+0x20u*k:start;
        else src[n++]=(w&0x3FF0u)<<1;
    }
    for(unsigned k=0;k<count;k++){
        const unsigned w=som_art_w(g,0xD2,c);
        int y=som_art_sext7(w&0x7Fu),x=som_art_sext7(w>>8&0x7Fu);
        c+=2u;
        if(flips&0x40u)x=-x-16;
        if(flips&0x80u)y=-y-16;
        out[k].x=x;out[k].y=y;
        out[k].hflip=(int)((w>>15&1u)^(flips>>6&1u));out[k].vflip=(int)(flips>>7&1u);
        for(unsigned q=0;q<4u;q++)out[k].tile[q]=src[4u*k+q]==0x7FE0u?-1:(int32_t)(a.pool+src[4u*k+q]);
    }
    return count;
}
static unsigned som_art_group(SomArtRom *g,unsigned row){
    const uint32_t r=som_art_off(0xD0,0x1000u+12u*row);
    return som_art_8(g,r+3u)<4u?som_art_8(g,r):som_art_8(g,r+3u)+0x12u;
}
static void som_art_weapon_tiles(SomArtRom *g,unsigned row,uint8_t *out){
    const uint32_t start=som_art_off(0xD4,24u*som_art_w(g,0xD4,0xC000u+4u*som_art_group(g,row)));
    for(unsigned t=0;t<64u;t++)for(unsigned y=0;y<8u;y++){
        const uint32_t s=start+24u*t;
        const unsigned a=som_art_8(g,s+2u*y),b=som_art_8(g,s+2u*y+1u),c=som_art_8(g,s+16u+y),keep=~(b&c);
        uint8_t *o=out+32u*t;
        o[2u*y]=(uint8_t)(a&keep);o[2u*y+1u]=(uint8_t)(b&keep);
        o[16u+2u*y]=(uint8_t)(c&keep);o[17u+2u*y]=0;
    }
}
static uint32_t som_art_weapon_list(SomArtRom *g,unsigned row,unsigned frame){
    return som_art_w(g,0xD4,som_art_w(g,0xD4,0xC000u+4u*som_art_group(g,row)+2u)+2u*frame);
}
static unsigned som_art_weapon_pieces(SomArtRom *g,unsigned row,unsigned frame,unsigned flags,SomArtPiece *out){
    const uint32_t at=som_art_weapon_list(g,row,frame);
    const unsigned header=som_art_8(g,som_art_off(0xD4,at)),count=header&15u;
    if(!count||count>=11u)return 0;
    int32_t tiles[128];
    uint32_t c=at+1u;
    unsigned n=0;
    while(n<4u*count){
        const unsigned b=som_art_8(g,som_art_off(0xD4,c));
        if(!(b&0x80u)){c++;tiles[n++]=b==0x7Fu?-1:(int32_t)b;continue;}
        const unsigned first=som_art_8(g,som_art_off(0xD4,c+1u))&0x7Fu;
        c+=2u;
        for(unsigned k=0,run=(b&0x3Fu)+1u;k<run;k++)
            tiles[n++]=b&0x40u?(int32_t)(first+k):first==0x7Fu?-1:(int32_t)first;
    }
    const unsigned flips=flags<<1&0xC0u;
    const int scale=header&0x10u?2:1;
    for(unsigned k=0;k<count;k++){
        const unsigned lo=som_art_8(g,som_art_off(0xD4,c)),hi=som_art_8(g,som_art_off(0xD4,c+1u));
        int y=som_art_sext7(lo&0x7Fu)*scale,x=som_art_sext7(hi&0x7Fu)*scale;
        c+=2u;
        if(flips&0x80u)y=-y-16;
        if(flips&0x40u)x=-x-16;
        out[k].x=x;out[k].y=y;out[k].hflip=(int)(hi>>7^(flips>>6&1u));out[k].vflip=(int)(lo>>7^flips>>7);
        memcpy(out[k].tile,tiles+4u*k,sizeof(out[k].tile));
    }
    return count;
}
static void som_art_weapon_colours(SomArtRom *g,unsigned row,uint16_t *five){
    const unsigned id=som_art_8(g,som_art_off(0xD0,0x1000u+12u*row)+4u);
    for(unsigned k=0;k<5u;k++)five[k]=(uint16_t)(som_art_w(g,0xD1,0xF600u+10u*id+2u*k)&0x7FFFu);
}
static void som_art_colours(SomArtRom *g,unsigned kind,uint16_t *fifteen){
    const uint32_t at=som_art_actor(g,kind).palette;
    for(unsigned k=0;k<15u;k++)fifteen[k]=(uint16_t)(som_art_16(g,at+2u*k)&0x7FFFu);
}
/* Draw pieces; `tiles` = 64 expanded weapon tiles, or NULL for ROM offsets.
 * 0 = a weapon tile outside the 64. */
static int som_art_render(SomArtRom *g,const SomArtPiece *p,unsigned n,uint8_t *image,int size,
                          int ox,int oy,const uint8_t *tiles,unsigned base){
    for(;n--;p++)for(unsigned q=0;q<4u;q++){
        if(p->tile[q]<0)continue;
        uint8_t d[32];
        if(tiles){
            if(p->tile[q]>=64)return 0;
            memcpy(d,tiles+32*p->tile[q],32u);
        }else for(unsigned k=0;k<32u;k++)d[k]=(uint8_t)som_art_8(g,(uint32_t)p->tile[q]+k);
        const int qx=q&1u?8:0,qy=q&2u?8:0;
        for(int ty=0;ty<8;ty++)for(int tx=0;tx<8;tx++){
            const unsigned s=7u-(unsigned)tx;
            const unsigned c=(d[2*ty]>>s&1u)|(d[2*ty+1]>>s&1u)<<1|(d[16+2*ty]>>s&1u)<<2|(d[17+2*ty]>>s&1u)<<3;
            const int x=ox+p->x+(p->hflip?15-(qx+tx):qx+tx),y=oy+p->y+(p->vflip?15-(qy+ty):qy+ty);
            if(c&&x>=0&&y>=0&&x<size&&y<size)image[y*size+x]=(uint8_t)(c+base);
        }
    }
    return 1;
}
typedef struct {int x0,y0,x1,y1;} SomArtWindow;      /* half open */
static int som_art_box_in(const uint8_t *image,int size,SomArtWindow w,int ox,int oy,SomArtBox *b){
    int x0=size,x1=-1,y0=size,y1=-1;
    for(int y=w.y0;y<w.y1;y++)for(int x=w.x0;x<w.x1;x++)if(image[y*size+x]){
        if(x<x0)x0=x;
        if(x>x1)x1=x;
        if(y<y0)y0=y;
        y1=y;
    }
    memset(b,0,sizeof(*b));
    if(x1<0)return 0;
    b->w=x1+1-x0;b->h=y1+1-y0;b->dx=x0-ox;b->dy=y0-oy-SOM_ART_HEIGHT;
    return 1;
}
static int som_art_box(const uint8_t *image,int size,int ox,int oy,SomArtBox *b){
    const SomArtWindow all={0,0,size,size};
    return som_art_box_in(image,size,all,ox,oy,b);
}
static void som_art_frame(SomArtOut *o,const uint8_t *image,int size,int ox,int oy,unsigned ticks,const SomArtBox *b){
    const uint8_t h[5]={(uint8_t)ticks,(uint8_t)b->dx,(uint8_t)b->dy,(uint8_t)b->w,(uint8_t)b->h};
    som_art_put(o,h,5u);
    for(int y=0;y<b->h;y++)
        som_art_put(o,image+(b->dy+oy+SOM_ART_HEIGHT+y)*size+b->dx+ox,(size_t)b->w);
}
/* ---- The sword styles (som_katana_art.py) ---- */
enum {SOM_ART_TSUBA,SOM_ART_CUP,SOM_ART_CROSS,SOM_ART_BASKET,SOM_ART_WINGS,SOM_ART_RING,SOM_ART_STAKE};
typedef struct {double length,grip,sag,base,tip,point,guard;int kind,edges;unsigned pack_kind;double belly,wave;int teeth;} SomArtBlade;
static const SomArtBlade som_art_blades[]={
    {0.42,3,0.2,1.00,1.00,2.0,2.0,SOM_ART_TSUBA,1,0xD4,0,0,0},    /* Tanto */
    {1.75,7,3.0,1.00,1.00,2.0,2.0,SOM_ART_TSUBA,1,0xD5,0,0,0},    /* Dai Katana */
    {1.15,5,1.6,1.00,1.00,2.0,2.0,SOM_ART_TSUBA,1,0xD7,0,0,0},    /* Katana */
    {1.35,3,0.0,0.55,0.55,1.0,2.2,SOM_ART_CUP,2,0xD8,0,0,0},      /* Rapier */
    {1.25,5,0.0,1.55,1.35,3.0,4.0,SOM_ART_CROSS,2,0xD9,0,0,0},    /* Longsword */
    {1.75,7,0.0,2.50,1.90,4.0,5.0,SOM_ART_CROSS,2,0xDA,0,0,0},    /* Claymore */
    {2.00,6,0.0,2.60,2.00,7.0,4.6,SOM_ART_WINGS,2,0xDB,0,0,0},   /* Zweihander */
    {2.15,7,0.0,3.60,3.00,6.0,5.2,SOM_ART_CROSS,2,0xDC,0,0,0},   /* Greatsword */
    {2.40,8,0.8,3.60,5.20,7.0,4.2,SOM_ART_CROSS,1,0xDD,0,0,0},   /* Colossus */
    {0.50,3,0.0,1.10,0.90,3.0,2.2,SOM_ART_CROSS,2,0xDE,0,0,0},    /* Dagger */
    {0.95,3,2.6,1.10,1.90,3.5,2.0,SOM_ART_BASKET,1,0xDF,0,0,0},   /* Cutlass */
    {1.50,5,0.0,1.05,0.50,2.0,3.0,SOM_ART_CROSS,2,0xE0,0,0,0},    /* Estoc */
    {0.40,3,0.0,0.70,0.70,3.0,1.0,SOM_ART_RING,2,0xE1,1.1,0.0,0},   /* Kunai */
    {0.88,5,0.0,1.00,1.00,1.0,2.4,SOM_ART_TSUBA,1,0xE2,0.0,0.0,0},   /* Ninjato */
    {0.62,3,0.0,1.80,1.40,6.0,0.8,SOM_ART_STAKE,2,0xE3,0.0,0.0,0},   /* wooden stake */
    {0.66,3,0.0,1.30,0.90,4.0,3.0,SOM_ART_WINGS,2,0xE4,0.5,0.0,0},   /* Silver Dagger */
    {0.66,4,-2.8,0.90,1.00,3.0,1.2,SOM_ART_TSUBA,1,0xE5,1.5,0.0,0},   /* Kukri */
    {0.95,3,0.0,1.50,1.20,2.0,3.0,SOM_ART_CROSS,2,0xE6,0.0,0.0,1},   /* Rusty Sword */
    {0.95,3,0.0,2.50,2.30,4.0,3.2,SOM_ART_CROSS,2,0xE7,0.0,0.0,0},   /* Broad Sword */
    {1.12,5,0.0,1.50,1.50,5.0,3.4,SOM_ART_WINGS,2,0xE8,0.0,0.0,0},   /* Herald Sword */
    {1.35,5,0.0,2.10,1.30,6.0,4.6,SOM_ART_WINGS,2,0xE9,0.3,0.0,0},   /* Excalibur */
    {1.60,7,1.0,2.20,3.60,5.0,3.0,SOM_ART_CROSS,1,0xEA,0.0,0.0,0},   /* Gigas Sword */
    {1.60,7,0.0,1.70,1.30,4.0,4.0,SOM_ART_CROSS,2,0xEB,0.0,1.0,0},   /* Dragon Buster */
    {1.40,5,0.0,1.20,0.80,6.0,5.0,SOM_ART_WINGS,2,0xEC,1.4,0.0,0},   /* Mana Sword */
};
/* A wooden stake keeps its own colours with any sword. */
static const uint16_t som_art_wood[5]={28u|21u<<5|12u<<10,22u|14u<<5|7u<<10,15u|9u<<5|4u<<10,6u|3u<<5|1u<<10,30u|27u<<5|20u<<10};
/* Python's sum(): compensated, so the point order does not matter. */
typedef struct {double s,c;} SomArtSum;
static void som_art_add(SomArtSum *a,double x){
    const double t=a->s+x;
    a->c+=fabs(a->s)>=fabs(x)?(a->s-t)+x:(x-t)+a->s;
    a->s=t;
}
static double som_art_total(const SomArtSum *a){return a->c!=0.0?a->s+a->c:a->s;}
typedef struct {double gx,gy,ux,uy,side;} SomArtHilt;
typedef struct {uint16_t pt[SOM_ART_CANVAS*SOM_ART_CANVAS],group[SOM_ART_CANVAS*SOM_ART_CANVAS];
                uint8_t seen[SOM_ART_CANVAS*SOM_ART_CANVAS];
                double t[4096],across[4096];} SomArtScratch;
static double som_art_spread(const double *t,const double *across,unsigned n,double at,double *mid){
    SomArtSum sum={0.0,0.0};
    double lo=0,hi=0;
    unsigned k=0;
    for(unsigned i=0;i<n;i++){
        if(fabs(t[i]-at)>0.8)continue;
        const double v=across[i];
        if(!k||v<lo)lo=v;
        if(!k||v>hi)hi=v;
        som_art_add(&sum,v);k++;
    }
    *mid=k?som_art_total(&sum)/(double)k:0.0;
    return k?hi-lo:0.0;
}
static int som_art_hilt(const uint8_t *image,SomArtWindow w,SomArtScratch *s,SomArtHilt *h){
    const int W=SOM_ART_CANVAS;
    unsigned n=0;
    memset(s->seen,0,sizeof(s->seen));
    for(int row=w.y0;row<w.y1;row++)for(int at=row*W+w.x0;at<row*W+w.x1;at++)if(image[at]>=1u&&image[at]<=4u&&!s->seen[at]){
        unsigned m=1,k=0;
        s->pt[0]=(uint16_t)at;s->seen[at]=1;
        while(k<m){
            const int x=s->pt[k]%W,y=s->pt[k]/W;
            k++;
            for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++){
                const int nx=x+i,ny=y+j,q=ny*W+nx;
                if(nx<0||ny<0||nx>=W||ny>=W||s->seen[q]||image[q]<1u||image[q]>4u)continue;
                s->seen[q]=1;s->pt[m++]=(uint16_t)q;
            }
        }
        if(m>=n){n=m;memcpy(s->group,s->pt,m*sizeof(*s->pt));}
    }
    if(n<20u||n>4096u)return 0;
    const uint16_t *p=s->group;
    double sx=0,sy=0;
    for(unsigned i=0;i<n;i++){sx+=p[i]%W;sy+=p[i]/W;}
    const double mx=sx/(double)n,my=sy/(double)n;
    SomArtSum xx={0,0},yy={0,0},xy={0,0};
    for(unsigned i=0;i<n;i++){
        const double dx=(double)(p[i]%W)-mx,dy=(double)(p[i]/W)-my;
        som_art_add(&xx,dx*dx);som_art_add(&yy,dy*dy);som_art_add(&xy,dx*dy);
    }
    const double a=0.5*atan2(2.0*som_art_total(&xy),som_art_total(&xx)-som_art_total(&yy));
    const double ux=cos(a),uy=sin(a);
    double lo=0,hi=0;
    for(unsigned i=0;i<n;i++){
        const double dx=(double)(p[i]%W)-mx,dy=(double)(p[i]/W)-my,t=dx*ux+dy*uy;
        s->t[i]=t;s->across[i]=-dx*uy+dy*ux;
        if(!i||t<lo)lo=t;
        if(!i||t>hi)hi=t;
    }
    if(hi-lo<8.0)return 0;
    double at=0,wide=-1,mid=0;
    for(int i=0,steps=(int)((hi-lo)/0.5)+1;i<steps;i++){
        const double q=lo+0.5*i;
        double m;
        const double w=som_art_spread(s->t,s->across,n,q,&m);
        if(w>wide||(w==wide&&fabs(q)>fabs(at))){wide=w;at=q;mid=m;}
    }
    if(wide<4.0)return 0;                          /* no guard to be seen */
    const double sign=at>(lo+hi)/2.0?-1.0:1.0;
    h->gx=mx+ux*at-uy*mid;h->gy=my+uy*at+ux*mid;
    double bx=0,by=0;
    unsigned k=0;
    for(unsigned i=0;i<n;i++){
        if((s->t[i]-at)*sign>2.5){bx+=p[i]%W;by+=p[i]/W;k++;}
    }
    if(k<8u)return 0;
    bx=bx/(double)k-h->gx;by=by/(double)k-h->gy;
    const double d=sqrt(bx*bx+by*by);
    h->ux=bx/d;h->uy=by/d;
    /* The edge leads the swing, so the back faces the trail. */
    h->side=h->ux>0.0||(h->ux==0.0&&h->uy<0.0)?1.0:-1.0;
    double tx=0,ty=0;
    k=0;
    for(int row=w.y0;row<w.y1;row++)for(int q=row*W+w.x0;q<row*W+w.x1;q++)if(image[q]==5u){tx+=q%W;ty+=q/W;k++;}
    if(k){
        tx=tx/(double)k-h->gx;ty=ty/(double)k-h->gy;
        const double cross=-tx*h->uy+ty*h->ux;
        if(fabs(cross)>1.0)h->side=cross>0.0?-1.0:1.0;
    }
    return 1;
}
static unsigned som_art_blade_pixel(const SomArtBlade *b,double length,double t,double v){
    const double thick=b->kind==SOM_ART_CUP?1.4:0.75,av=fabs(v);
    if(fabs(t)<=thick&&av<=b->guard)return av>b->guard-0.9?3u:2u;
    if(b->kind==SOM_ART_STAKE&&-b->grip<=t&&t<0.0&&av<=1.5)return v>0.0?2u:3u;
    if(b->kind==SOM_ART_BASKET&&-b->grip-0.5<=t&&t<-0.5&&1.7<=v&&v<=2.6)return 2u;
    if(-b->grip<=t&&t<0.0&&av<=0.75)return (int)(-t)%3==1?3u:4u;
    if(b->kind==SOM_ART_WINGS&&0.0<t&&t<=2.4&&b->guard-1.3<=av&&av<=b->guard)return 3u;
    if(b->kind==SOM_ART_RING){
        const double at=t+b->grip+1.6,far=sqrt(at*at+v*v);
        if(0.6<=far&&far<=1.9)return 2u;
    }
    if((b->kind==SOM_ART_CROSS||b->kind==SOM_ART_WINGS)&&b->grip>4.0&&fabs(t+b->grip+1.0)<=1.1&&av<=1.2)return 2u;
    if(!(0.0<t&&t<=length))return 0;
    const double left=length-t;
    double half=b->base+(b->tip-b->base)*t/length+b->belly*4.0*(t/length)*(1.0-t/length);
    if(b->teeth&&(int)t%4==2)half-=0.7;
    if(b->edges==1){
        if(!(-0.95<=v&&v<=half+0.05)||(left<b->point&&v>left-0.6))return 0;
        return v>half-1.0?1u:t<length*0.55?2u:3u;
    }
    if(left<b->point)half*=left/b->point>0.35?left/b->point:0.35;
    if(av>half)return 0;
    return half<0.8||v>0.45?1u:v<-0.45||half<1.3?2u:3u;
}
/* `w` = where the native weapon is; on return, where the drawn one is. */
static int som_art_blade(const uint8_t *image,const SomArtHilt *h,const SomArtBlade *b,uint8_t *out,uint8_t *core,SomArtWindow *w){
    const int W=SOM_ART_CANVAS;
    const double length=15.0*b->length;
    const double bend=fabs(b->sag)*1.4+b->wave;
    const int reach=(int)(length+fabs(b->sag)+b->grip+8.0);
    /* Only the turned rectangle the blade can ink: its corners' box, then per
     * row the pixels between its long edges. The bounds are generous. */
    const double ta=-b->grip-4.5,tb=length+2.0,vb=7.5+bend;
    double lx=h->gx,rx=h->gx,uy=h->gy,dy_=h->gy;
    for(int k=0;k<4;k++){
        const double t=k&1?tb:ta,v=k&2?vb:-vb;
        const double cx=h->gx+t*h->ux-v*h->uy,cy=h->gy+t*h->uy+v*h->ux;
        if(cx<lx)lx=cx;
        if(cx>rx)rx=cx;
        if(cy<uy)uy=cy;
        if(cy>dy_)dy_=cy;
    }
    int x0=(int)h->gx-reach,y0=(int)h->gy-reach,x1=(int)h->gx+reach+1,y1=(int)h->gy+reach+1;
    if((int)floor(lx)>x0)x0=(int)floor(lx);
    if((int)floor(uy)>y0)y0=(int)floor(uy);
    if((int)ceil(rx)+1<x1)x1=(int)ceil(rx)+1;
    if((int)ceil(dy_)+1<y1)y1=(int)ceil(dy_)+1;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>W)x1=W;
    if(y1>W)y1=W;
    if(x1<=x0||y1<=y0)return 0;
    int any=0;
    if(x0-1<w->x0)w->x0=x0>0?x0-1:0;
    if(y0-1<w->y0)w->y0=y0>0?y0-1:0;
    if(x1+1>w->x1)w->x1=x1<W?x1+1:W;
    if(y1+1>w->y1)w->y1=y1<W?y1+1:W;
    for(int y=w->y0;y<w->y1;y++)for(int x=w->x0;x<w->x1;x++){
        out[y*W+x]=image[y*W+x]==5u?5u:0u;core[y*W+x]=0;
    }
    for(int y=y0;y<y1;y++){
        const double dy=y-h->gy;
        double from=x0,to=x1-1;
        if(fabs(h->uy)>0.05){                /* |-dx*uy+dy*ux| <= vb */
            const double mid=h->gx+dy*h->ux/h->uy,half=vb/fabs(h->uy);
            if(mid-half>from)from=mid-half;
            if(mid+half<to)to=mid+half;
        }
        if(fabs(h->ux)>0.05){                /* ta <= dx*ux+dy*uy <= tb */
            double a=h->gx+(ta-dy*h->uy)/h->ux,c=h->gx+(tb-dy*h->uy)/h->ux;
            if(a>c){const double k=a;a=c;c=k;}
            if(a>from)from=a;
            if(c<to)to=c;
        }
        for(int x=(int)from,last=(int)to;x<=last;x++){
        const double dx=x-h->gx,t=dx*h->ux+dy*h->uy;
        if(t>length+1.5||t<-b->grip-4.0)continue;       /* past both ends */
        const double v0=(-dx*h->uy+dy*h->ux)*h->side;
        if(v0>6.0+bend||v0<-6.0-bend)continue;
        const double bow=(t>0.0?t:0.0)/length;
        double v=v0+b->sag*(bow*bow);
        if(b->wave!=0.0&&t>0.0){
            const double turn=t/6.0-floor(t/6.0);
            v+=b->wave*(1.0-4.0*fabs(turn-0.5));
        }
        const unsigned c=som_art_blade_pixel(b,length,t,v);
        if(c){core[y*W+x]=(uint8_t)c;any=1;}
        }
    }
    if(!any)return 0;
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)if(core[y*W+x]){
        if(x+1<W&&!core[y*W+x+1])out[y*W+x+1]=4u;
        if(x>0&&!core[y*W+x-1])out[y*W+x-1]=4u;
        if(y+1<W&&!core[(y+1)*W+x])out[(y+1)*W+x]=4u;
        if(y>0&&!core[(y-1)*W+x])out[(y-1)*W+x]=4u;
    }
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)if(core[y*W+x])out[y*W+x]=core[y*W+x];
    return 1;
}
/* The native weapon at twice its size about the hands (Scale2x), outline thin. */
static int som_art_at(const uint8_t *image,int x,int y){
    return x>=0&&y>=0&&x<SOM_ART_CANVAS&&y<SOM_ART_CANVAS?image[y*SOM_ART_CANVAS+x]:0;
}
static int som_art_giant(const uint8_t *image,const uint8_t *body,uint8_t *big,uint8_t *out,SomArtWindow *w){
    const int W=SOM_ART_CANVAS;
    int x0=W,y0=W,x1=0,y1=0;
    long sx=0,sy=0,n=0,bx=0,by=0,bn=0,any=0,best=-1;
    int ax=0,ay=0;
    for(int y=0;y<W;y++)for(int x=0;x<W;x++){
        const int c=image[y*W+x];
        if(body[y*W+x]){bx+=x;by+=y;bn++;}
        if(c){
            if(x<x0)x0=x;
            if(y<y0)y0=y;
            if(x+1>x1)x1=x+1;
            y1=y+1;
        }
        if(c<1||c>4)continue;
        any=1;
        int held=0;
        for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++)held|=som_art_at(body,x+i,y+j);
        if(held){sx+=x;sy+=y;n++;}
    }
    if(!any)return 0;
    if(n){ax=(int)(sx/n);ay=(int)(sy/n);}
    else{
        if(!bn)return 0;
        bx/=bn;by/=bn;
        for(int y=0;y<W;y++)for(int x=0;x<W;x++)if(image[y*W+x]>=1&&image[y*W+x]<=4){
            const long d=(x-bx)*(x-bx)+(y-by)*(y-by);
            if(best<0||d<best){best=d;ax=x;ay=y;}
        }
    }
    memset(big,0,(size_t)(W*W));
    for(int y=y0>0?y0-1:0;y<y1+1&&y<W;y++)for(int x=x0>0?x0-1:0;x<x1+1&&x<W;x++){
        const int p=image[y*W+x],up=som_art_at(image,x,y-1),right=som_art_at(image,x+1,y);
        const int left=som_art_at(image,x-1,y),down=som_art_at(image,x,y+1);
        if(!(p|up|right|left|down))continue;
        const int four[4]={left==up&&left!=down&&up!=right?up:p,up==right&&up!=left&&right!=down?right:p,
                           down==left&&down!=right&&left!=up?left:p,right==down&&right!=up&&down!=left?down:p};
        for(int k=0;k<4;k++){
            const int px=2*x-ax+(k&1),py=2*y-ay+(k>>1);
            if(four[k]&&px>=0&&py>=0&&px<W&&py<W)big[py*W+px]=(uint8_t)four[k];
        }
    }
    w->x0=2*x0-ax-2>0?2*x0-ax-2:0;w->y0=2*y0-ay-2>0?2*y0-ay-2:0;
    w->x1=2*x1-ax+2<W?2*x1-ax+2:W;w->y1=2*y1-ay+2<W?2*y1-ay+2:W;
    for(int y=w->y0;y<w->y1;y++)for(int x=w->x0;x<w->x1;x++){
        const uint8_t c=big[y*W+x];
        if(!c){out[y*W+x]=0;continue;}
        out[y*W+x]=c==4u&&!(som_art_at(big,x+1,y)&&som_art_at(big,x-1,y)&&
                            som_art_at(big,x,y+1)&&som_art_at(big,x,y-1))?0u:c;
    }
    return 1;
}
/* The Monk's Bo (player, 2026-11-27: "make monk spear look like a Bo"):
 * tools/som_katana_art.py bo(), byte for byte. A straight two-pixel wooden
 * staff between the two farthest pixels of the native spear, dark bands at
 * both ends, a one-pixel outline. core: scratch. 0 when no spear. */
static long som_art_fdiv(long a,long b){        /* Python's // for b > 0 */
    long q=a/b;
    if(a%b&&a<0)q--;
    return q;
}
static int som_art_bo(const uint8_t *image,uint8_t *core,uint8_t *out){
    const int W=SOM_ART_CANVAS;
    long best=-1;int ax=0,ay=0,bx=0,by=0,count=0;
    static int px[SOM_ART_CANVAS*8],py[SOM_ART_CANVAS*8];
    for(int y=0;y<W;y++)for(int x=0;x<W;x++){
        const int c=image[y*W+x];
        if(c<1||c>4)continue;
        if(count<SOM_ART_CANVAS*8){px[count]=x;py[count]=y;}
        count++;
    }
    if(count<2||count>SOM_ART_CANVAS*8)return 0;
    for(int i=0;i<count;i++)for(int j=i+1;j<count;j++){
        const long d=(long)(px[i]-px[j])*(px[i]-px[j])+(long)(py[i]-py[j])*(py[i]-py[j]);
        if(d>best){best=d;ax=px[i];ay=py[i];bx=px[j];by=py[j];}
    }
    const long dx=bx-ax,dy=by-ay,n=labs(dx)>labs(dy)?labs(dx):labs(dy);
    memset(core,0,(size_t)(W*W));memset(out,0,(size_t)(W*W));
    for(long k=0;k<=n;k++){
        const int x=ax+(int)som_art_fdiv(2*dx*k+n,2*n),y=ay+(int)som_art_fdiv(2*dy*k+n,2*n);
        const int end=k<2||k>n-2;
        const int x2=labs(dx)>=labs(dy)?x:x+1,y2=labs(dx)>=labs(dy)?y+1:y;
        if(x>=0&&y>=0&&x<W&&y<W)core[y*W+x]=(uint8_t)(end?3:1);
        if(x2>=0&&y2>=0&&x2<W&&y2<W)core[y2*W+x2]=(uint8_t)(end?3:2);
    }
    for(int y=0;y<W;y++)for(int x=0;x<W;x++){
        if(!core[y*W+x])continue;
        static const int d4[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
        for(int q=0;q<4;q++){
            const int nx=x+d4[q][0],ny=y+d4[q][1];
            if(nx>=0&&ny>=0&&nx<W&&ny<W&&!core[ny*W+nx])out[ny*W+nx]=4u;
        }
    }
    for(int i=0;i<W*W;i++)if(core[i])out[i]=core[i];
    return 1;
}
/* Every frame of a weapon's attacks, then the walking and resting poses. */
static void som_art_frames(SomArtRom *g,int weapon,unsigned row,uint8_t *have,uint8_t *flags){
    SomArtStep steps[256];
    SomArtPiece p[20];
    memset(have,0,256u);memset(flags,0,256u);
    for(unsigned anim=0;anim<64u;anim++)for(unsigned dir=0;dir<3u;dir++){
        const unsigned n=som_art_script(g,som_art_attacks(weapon),anim,dir,steps);
        for(unsigned i=0;i<n;i++)if(!have[steps[i].frame]){have[steps[i].frame]=1;flags[steps[i].frame]=steps[i].flags;}
    }
    for(unsigned f=0;f<256u;f++)if(!have[f]){
        const unsigned n=som_art_weapon_pieces(g,row,f,0u,p);
        int ok=n!=0;
        for(unsigned i=0;i<n;i++)for(unsigned q=0;q<4u;q++)if(p[i].tile[q]>=64)ok=0;
        have[f]=(uint8_t)ok;
    }
}
/* ---- som_weapon_mask.py --kusarigama ---- */
static void som_art_mask_entry(SomArtOut *o,const uint8_t *image,SomArtWindow w,unsigned kind,unsigned frame,const uint16_t *five){
    SomArtBox b;
    if(!som_art_box_in(image,SOM_ART_CANVAS,w,112,140,&b)||b.dx<-128||b.dx>127||b.dy<-128||b.dy>127||b.w>255||b.h>255)return;
    uint16_t colours[20]={0};
    for(unsigned i=0;i<10u;i++)colours[i]=five[i%5u];
    som_art_entry(o,kind,9u,frame,1u,colours);
    som_art_frame(o,image,SOM_ART_CANVAS,112,140,1u,&b);
}
static int som_art_weapon_masks(const uint8_t *rom,size_t size,SomArtOut *o){
    SomArtRom g={rom,size,0};
    const size_t area=SOM_ART_CANVAS*SOM_ART_CANVAS;
    uint8_t *mem=malloc(4u*area);
    SomArtScratch *s=malloc(sizeof(*s));
    som_art_start(o);
    if(!mem||!s)o->bad=1;
    else{
        uint8_t *weapon=mem,*body=mem+area,*art=mem+2u*area,*core=mem+3u*area,tiles[64*32];
        uint8_t have[256]={0},flags[256]={0};
        uint16_t five[5];
        SomArtStep steps[256],whips[256];
        SomArtPiece p[20];
        som_art_frames(&g,SOM_ART_SWORD,9u,have,flags);
        som_art_weapon_tiles(&g,9u,tiles);
        som_art_weapon_colours(&g,9u,five);
        for(unsigned f=0;f<256u&&!g.bad;f++){
            if(!have[f])continue;
            unsigned n=som_art_weapon_pieces(&g,9u,f,flags[f],p);
            if(!n)continue;
            memset(weapon,0,2u*area);
            if(!som_art_render(&g,p,n,weapon,SOM_ART_CANVAS,112,140,tiles,0)){g.bad=1;break;}
            SomArtHilt hilt;
            SomArtWindow shown={SOM_ART_CANVAS,SOM_ART_CANVAS,0,0};
            for(unsigned i=0;i<n;i++){                /* the 16x16 pieces */
                const int x=112+p[i].x,y=140+p[i].y;
                if(x<shown.x0)shown.x0=x>0?x:0;
                if(y<shown.y0)shown.y0=y>0?y:0;
                if(x+16>shown.x1)shown.x1=x+16<SOM_ART_CANVAS?x+16:SOM_ART_CANVAS;
                if(y+16>shown.y1)shown.y1=y+16<SOM_ART_CANVAS?y+16:SOM_ART_CANVAS;
            }
            if(!som_art_hilt(weapon,shown,s,&hilt))continue;
            n=som_art_pieces(&g,0u,SOM_ART_SWORD,f,flags[f],p);
            som_art_render(&g,p,n,body,SOM_ART_CANVAS,112,140,NULL,0);
            const int behind=!(som_art_8(&g,som_art_off(0xD4,som_art_weapon_list(&g,9u,f)))&0x80u);
            for(unsigned k=0;k<sizeof(som_art_blades)/sizeof(som_art_blades[0]);k++){
                SomArtWindow w=shown;
                if(!som_art_blade(weapon,&hilt,&som_art_blades[k],art,core,&w))continue;
                if(behind)for(int y=w.y0;y<w.y1;y++)for(int x=w.x0;x<w.x1;x++)
                    if(body[y*SOM_ART_CANVAS+x])art[y*SOM_ART_CANVAS+x]=0;
                som_art_mask_entry(o,art,w,som_art_blades[k].pack_kind,f,som_art_blades[k].kind==SOM_ART_STAKE?som_art_wood:five);
            }
        }
        /* Kusarigama: each level-2 sword frame gets the whip's matching pose */
        uint8_t pose[256]={0},pose_flags[256]={0};
        memset(have,0,sizeof(have));
        for(unsigned dir=0;dir<3u;dir++){
            const unsigned n=som_art_script(&g,som_art_attacks(SOM_ART_SWORD),2u,dir,steps);
            const unsigned m=som_art_script(&g,som_art_attacks(SOM_ART_WHIP),2u,dir,whips);
            unsigned total=0,whip_total=0,at=0;
            for(unsigned i=0;i<n;i++)total+=steps[i].ticks;
            for(unsigned i=0;i<m;i++)whip_total+=whips[i].ticks;
            for(unsigned i=0;total&&whip_total&&i<n;i++){
                const double when=(at+steps[i].ticks/2.0)*whip_total/total;
                unsigned cursor=0;
                for(unsigned j=0;j<m;j++){
                    if(when<cursor+whips[j].ticks){
                        const unsigned f=steps[i].frame;
                        if(!have[f]){have[f]=1;pose[f]=whips[j].frame;pose_flags[f]=whips[j].flags;}
                        break;
                    }
                    cursor+=whips[j].ticks;
                }
                at+=steps[i].ticks;
            }
        }
        som_art_weapon_tiles(&g,36u,tiles);
        som_art_weapon_colours(&g,36u,five);
        for(unsigned f=0;f<256u&&!g.bad;f++)if(have[f]){
            const unsigned n=som_art_weapon_pieces(&g,36u,pose[f],pose_flags[f],p);
            if(!n)continue;
            memset(weapon,0,area);
            if(!som_art_render(&g,p,n,weapon,SOM_ART_CANVAS,112,140,tiles,0)){g.bad=1;break;}
            const SomArtWindow all={0,0,SOM_ART_CANVAS,SOM_ART_CANVAS};
            som_art_mask_entry(o,weapon,all,0xD6u,f,five);
        }
        static const struct {int weapon;unsigned row,kind;} giants[2]={{2,18u,0xEDu},{3,27u,0xEEu}};
        for(unsigned k=0;k<2u;k++){
            const unsigned row=giants[k].row;
            som_art_frames(&g,giants[k].weapon,row,have,flags);
            som_art_weapon_tiles(&g,row,tiles);
            som_art_weapon_colours(&g,row,five);
            for(unsigned f=0;f<256u&&!g.bad;f++)if(have[f]){
                unsigned n=som_art_weapon_pieces(&g,row,f,flags[f],p);
                int ok=n!=0;
                for(unsigned i=0;i<n;i++)for(unsigned q=0;q<4u;q++)if(p[i].tile[q]>=64)ok=0;
                if(!ok)continue;
                memset(weapon,0,2u*area);
                som_art_render(&g,p,n,weapon,SOM_ART_CANVAS,112,140,tiles,0);
                n=som_art_pieces(&g,0u,giants[k].weapon,f,flags[f],p);
                som_art_render(&g,p,n,body,SOM_ART_CANVAS,112,140,NULL,0);
                SomArtWindow w;
                if(!som_art_giant(weapon,body,core,art,&w))continue;
                if(!(som_art_8(&g,som_art_off(0xD4,som_art_weapon_list(&g,row,f)))&0x80u))
                    for(size_t i=0;i<area;i++)if(body[i])art[i]=0;
                som_art_mask_entry(o,art,w,giants[k].kind,f,five);
            }
        }
        {   /* the Monk's Bo: tools/som_weapon_mask.py BO, wood colours */
            const unsigned row=27u;
            som_art_frames(&g,3,row,have,flags);
            som_art_weapon_tiles(&g,row,tiles);
            for(unsigned f=0;f<256u&&!g.bad;f++)if(have[f]){
                unsigned n=som_art_weapon_pieces(&g,row,f,flags[f],p);
                int ok=n!=0;
                for(unsigned i=0;i<n;i++)for(unsigned q=0;q<4u;q++)if(p[i].tile[q]>=64)ok=0;
                if(!ok)continue;
                memset(weapon,0,2u*area);
                som_art_render(&g,p,n,weapon,SOM_ART_CANVAS,112,140,tiles,0);
                n=som_art_pieces(&g,0u,3,f,flags[f],p);
                som_art_render(&g,p,n,body,SOM_ART_CANVAS,112,140,NULL,0);
                if(!som_art_bo(weapon,core,art))continue;
                if(!(som_art_8(&g,som_art_off(0xD4,som_art_weapon_list(&g,row,f)))&0x80u))
                    for(size_t i=0;i<area;i++)if(body[i])art[i]=0;
                const SomArtWindow all={0,0,SOM_ART_CANVAS,SOM_ART_CANVAS};
                som_art_mask_entry(o,art,all,0xEFu,f,som_art_wood);
            }
        }
    }
    free(mem);free(s);
    return som_art_finish(o,&g);
}
/* ---- som_cartwheel.py ---- */
typedef uint8_t SomArtCell[SOM_ART_CELL*SOM_ART_CELL];
static void som_art_cell(SomArtRom *g,unsigned kind,int weapon,unsigned frame,unsigned flags,uint8_t *cell){
    SomArtPiece p[20];
    const unsigned n=som_art_pieces(g,kind,weapon,frame,flags,p);
    memset(cell,0,sizeof(SomArtCell));
    som_art_render(g,p,n,cell,SOM_ART_CELL,40,60,NULL,0);
}
static void som_art_cell_frame(SomArtOut *o,const uint8_t *cell,unsigned ticks){
    SomArtBox b;
    som_art_box(cell,SOM_ART_CELL,40,60,&b);
    som_art_frame(o,cell,SOM_ART_CELL,40,60,ticks,&b);
}
static unsigned som_art_standing(SomArtRom *g,unsigned kind,unsigned dir,SomArtPiece *p){
    SomArtStep steps[256];
    if(!som_art_script(g,som_art_actor(g,kind).ordinary,0u,dir,steps)){g->bad=1;return 0;}
    return som_art_pieces(g,kind,SOM_ART_NO_WEAPON,steps[0].frame,steps[0].flags,p);
}
static void som_art_head(const SomArtPiece *p,unsigned n,int *top,int *x){
    int t=p[0].y,sum=0,k=0;
    for(unsigned i=1;i<n;i++)if(p[i].y<t)t=p[i].y;
    for(unsigned i=0;i<n;i++)if(p[i].y==t){sum+=p[i].x;k++;}
    *top=t;*x=sum>=0?sum/k:-((-sum+k-1)/k);        /* floor */
}
/* The girl's body with a villager's head and colours. */
static void som_art_colour_map(SomArtRom *g,unsigned kind,unsigned hero,uint8_t *map){
    SomArtCell a,b;
    SomArtPiece p[20];
    unsigned n;
    memset(a,0,sizeof(a));memset(b,0,sizeof(b));
    n=som_art_standing(g,hero,2u,p);som_art_render(g,p,n,a,SOM_ART_CELL,40,60,NULL,0);
    n=som_art_standing(g,kind,2u,p);som_art_render(g,p,n,b,SOM_ART_CELL,40,60,NULL,0);
    unsigned votes[16][16]={{0}},first[16][16]={{0}},seq=0;
    for(unsigned i=0;i<sizeof(a);i++)if(a[i]&&b[i]){
        if(!votes[a[i]][b[i]]++)first[a[i]][b[i]]=++seq;
    }
    uint16_t hp[15],vp[15];
    som_art_colours(g,hero,hp);som_art_colours(g,kind,vp);
    map[0]=0;
    for(unsigned i=1;i<16u;i++){
        unsigned best=0;
        for(unsigned j=1;j<16u;j++)if(votes[i][j]&&(!best||votes[i][j]>votes[i][best]||
            (votes[i][j]==votes[i][best]&&first[i][j]<first[i][best])))best=j;
        int near=0;
        for(unsigned j=1;!votes[i][best]&&j<16u;j++){
            int d=0;
            for(unsigned k=0;k<15u;k+=5u){
                const int e=(hp[i-1u]>>k&31)*255/31-(vp[j-1u]>>k&31)*255/31;
                d+=e*e;
            }
            if(!near||d<near-1){near=d+1;map[i]=(uint8_t)j;}
        }
        if(votes[i][best])map[i]=(uint8_t)best;
    }
}
static int som_art_villager(SomArtRom *g,unsigned kind,unsigned hero,int weapon,unsigned dir,const SomArtStep *step,
                            const uint8_t *map,const uint8_t *tiles,unsigned row,uint8_t *cell){
    SomArtPiece p[20],head[20],body[20];
    int top,hx,vtop,vx;
    const unsigned vn=som_art_standing(g,kind,dir,p);
    if(!vn)return 0;
    som_art_head(p,vn,&vtop,&vx);
    unsigned heads=0,bodies=0;
    for(unsigned i=0;i<vn;i++)if(p[i].y==vtop)head[heads++]=p[i];
    const unsigned n=som_art_pieces(g,hero,weapon,step->frame,step->flags,p);
    som_art_head(p,n,&top,&hx);
    for(unsigned i=0;i<n;i++)if(p[i].y!=top)body[bodies++]=p[i];
    memset(cell,0,sizeof(SomArtCell));
    som_art_render(g,body,bodies,cell,SOM_ART_CELL,40,60,NULL,0);
    for(unsigned i=0;i<sizeof(SomArtCell);i++)cell[i]=map[cell[i]&15u];
    if(tiles){
        const unsigned m=som_art_weapon_pieces(g,row,step->frame,step->flags,p);
        if(!som_art_render(g,p,m,cell,SOM_ART_CELL,40,60,tiles,16u))return 0;
    }
    for(unsigned i=0;i<heads;i++){head[i].x+=hx-vx;head[i].y+=top-vtop;}
    som_art_render(g,head,heads,cell,SOM_ART_CELL,40,60,NULL,0);
    return 1;
}
static void som_art_companion(SomArtRom *g,SomArtOut *o){
    enum {GIRL=1,HEAD=0x8C};
    uint8_t map[16]={0},tiles[64*32];
    uint16_t colours[20]={0};
    SomArtStep steps[256];
    SomArtCell cell;
    som_art_colour_map(g,HEAD,GIRL,map);
    som_art_colours(g,HEAD,colours);
    for(unsigned dir=0;dir<3u;dir++){
        const unsigned n=som_art_script(g,SOM_ART_POSES,1u,dir,steps);
        som_art_entry(o,0xB0u,0xFBu,dir,n,colours);
        for(unsigned i=0;i<n;i++){
            if(!som_art_villager(g,HEAD,GIRL,SOM_ART_NO_WEAPON,dir,&steps[i],map,NULL,0,cell)){g->bad=1;return;}
            som_art_cell_frame(o,cell,steps[i].ticks);
        }
    }
    for(unsigned type=0;type<8u;type++){
        som_art_weapon_tiles(g,9u*type,tiles);
        som_art_weapon_colours(g,9u*type,colours+15);
        for(unsigned dir=0;dir<3u;dir++){
            const size_t mark=o->n;
            const unsigned entries=o->entries,n=som_art_script(g,som_art_attacks((int)type),0u,dir,steps);
            int ok=1;
            som_art_entry(o,0xB1u+type,0xFBu,dir,n,colours);
            for(unsigned i=0;ok&&i<n;i++){
                ok=som_art_villager(g,HEAD,GIRL,(int)type,dir,&steps[i],map,tiles,9u*type,cell);
                if(ok)som_art_cell_frame(o,cell,steps[i].ticks);
            }
            if(!ok){o->n=mark;o->entries=entries;break;}   /* no swing for this body */
        }
    }
}
static int som_art_bottom(const uint8_t *cell){
    for(int y=SOM_ART_CELL-1;y>=0;y--)for(int x=0;x<SOM_ART_CELL;x++)if(cell[y*SOM_ART_CELL+x])return y;
    return 0;
}
static int som_art_hero_moves(const uint8_t *rom,size_t size,SomArtOut *o){
    static const uint8_t tumble[3][6]={{47,142,143,144,145,47},{49,138,139,140,141,49},{45,134,135,136,137,45}};
    static const uint8_t tumble_ticks[6]={2,3,3,3,3,4};
    /* up, down, side: the side is now a jump like the other two (crouch 45,
     * airborne 46; player, 2026-11-28), not the game's sideways flip 65-67 */
    static const uint8_t forward[3][5]={{47,42,48,42,47},{49,50,50,50,49},{45,46,46,46,45}};
    static const uint8_t kick[3][2]={{92,93},{90,91},{88,89}};
    SomArtRom g={rom,size,0};
    SomArtCell cell,turned;
    SomArtPiece p[20];
    SomArtStep steps[256];
    const int C=SOM_ART_CELL;
    som_art_start(o);
    for(unsigned who=0;who<3u&&!g.bad;who++){
        uint16_t colours[20]={0};
        uint8_t tiles[64*32];
        som_art_colours(&g,who,colours);
        for(unsigned facing=0;facing<3u;facing++){    /* cartwheel: quarter turns */
            const unsigned n=som_art_standing(&g,who,facing==2u?1u:2u,p);
            SomArtBox b;
            memset(cell,0,sizeof(cell));
            som_art_render(&g,p,n,cell,C,40,60,NULL,0);
            if(!som_art_box(cell,C,0,0,&b)){g.bad=1;break;}
            const int x0=b.dx,y0=b.dy+SOM_ART_HEIGHT,px=(x0+x0+b.w-1)/2,py=(y0+y0+b.h-1)/2;
            som_art_entry(o,0x80u+who,0xFEu,facing,5u,colours);
            for(unsigned step=0;step<5u;step++){
                memset(turned,0,sizeof(turned));
                for(int y=0;y<C;y++)for(int x=0;x<C;x++)if(cell[y*C+x]){
                    int dx=x-px,dy=y-py;
                    for(unsigned t=0;t<step%4u;t++){const int k=dx;dx=-dy;dy=k;}
                    if(px+dx>=0&&px+dx<C&&py+dy>=0&&py+dy<C)turned[(py+dy)*C+px+dx]=cell[y*C+x];
                }
                som_art_cell_frame(o,turned,1u);
            }
        }
        for(unsigned facing=0;facing<3u;facing++){    /* Ukemi roll, on the ground */
            int ground=0;
            som_art_entry(o,0xCCu+who,0xFAu,facing,6u,colours);
            for(unsigned i=0;i<6u;i++){
                som_art_cell(&g,who,SOM_ART_NO_WEAPON,tumble[facing][i],0u,cell);
                if(!i)ground=som_art_bottom(cell);
                const int shift=ground-som_art_bottom(cell);
                memset(turned,0,sizeof(turned));
                for(int y=0;y<C;y++)if(y+shift>=0&&y+shift<C)memcpy(turned+(y+shift)*C,cell+y*C,(size_t)C);
                som_art_cell_frame(o,turned,tumble_ticks[i]);
            }
        }
        for(unsigned facing=0;facing<3u;facing++){    /* the game's own cartwheel */
            som_art_entry(o,0xC8u+who,0xF9u,facing,5u,colours);
            for(unsigned i=0;i<5u;i++){
                som_art_cell(&g,who,SOM_ART_NO_WEAPON,forward[facing][i],0u,cell);
                som_art_cell_frame(o,cell,1u);
            }
        }
        for(unsigned facing=0;facing<3u;facing++){    /* flying kick */
            som_art_entry(o,0x90u+who,0xFDu,facing,2u,colours);
            for(unsigned i=0;i<2u;i++){
                som_art_cell(&g,who,SOM_ART_NO_WEAPON,kick[facing][i],0u,cell);
                som_art_cell_frame(o,cell,i?4u:1u);
            }
        }
        som_art_weapon_tiles(&g,9u,tiles);
        som_art_weapon_colours(&g,9u,colours+15);
        for(unsigned facing=0;facing<3u;facing++){    /* Fuma clone's sword swing */
            const unsigned n=som_art_script(&g,som_art_attacks(SOM_ART_SWORD),0u,facing,steps);
            som_art_entry(o,0xA0u+who,0xFCu,facing,n,colours);
            for(unsigned i=0;i<n;i++){
                som_art_cell(&g,who,SOM_ART_SWORD,steps[i].frame,steps[i].flags,cell);
                const unsigned m=som_art_weapon_pieces(&g,9u,steps[i].frame,steps[i].flags,p);
                if(!som_art_render(&g,p,m,cell,C,40,60,tiles,16u)){g.bad=1;break;}
                som_art_cell_frame(o,cell,steps[i].ticks);
            }
        }
    }
    if(!g.bad)som_art_companion(&g,o);
    return som_art_finish(o,&g);
}
#endif
