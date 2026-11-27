#ifndef FLXTR_SOM_HAIR_STYLES_H
#define FLXTR_SOM_HAIR_STYLES_H
/* Hair styles made from the hero's own hair (player, 2026-11-27: game heads
 * are "so glitchy but not samurai clothing?" ... chose "Both": own-hair
 * styles first, game heads as an extra).
 *
 * Like the Samurai armour, nothing is pasted in: each frame the hero's
 * composed picture (som_armour_compose: palette indices of every piece the
 * game draws this frame) is read, and the style is worked out from his real
 * hair in that very frame, so it follows every pose and turn:
 *   BALD      the hair goes, the skull under it is skin;
 *   SHORT     a neat cap over the skull in his hair colours;
 *   LONG      his hair goes on down: curtains beside the face from the
 *             front, over the back from behind, behind the head from the side;
 *   PONYTAIL  a cord and a tail down the back (from behind and the side);
 *   SPIKY     points along the top of his hair.
 * The facing comes from the frame too (two eyes: front, one: side, none:
 * back): the facing byte lags behind the drawn pose during turns.
 * Output: an edit canvas (screen positions around the hero): 0xFF unchanged,
 * 0 clear, else the palette index to show. The proxy applies it through the
 * sprite tiles (som_heads.h) where the hero has pixels, and paints the rest
 * (additions beside him) on the frame. Pure. */
#include <stdint.h>
#include <string.h>

enum { SOM_OWN_NATIVE, SOM_OWN_BALD, SOM_OWN_SHORT, SOM_OWN_LONG, SOM_OWN_PONYTAIL, SOM_OWN_SPIKY, SOM_OWN_STYLES };
static const char *const som_own_style_names[SOM_OWN_STYLES]={"NATIVE","BALD","SHORT","LONG","PONYTAIL","SPIKY"};

#define SOM_EDIT_BOX 64
#define SOM_EDIT_MARGIN 8      /* the canvas starts this far up-left of the composed box */
typedef struct {
    int x0,y0;                 /* screen position of canvas (0, 0) */
    uint8_t px[SOM_EDIT_BOX][SOM_EDIT_BOX];
    int any;
} SomHairEdit;
enum { SOM_VIEW_BACK, SOM_VIEW_FRONT, SOM_VIEW_RIGHT, SOM_VIEW_LEFT, SOM_VIEW_NONE };

/* Which way the frame shows the hero: by his eye whites (index 15) in the
 * top rows. *ex, *ey: the eye centre (box coordinates) when seen. */
static inline unsigned som_hair_view(const uint8_t (*img)[48],int top,int cx,int *ex,int *ey){
    int cols[48];memset(cols,0,sizeof(cols));
    int sx=0,sy=0,n=0;
    for(int y=top+4;y<top+22&&y<48;y++)for(int x=0;x<48;x++)
        if(img[y][x]==15u){cols[x]++;sx+=x;sy+=y;n++;}
    if(!n)return SOM_VIEW_BACK;
    *ex=sx/n;*ey=sy/n;
    int clusters=0,prev=-9;
    for(int x=0;x<48;x++)if(cols[x]){if(x-prev>2)clusters++;prev=x;}
    if(clusters>=2)return SOM_VIEW_FRONT;
    return *ex>=cx?SOM_VIEW_RIGHT:SOM_VIEW_LEFT;
}
/* The hero's own hair indices: the boy 2-4, the girl 2-4 and her bow 8-9,
 * the sprite 2-4 and his puffs 8. */
static inline int som_own_hair(unsigned who,unsigned c){
    const uint16_t set=who==0u?0x001Cu:who==1u?0x031Cu:0x011Cu;
    return c<16u&&(set>>c&1u);
}
static inline void som_edit_put(SomHairEdit *e,int bx,int by,uint8_t v){
    const int x=bx+SOM_EDIT_MARGIN,y=by+SOM_EDIT_MARGIN;
    if(x<0||y<0||x>=SOM_EDIT_BOX||y>=SOM_EDIT_BOX)return;
    e->px[y][x]=v;e->any=1;
}
/* A strand shade for an added hair pixel: dark edges, light streaks. */
static inline uint8_t som_strand(int x,int y,int edge){
    if(edge)return 2u;
    return ((x+(y>>2))&3)==0?2u:((x+y)%5==0?4u:3u);
}

/* The style for hero `who` on his composed frame img (box origin at screen
 * bx0, by0). Returns 1 if anything changes. eye_from_top: remembered
 * per hero (hair top to eyes) for frames that show no eyes. */
static inline int som_hair_style_edit(unsigned style,unsigned who,const uint8_t (*img)[48],int bx0,int by0,
                                      int *eye_from_top,SomHairEdit *out){
    memset(out->px,0xFF,sizeof(out->px));out->any=0;
    out->x0=bx0-SOM_EDIT_MARGIN;out->y0=by0-SOM_EDIT_MARGIN;
    if(style==SOM_OWN_NATIVE||style>=SOM_OWN_STYLES)return 0;
    /* the hair: rows from the first hair pixel; its box and centre */
    int T=-1,B=-1,L=48,R=-1;
    for(int y=0;y<48;y++)for(int x=0;x<48;x++)
        if(som_own_hair(who,img[y][x])){if(T<0)T=y;B=y;if(x<L)L=x;if(x>R)R=x;}
    if(T<0)return 0;
    int top=-1;
    for(int y=0;y<48&&top<0;y++)for(int x=0;x<48;x++)if(img[y][x]){top=y;break;}
    int cx=(L+R+1)/2;
    {int s=0,n=0;for(int y=T;y<T+5&&y<48;y++)for(int x=0;x<48;x++)if(som_own_hair(who,img[y][x])){s+=x;n++;}if(n)cx=s/n;}
    int ex=cx,ey=T+*eye_from_top;
    const unsigned view=som_hair_view(img,top,cx,&ex,&ey);
    if(view!=SOM_VIEW_BACK&&ey-T>5&&ey-T<20)*eye_from_top=ey-T;
    else ey=T+*eye_from_top;
    const int E=ey;                                    /* the eye line */
    /* the hair's left and right edge per row */
    int rl[48],rr[48];
    for(int y=0;y<48;y++){
        rl[y]=48;rr[y]=-1;
        for(int x=0;x<48;x++)if(som_own_hair(who,img[y][x])){if(x<rl[y])rl[y]=x;if(x>rr[y])rr[y]=x;}
    }
    const int back=view==SOM_VIEW_BACK;
    if(style==SOM_OWN_BALD||style==SOM_OWN_SHORT){
        /* The skull: a dome as wide as his face this frame (skin around the
         * eyes, +1 each side), from the brows up nine rows; from behind the
         * width last seen and down to the neck. Every hair pixel goes; inside
         * the dome it is skin (bald) or a cap of his hair colours (short). */
        static int half_seen[3]={5,5,5};
        int fl=48,fr=-1;
        for(int y=E-1;y<=E+2&&y<48;y++)for(int x=0;x<48;x++){
            const unsigned c=img[y<0?0:y][x];
            if(c==6u||c==7u||(who==0u&&c==5u)){if(x<fl)fl=x;if(x>fr)fr=x;}
        }
        int a=half_seen[who%3u],dcx=cx;
        if(view==SOM_VIEW_FRONT&&fr>fl){a=(fr-fl)/2+1;dcx=(fl+fr+1)/2;if(a>=3&&a<=8)half_seen[who%3u]=a;else a=half_seen[who%3u];}
        else if(view!=SOM_VIEW_BACK)a=half_seen[who%3u]+1;   /* from the side: the skull is longer, on the hair's middle */
        const int base=E-2,bh=9,bottom=back?E+2:base;
        #define SOM_IN_DOME(px,py) ((py)<=bottom&&(py)>=base-bh&& \
            ((py)>base?((px)-dcx)*((px)-dcx)<=a*a: \
             ((px)-dcx)*((px)-dcx)*bh*bh+(base-(py))*(base-(py))*a*a<=a*a*bh*bh))
        for(int y=0;y<48;y++)for(int x=0;x<48;x++){
            const unsigned c=img[y][x];
            const int in=SOM_IN_DOME(x,y);
            const int edge=in&&(!SOM_IN_DOME(x-1,y)||!SOM_IN_DOME(x+1,y)||!SOM_IN_DOME(x,y-1));
            const int old=som_own_hair(who,c)||(c==1u&&y<E-3&&!in);
            if(!old&&!(in&&!c&&y<E))continue;
            uint8_t v=0;
            if(in){
                const int dx=x>=dcx?x-dcx:dcx-x;
                if(edge)v=1u;
                else if(style==SOM_OWN_SHORT&&(back||y<E-3))v=som_strand(x,y,dx>=a-1);
                else v=(uint8_t)(dx>=a-1||y<base-bh+2?6u:7u);
            }
            som_edit_put(out,x,y,v);
        }
        #undef SOM_IN_DOME
        return out->any;
    }
    if(style==SOM_OWN_SPIKY){
        /* points on the hair's top: every fourth column, three rows high */
        int col_top[48];
        for(int x=0;x<48;x++){col_top[x]=-1;for(int y=0;y<48;y++)if(som_own_hair(who,img[y][x])||(img[y][x]==1u&&y<E-6)){col_top[x]=y;break;}}
        for(int x=L+1;x<=R-1;x+=4){
            if(col_top[x]<0)continue;
            const int t=col_top[x];
            for(int k=1;k<=3&&t-k>=0;k++){
                const int w=k==3?0:1;                    /* a point: wider at its foot */
                if(x-w-1<0||x+w+1>=48)break;
                for(int d=-w;d<=w;d++){
                    if(img[t-k][x+d])continue;
                    som_edit_put(out,x+d,t-k,(k==3||d!=0)?1u:(k==1?3u:4u));
                }
                if(!img[t-k][x-w-1])som_edit_put(out,x-w-1,t-k,1u);
                if(!img[t-k][x+w+1])som_edit_put(out,x+w+1,t-k,1u);
            }
        }
        return out->any;
    }
    /* LONG and PONYTAIL add hair below the native hair */
    const int hb=E-3;                                   /* the hairline row */
    const int hl=rl[hb>=0&&hb<48?hb:T],hr=rr[hb>=0&&hb<48?hb:T];
    if(style==SOM_OWN_LONG){
        if(view==SOM_VIEW_FRONT&&hl<hr){
            /* curtains beside the face, from the hairline to below the chin */
            for(int y=hb;y<=E+8&&y<56;y++){
                for(int k=0;k<3;k++){
                    const int xl=hl+k-1,xr=hr-k+1;
                    const int edge=k==0||y==E+8;
                    if(xl>=0&&xl<48&&y<48&&(!img[y][xl]||som_own_hair(who,img[y][xl])||img[y][xl]==1u))
                        som_edit_put(out,xl,y,edge?1u:som_strand(xl,y,k==1));
                    if(xr>=0&&xr<48&&y<48&&(!img[y][xr]||som_own_hair(who,img[y][xr])||img[y][xr]==1u))
                        som_edit_put(out,xr,y,edge?1u:som_strand(xr,y,k==1));
                }
            }
        }else if(back){
            /* down the back: the hair's width at its bottom, eight rows more */
            const int y0=B,l=rl[B],r=rr[B];
            for(int y=y0;y<=y0+9;y++){
                const int shrink=y>y0+7?y-(y0+7):0;
                for(int x=l+shrink;x<=r-shrink;x++){
                    const int edge=x==l+shrink||x==r-shrink||y==y0+9;
                    som_edit_put(out,x,y,edge?1u:som_strand(x,y,0));
                }
            }
        }else if(view==SOM_VIEW_RIGHT||view==SOM_VIEW_LEFT){
            /* behind the head: from the back edge of the hair, down */
            const int right=view==SOM_VIEW_RIGHT;
            const int edgex=right?rl[hb>=0&&hb<48?hb:T]:rr[hb>=0&&hb<48?hb:T];
            for(int y=hb;y<=E+9;y++)for(int k=0;k<5;k++){
                const int x=right?edgex+k-1:edgex-k+1;
                if(x<0||x>=48||(y<48&&img[y][x]&&!som_own_hair(who,img[y][x])&&img[y][x]!=1u))continue;
                const int edge=k==0||k==4||y==E+9;
                som_edit_put(out,x,y,edge?1u:som_strand(x,y,k==3));
            }
        }
        return out->any;
    }
    if(style==SOM_OWN_PONYTAIL){
        if(back){
            const int y0=B+1;
            for(int x=cx-2;x<=cx+2;x++)som_edit_put(out,x,y0-1,1u);                       /* the cord: dark, every hero's outline */
            for(int y=y0;y<=y0+9;y++){
                const int w=y>y0+6?1:2;
                for(int x=cx-w;x<=cx+w;x++)
                    som_edit_put(out,x,y,(x==cx-w||x==cx+w||y==y0+9)?1u:som_strand(x,y,0));
            }
        }else if(view==SOM_VIEW_RIGHT||view==SOM_VIEW_LEFT){
            const int right=view==SOM_VIEW_RIGHT;
            const int edgex=right?rl[hb-3>=0&&hb-3<48?hb-3:T]:rr[hb-3>=0&&hb-3<48?hb-3:T];
            for(int k=0;k<10;k++){
                const int y=hb-3+k,x=right?edgex-1-k/3:edgex+1+k/3;
                for(int d=-1;d<=1;d++){
                    const int xx=x+d;
                    if(xx<0||xx>=48||(y>=0&&y<48&&img[y][xx]&&!som_own_hair(who,img[y][xx])))continue;
                    som_edit_put(out,xx,y,(d!=0||k==9||k==0)?1u:som_strand(xx,y,0));
                }
            }
        }
        return out->any;
    }
    return 0;
}
#endif
